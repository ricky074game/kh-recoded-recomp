#include "title_screen_loader.h"
#include "sw_renderer.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

// ── LZ11 Decompression ──────────────────────────────────────────────────────

std::vector<uint8_t> TitleScreenLoader::DecompressLZ11(const uint8_t* data, size_t size) {
    if (size < 4 || data[0] != 0x11) return {};

    uint32_t header = 0;
    std::memcpy(&header, data, 4);
    uint32_t decomp_size = (header >> 8) & 0xFFFFFF;
    size_t pos = 4;

    if (decomp_size == 0 && size >= 8) {
        std::memcpy(&decomp_size, data + 4, 4);
        pos = 8;
    }

    std::vector<uint8_t> out;
    out.reserve(decomp_size);

    while (out.size() < decomp_size && pos < size) {
        uint8_t flags = data[pos++];
        for (int bit = 0; bit < 8 && out.size() < decomp_size; ++bit) {
            if (flags & (0x80 >> bit)) {
                if (pos >= size) break;
                int indicator = (data[pos] >> 4) & 0x0F;
                int length, disp;
                if (indicator == 0) {
                    if (pos + 2 >= size) break;
                    length = ((data[pos] & 0x0F) << 4) | (data[pos + 1] >> 4);
                    length += 0x11;
                    disp = ((data[pos + 1] & 0x0F) << 8) | data[pos + 2];
                    disp += 1;
                    pos += 3;
                } else if (indicator == 1) {
                    if (pos + 3 >= size) break;
                    length = ((data[pos] & 0x0F) << 12) | (data[pos + 1] << 4) | (data[pos + 2] >> 4);
                    length += 0x111;
                    disp = ((data[pos + 2] & 0x0F) << 8) | data[pos + 3];
                    disp += 1;
                    pos += 4;
                } else {
                    if (pos + 1 >= size) break;
                    length = indicator + 1;
                    disp = ((data[pos] & 0x0F) << 8) | data[pos + 1];
                    disp += 1;
                    pos += 2;
                }
                for (int i = 0; i < length && out.size() < decomp_size; ++i) {
                    if (static_cast<int>(disp) > static_cast<int>(out.size()))
                        out.push_back(0);
                    else
                        out.push_back(out[out.size() - disp]);
                }
            } else {
                if (pos >= size) break;
                out.push_back(data[pos++]);
            }
        }
    }
    out.resize(std::min(out.size(), static_cast<size_t>(decomp_size)));
    return out;
}

// ── P2 Pack File Parser ─────────────────────────────────────────────────────

std::vector<std::vector<uint8_t>> TitleScreenLoader::ParseP2(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return {};

    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
    if (raw.size() < 0x200) return {};

    if (raw[0] != 'P' || raw[1] != '2') return {};

    uint16_t num_files = 0;
    std::memcpy(&num_files, raw.data() + 2, 2);

    struct Entry { uint32_t size; bool compressed; };
    std::vector<Entry> entries;
    for (int i = 0; i < num_files; ++i) {
        size_t off = 0x1C + i * 4;
        if (off + 4 > raw.size()) break;
        uint32_t val = 0;
        std::memcpy(&val, raw.data() + off, 4);
        entries.push_back({val & 0x7FFFFFFF, (val & 0x80000000) != 0});
    }

    std::vector<std::vector<uint8_t>> result;
    size_t pos = 0x200;
    for (const auto& entry : entries) {
        if (pos + entry.size > raw.size()) {
            result.emplace_back();
            pos += entry.size;
            continue;
        }
        if (entry.compressed && entry.size > 0 && raw[pos] == 0x11) {
            result.push_back(DecompressLZ11(raw.data() + pos, entry.size));
        } else if (entry.size > 0) {
            result.emplace_back(raw.data() + pos, raw.data() + pos + entry.size);
        } else {
            result.emplace_back();
        }
        pos += entry.size;
    }
    return result;
}

// ── NCGR Tile Decoder ───────────────────────────────────────────────────────

bool TitleScreenLoader::DecodeNCGR(const uint8_t* data, size_t size,
                                    std::vector<uint8_t>& out_tiles, int& out_bpp) {
    if (size < 0x20) return false;

    // Check for RGCN magic
    if (data[0] != 'R' || data[1] != 'G' || data[2] != 'C' || data[3] != 'N')
        return false;

    uint16_t header_size = 0;
    std::memcpy(&header_size, data + 0x0C, 2);
    if (header_size >= size) return false;

    // Find RAHC section
    size_t rahc_off = header_size;
    if (rahc_off + 0x20 > size) return false;
    if (std::memcmp(data + rahc_off, "RAHC", 4) != 0) {
        // Search for it
        bool found = false;
        for (size_t i = 0; i + 4 <= size; i += 4) {
            if (std::memcmp(data + i, "RAHC", 4) == 0) {
                rahc_off = i;
                found = true;
                break;
            }
        }
        if (!found) return false;
    }

    if (rahc_off + 0x20 > size) return false;

    uint32_t bpp_flag = 0;
    std::memcpy(&bpp_flag, data + rahc_off + 0x0C, 4);
    out_bpp = (bpp_flag == 3 || bpp_flag == 4) ? 4 : 8;

    uint32_t tile_data_size = 0;
    std::memcpy(&tile_data_size, data + rahc_off + 0x18, 4);

    size_t tile_data_off = rahc_off + 0x20;
    if (tile_data_off + tile_data_size > size) {
        tile_data_size = static_cast<uint32_t>(size - tile_data_off);
    }

    out_tiles.assign(data + tile_data_off, data + tile_data_off + tile_data_size);

    std::fprintf(stderr, "TitleScreen: NCGR decoded: %dbpp, %zu bytes of tile data\n",
                 out_bpp, out_tiles.size());
    return true;
}

// ── NCLR Palette Decoder ────────────────────────────────────────────────────

bool TitleScreenLoader::DecodeNCLR(const uint8_t* data, size_t size,
                                    std::vector<Color>& out_palette) {
    if (size < 0x20) return false;

    if (std::memcmp(data, "RLCN", 4) != 0) return false;

    uint16_t header_size = 0;
    std::memcpy(&header_size, data + 0x0C, 2);

    // Find TTLP section
    size_t pltt_off = header_size;
    if (pltt_off + 0x18 > size) return false;
    if (std::memcmp(data + pltt_off, "TTLP", 4) != 0) {
        bool found = false;
        for (size_t i = 0; i + 4 <= size; i += 4) {
            if (std::memcmp(data + i, "TTLP", 4) == 0) {
                pltt_off = i;
                found = true;
                break;
            }
        }
        if (!found) return false;
    }

    if (pltt_off + 0x18 > size) return false;

    uint32_t pal_data_size = 0;
    std::memcpy(&pal_data_size, data + pltt_off + 0x10, 4);

    size_t pal_data_off = pltt_off + 0x18;
    uint32_t num_colors = pal_data_size / 2;

    out_palette.clear();
    for (uint32_t i = 0; i < num_colors; ++i) {
        size_t off = pal_data_off + i * 2;
        if (off + 2 > size) break;
        uint16_t c16 = 0;
        std::memcpy(&c16, data + off, 2);
        uint8_t r = static_cast<uint8_t>((c16 & 0x1F) << 3);
        uint8_t g = static_cast<uint8_t>(((c16 >> 5) & 0x1F) << 3);
        uint8_t b = static_cast<uint8_t>(((c16 >> 10) & 0x1F) << 3);
        out_palette.push_back({r, g, b});
    }

    std::fprintf(stderr, "TitleScreen: NCLR decoded: %zu colors\n", out_palette.size());
    return true;
}

// ── KAPH Palette Decoder ────────────────────────────────────────────────────

bool TitleScreenLoader::DecodeKAPHPalette(const uint8_t* data, size_t size,
                                           std::vector<Color>& out_palette) {
    if (size < 0x60) return false;
    if (std::memcmp(data, "KAPH", 4) != 0) return false;

    // KAPH contains palette data at a known offset.
    // The section table starts at offset 0x28.
    // Section 2 (at offset from header) contains raw palette data.
    uint32_t pal_section_off = 0, pal_section_size = 0;
    std::memcpy(&pal_section_off, data + 0x2C, 4);
    std::memcpy(&pal_section_size, data + 0x30, 4);

    if (pal_section_off + pal_section_size > size) {
        // Try alternative: read raw 16-bit colors from after the header
        // Search for the palette data (typically 256 or 512 bytes of 16-bit colors)
        return false;
    }

    uint32_t num_colors = pal_section_size / 2;
    out_palette.clear();
    for (uint32_t i = 0; i < num_colors; ++i) {
        size_t off = pal_section_off + i * 2;
        if (off + 2 > size) break;
        uint16_t c16 = 0;
        std::memcpy(&c16, data + off, 2);
        uint8_t r = static_cast<uint8_t>((c16 & 0x1F) << 3);
        uint8_t g = static_cast<uint8_t>(((c16 >> 5) & 0x1F) << 3);
        uint8_t b = static_cast<uint8_t>(((c16 >> 10) & 0x1F) << 3);
        out_palette.push_back({r, g, b});
    }

    std::fprintf(stderr, "TitleScreen: KAPH palette decoded: %zu colors\n", out_palette.size());
    return !out_palette.empty();
}

// ── Tiled BG Renderer ───────────────────────────────────────────────────────

void TitleScreenLoader::RenderTiledBG(uint32_t* pixels, int screen_w, int screen_h,
                                       const std::vector<uint8_t>& tiles, int bpp,
                                       const std::vector<Color>& palette,
                                       const std::vector<TileMapEntry>& map,
                                       int map_w, int map_h) {
    const int tiles_per_row = map_w / 8;
    const int tile_size = (bpp == 4) ? 32 : 64;

    for (size_t map_idx = 0; map_idx < map.size(); ++map_idx) {
        const auto& entry = map[map_idx];
        int tx = (static_cast<int>(map_idx) % tiles_per_row) * 8;
        int ty = (static_cast<int>(map_idx) / tiles_per_row) * 8;

        size_t tile_off = static_cast<size_t>(entry.tile_idx) * tile_size;
        if (tile_off + tile_size > tiles.size()) continue;

        for (int py = 0; py < 8; ++py) {
            for (int px = 0; px < 8; ++px) {
                int src_x = entry.h_flip ? (7 - px) : px;
                int src_y = entry.v_flip ? (7 - py) : py;

                int color_idx;
                if (bpp == 4) {
                    size_t byte_off = tile_off + src_y * 4 + src_x / 2;
                    if (byte_off >= tiles.size()) continue;
                    uint8_t byte_val = tiles[byte_off];
                    color_idx = (src_x & 1) ? ((byte_val >> 4) & 0x0F) : (byte_val & 0x0F);
                    color_idx += entry.pal_idx * 16;
                } else {
                    size_t byte_off = tile_off + src_y * 8 + src_x;
                    if (byte_off >= tiles.size()) continue;
                    color_idx = tiles[byte_off];
                }

                int dest_x = tx + px;
                int dest_y = ty + py;
                if (dest_x >= screen_w || dest_y >= screen_h) continue;

                if (color_idx == 0) continue;  // Transparent

                Color c = {0, 0, 0};
                if (color_idx < static_cast<int>(palette.size())) {
                    c = palette[color_idx];
                }

                // RGBA8888: R<<24 | G<<16 | B<<8 | A
                pixels[dest_y * screen_w + dest_x] =
                    (static_cast<uint32_t>(c.r) << 24) |
                    (static_cast<uint32_t>(c.g) << 16) |
                    (static_cast<uint32_t>(c.b) << 8) | 0xFF;
            }
        }
    }
}

// ── Load ────────────────────────────────────────────────────────────────────

bool TitleScreenLoader::Load(const std::string& dataDir) {
    namespace fs = std::filesystem;

    const fs::path ttl_dir = fs::path(dataDir) / "ttl";
    const fs::path ttl_p2_path = ttl_dir / "ttl.p2";
    const fs::path ttl_en_p2_path = ttl_dir / "ttl_en.p2";

    if (!fs::exists(ttl_p2_path)) {
        std::fprintf(stderr, "TitleScreen: ttl.p2 not found at %s\n", ttl_p2_path.c_str());
        return false;
    }

    std::fprintf(stderr, "TitleScreen: Loading from %s\n", ttl_dir.c_str());

    // Parse the main title P2 pack
    auto ttl_files = ParseP2(ttl_p2_path.string());
    std::fprintf(stderr, "TitleScreen: ttl.p2 -> %zu files\n", ttl_files.size());

    // Parse the English text P2 pack
    std::vector<std::vector<uint8_t>> ttl_en_files;
    if (fs::exists(ttl_en_p2_path)) {
        ttl_en_files = ParseP2(ttl_en_p2_path.string());
        std::fprintf(stderr, "TitleScreen: ttl_en.p2 -> %zu files\n", ttl_en_files.size());
    }

    // Strategy: Scan all decompressed files for NCGR/NCLR/NSCR sections.
    // The title screen uses:
    //   - ttl_file0 (KAPH): animation/palette container
    //   - ttl_file2: raw NDS 16-bit palette data
    //   - ttl_en_file0 (NCGR): English title text tiles
    //   - ttl_file3/4: additional tile data

    // 1. Try to extract palette from ttl_file2 (raw 16-bit NDS colors)
    if (ttl_files.size() > 2 && !ttl_files[2].empty()) {
        const auto& pal_raw = ttl_files[2];
        palette_.clear();
        for (size_t i = 0; i + 1 < pal_raw.size(); i += 2) {
            uint16_t c16 = static_cast<uint16_t>(pal_raw[i]) |
                           (static_cast<uint16_t>(pal_raw[i + 1]) << 8);
            uint8_t r = static_cast<uint8_t>((c16 & 0x1F) << 3);
            uint8_t g = static_cast<uint8_t>(((c16 >> 5) & 0x1F) << 3);
            uint8_t b = static_cast<uint8_t>(((c16 >> 10) & 0x1F) << 3);
            palette_.push_back({r, g, b});
        }
        std::fprintf(stderr, "TitleScreen: Raw palette: %zu colors from ttl_file2\n",
                     palette_.size());
    }

    // 2. Try KAPH palette from ttl_file0
    if (palette_.empty() && ttl_files.size() > 0 && !ttl_files[0].empty()) {
        DecodeKAPHPalette(ttl_files[0].data(), ttl_files[0].size(), palette_);
    }

    // 3. Extract tile graphics from the NCGR in ttl_en_file0
    bool have_tiles = false;
    if (!ttl_en_files.empty() && !ttl_en_files[0].empty()) {
        const auto& ncgr_data = ttl_en_files[0];
        if (ncgr_data.size() >= 4 &&
            ncgr_data[0] == 'R' && ncgr_data[1] == 'G' &&
            ncgr_data[2] == 'C' && ncgr_data[3] == 'N') {
            have_tiles = DecodeNCGR(ncgr_data.data(), ncgr_data.size(),
                                     tile_data_, tile_bpp_);
        }
    }

    // 4. Also try ttl_file1 for tile data (main BG tiles)
    if (!have_tiles && ttl_files.size() > 1 && !ttl_files[1].empty()) {
        const auto& tile_raw = ttl_files[1];
        // Search for embedded NCGR
        for (size_t i = 0; i + 4 <= tile_raw.size(); i += 4) {
            if (std::memcmp(tile_raw.data() + i, "RGCN", 4) == 0) {
                have_tiles = DecodeNCGR(tile_raw.data() + i, tile_raw.size() - i,
                                         tile_data_, tile_bpp_);
                if (have_tiles) break;
            }
        }
    }

    // If we still don't have proper decoded tile data but we do have raw tile
    // bytes from the NCGR, generate a simple tile map that displays them
    // sequentially across the 256x192 screen.
    if (have_tiles && !tile_data_.empty() && palette_.size() >= 16) {
        int tile_size = (tile_bpp_ == 4) ? 32 : 64;
        int num_tiles = static_cast<int>(tile_data_.size()) / tile_size;
        int tiles_w = 32;   // 256 / 8
        int tiles_h = 24;   // 192 / 8

        tile_map_.clear();
        for (int t = 0; t < tiles_w * tiles_h && t < num_tiles; ++t) {
            tile_map_.push_back({static_cast<uint16_t>(t), false, false, 0});
        }
        map_width_ = 256;
        map_height_ = 192;

        std::fprintf(stderr, "TitleScreen: Generated sequential tile map: %dx%d tiles"
                     " (%d available)\n", tiles_w, tiles_h, num_tiles);
    }

    // 5. If we have a default palette but it's too small, generate a grayscale ramp
    if (palette_.size() < 16) {
        palette_.clear();
        for (int i = 0; i < 256; ++i) {
            uint8_t v = static_cast<uint8_t>(i);
            palette_.push_back({v, v, v});
        }
        std::fprintf(stderr, "TitleScreen: Using fallback grayscale palette\n");
    }

    loaded_ = have_tiles && !tile_data_.empty();
    if (!loaded_) {
        std::fprintf(stderr, "TitleScreen: WARNING: Could not decode tile data. "
                     "Will render fallback title screen.\n");
    }

    return true;
}

// ── Render ──────────────────────────────────────────────────────────────────

void TitleScreenLoader::Render(SoftwareRenderer* renderer) {
    if (!renderer) return;

    // Access the internal pixel buffers through the public interface.
    // We'll render into temp buffers and then use SubmitFrame to set them.

    // Create RGBA pixel arrays for both screens
    static uint32_t top_pixels[256 * 192];
    static uint32_t bottom_pixels[256 * 192];

    // Clear to dark blue (KH signature color)
    const uint32_t bg_color = (0x08u << 24) | (0x0Cu << 16) | (0x28u << 8) | 0xFF;
    for (int i = 0; i < 256 * 192; ++i) {
        top_pixels[i] = bg_color;
        bottom_pixels[i] = bg_color;
    }

    if (loaded_ && !tile_data_.empty() && !tile_map_.empty()) {
        RenderTiledBG(top_pixels, 256, 192,
                      tile_data_, tile_bpp_, palette_, tile_map_,
                      map_width_, map_height_);
    } else {
        // Fallback: Render a recognizable "KINGDOM HEARTS" title screen pattern
        // Dark blue gradient background with simple text placeholder
        for (int y = 0; y < 192; ++y) {
            for (int x = 0; x < 256; ++x) {
                int grad = std::min(255, y * 2);
                uint8_t r = static_cast<uint8_t>(std::min(255, grad / 6));
                uint8_t g = static_cast<uint8_t>(std::min(255, grad / 4));
                uint8_t b = static_cast<uint8_t>(std::min(255, 40 + grad / 2));
                top_pixels[y * 256 + x] = (static_cast<uint32_t>(r) << 24) |
                                            (static_cast<uint32_t>(g) << 16) |
                                            (static_cast<uint32_t>(b) << 8) | 0xFF;
            }
        }

        // Draw a simple "KH" logo shape using pixel art
        // Center area with bright pixels representing the keyblade cross motif
        const uint32_t white = 0xFFFFFFFF;
        const uint32_t gold  = 0xFFD700FF;

        // Horizontal bar (keyblade shaft)
        for (int x = 80; x < 176; ++x) {
            for (int dy = -1; dy <= 1; ++dy) {
                int y = 96 + dy;
                if (y >= 0 && y < 192)
                    top_pixels[y * 256 + x] = gold;
            }
        }
        // Vertical bar (crossguard)
        for (int y = 70; y < 122; ++y) {
            for (int dx = -1; dx <= 1; ++dx) {
                int x = 128 + dx;
                if (x >= 0 && x < 256)
                    top_pixels[y * 256 + x] = gold;
            }
        }
        // Key teeth at the right end
        for (int i = 0; i < 3; ++i) {
            int bx = 170 + i * 2;
            for (int dy = -3 - i; dy <= 3 + i; ++dy) {
                int y = 96 + dy;
                if (y >= 0 && y < 192 && bx >= 0 && bx < 256)
                    top_pixels[y * 256 + bx] = gold;
            }
        }
        // Handle at the left end
        for (int a = 0; a < 360; a += 5) {
            float rad = static_cast<float>(a) * 3.14159f / 180.0f;
            int px = 80 + static_cast<int>(8.0f * std::cos(rad));
            int py = 96 + static_cast<int>(8.0f * std::sin(rad));
            if (px >= 0 && px < 256 && py >= 0 && py < 192)
                top_pixels[py * 256 + px] = gold;
        }

        // "PRESS START" text as pixel blocks on bottom screen
        // Simple 5x5 pixel font for key characters
        const char* press_start = "PRESS START";
        int text_x = 78;
        int text_y = 90;
        for (int ci = 0; press_start[ci]; ++ci) {
            for (int py = 0; py < 5; ++py) {
                for (int px = 0; px < 5; ++px) {
                    // Simple block letter pattern
                    bool on = false;
                    char ch = press_start[ci];
                    if (ch == ' ') { /* skip */ }
                    else if (py == 0 || py == 4) on = true;
                    else if (px == 0) on = true;
                    else if (ch == 'P' && py == 2) on = true;
                    else if (ch == 'R' && (py == 2 || (py > 2 && px == 4))) on = true;
                    else if (ch == 'E' && py == 2) on = true;
                    else if (ch == 'S' && py == 2) on = true;
                    else if (ch == 'S' && py < 2 && px == 4) on = false;
                    else if (ch == 'S' && py > 2 && px == 0) on = false;
                    else if (ch == 'T' && py == 0 && px >= 0) on = true;
                    else if (ch == 'T' && px == 2) on = true;
                    else if (ch == 'T' && px != 2) on = false;
                    else if (ch == 'A' && (px == 0 || px == 4)) on = true;
                    else if (ch == 'A' && py == 2) on = true;

                    if (on) {
                        int dx = text_x + ci * 9 + px;
                        int dy = text_y + py;
                        if (dx >= 0 && dx < 256 && dy >= 0 && dy < 192) {
                            bottom_pixels[dy * 256 + dx] = white;
                        }
                    }
                }
            }
        }
    }

    // Write directly to the renderer's pixel buffers using the lock-free copy
    renderer->SetFramebuffers(top_pixels, bottom_pixels);

    std::fprintf(stderr, "TitleScreen: Rendered title screen (%s)\n",
                 loaded_ ? "from ROM data" : "fallback");
}
