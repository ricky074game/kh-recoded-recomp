#include "sw_renderer.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cmath>
#include <cstring>

SoftwareRenderer::SoftwareRenderer() {
    std::memset(m_pixels_top, 0, sizeof(m_pixels_top));
    std::memset(m_pixels_bottom, 0, sizeof(m_pixels_bottom));
}

SoftwareRenderer::~SoftwareRenderer() {
}

// Simple edge function for rasterization
static inline int EdgeFunction(const std::pair<int, int>& a, const std::pair<int, int>& b, const std::pair<int, int>& c) {
    return (c.first - a.first) * (b.second - a.second) - (c.second - a.second) * (b.first - a.first);
}

static inline uint32_t PackRGBA(uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
    return ((r & 0xFF) << 24) | ((g & 0xFF) << 16) | ((b & 0xFF) << 8) | (a & 0xFF);
}

static inline void UnpackRGBA(uint32_t color, uint32_t& r, uint32_t& g, uint32_t& b, uint32_t& a) {
    r = (color >> 24) & 0xFF;
    g = (color >> 16) & 0xFF;
    b = (color >> 8) & 0xFF;
    a = color & 0xFF;
}

static bool IsInWindowRect(uint8_t x1, uint8_t x2, uint8_t y1, uint8_t y2, int x, int y) {
    const bool x_in = (x1 <= x2) ? (x >= x1 && x < x2) : (x >= x1 || x < x2);
    const bool y_in = (y1 <= y2) ? (y >= y1 && y < y2) : (y >= y1 || y < y2);
    return x_in && y_in;
}

static uint8_t ResolveWindowMask(const WindowControl& windows, int x, int y) {
    if (IsInWindowRect(windows.win0_x1, windows.win0_x2, windows.win0_y1, windows.win0_y2, x, y)) {
        return windows.winin_win0;
    }
    if (IsInWindowRect(windows.win1_x1, windows.win1_x2, windows.win1_y1, windows.win1_y2, x, y)) {
        return windows.winin_win1;
    }
    return windows.winout_outside;
}

static uint32_t BlendPixel(uint32_t dst, uint32_t src, const BlendControl& blend, bool effects_enabled) {
    if (!effects_enabled || blend.mode == 0) {
        return src;
    }

    uint32_t sr, sg, sb, sa;
    uint32_t dr, dg, db, da;
    UnpackRGBA(src, sr, sg, sb, sa);
    UnpackRGBA(dst, dr, dg, db, da);

    if (blend.mode == 1) {
        const uint32_t eva = std::min<uint32_t>(blend.eva, 16);
        const uint32_t evb = std::min<uint32_t>(blend.evb, 16);
        const uint32_t r = std::min<uint32_t>(255, (sr * eva + dr * evb) / 16);
        const uint32_t g = std::min<uint32_t>(255, (sg * eva + dg * evb) / 16);
        const uint32_t b = std::min<uint32_t>(255, (sb * eva + db * evb) / 16);
        return PackRGBA(r, g, b, 0xFF);
    }

    if (blend.mode == 2) {
        const uint32_t evy = std::min<uint32_t>(blend.evy, 16);
        const uint32_t r = std::min<uint32_t>(255, sr + ((255 - sr) * evy) / 16);
        const uint32_t g = std::min<uint32_t>(255, sg + ((255 - sg) * evy) / 16);
        const uint32_t b = std::min<uint32_t>(255, sb + ((255 - sb) * evy) / 16);
        return PackRGBA(r, g, b, 0xFF);
    }

    const uint32_t evy = std::min<uint32_t>(blend.evy, 16);
    const uint32_t r = (sr * (16 - evy)) / 16;
    const uint32_t g = (sg * (16 - evy)) / 16;
    const uint32_t b = (sb * (16 - evy)) / 16;
    return PackRGBA(r, g, b, 0xFF);
}

void SoftwareRenderer::SubmitFrame(const std::vector<GXVertex>& vertices, 
                               const std::vector<GXPolygon>& polygons) {
    std::lock_guard<std::mutex> lock(m_render_mutex);
    
    // Clear top screen to black
    for (int i = 0; i < 256 * 192; ++i) {
        m_pixels_top[i] = 0x000000FF; // RGBA
    }

    // Software Rasterizer for DS 3D Geometry
    for (const auto& poly : polygons) {
        if (poly.vertex_count < 3) continue;

        // Extract and project vertices for the polyhedron
        std::vector<std::pair<int, int>> screen_pts;
        for (int i = 0; i < poly.vertex_count; ++i) {
            uint32_t idx = poly.vertex_indices[i];
            if (idx >= vertices.size()) continue;
            
            const auto& v = vertices[idx];
            // Perspective divide
            float ndc_x = v.position.x;
            float ndc_y = v.position.y;
            if (v.position.w != 0.0f) {
                ndc_x /= v.position.w;
                ndc_y /= v.position.w;
            }
            
            // Viewport transform: 
            // map (-1.0 to 1.0) -> (0 to 256) and (-1.0 to 1.0) -> (0 to 192)
            int sx = static_cast<int>((ndc_x + 1.0f) * 128.0f);
            int sy = static_cast<int>((1.0f - ndc_y) * 96.0f); // DS Y goes down
            screen_pts.push_back({sx, sy});
        }

        // Fan triangulate
        for (size_t i = 1; i + 1 < screen_pts.size(); ++i) {
            auto v0 = screen_pts[0];
            auto v1 = screen_pts[i];
            auto v2 = screen_pts[i + 1];

            // Bounding box for the triangle
            int minX = std::max(0, std::min({v0.first, v1.first, v2.first}));
            int minY = std::max(0, std::min({v0.second, v1.second, v2.second}));
            int maxX = std::min(255, std::max({v0.first, v1.first, v2.first}));
            int maxY = std::min(191, std::max({v0.second, v1.second, v2.second}));

            // Calculate triangle area for barycentric coordinates
            int area = EdgeFunction(v0, v1, v2);
            if (area == 0) continue;
            // Backface culling
            if (area < 0) {
                std::swap(v1, v2); 
                area = -area;
            }

            // Rasterize
            for (int y = minY; y <= maxY; ++y) {
                for (int x = minX; x <= maxX; ++x) {
                    std::pair<int, int> p{x, y};
                    int w0 = EdgeFunction(v1, v2, p);
                    int w1 = EdgeFunction(v2, v0, p);
                    int w2 = EdgeFunction(v0, v1, p);

                    if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
                        // Inside triangle - map color from first vertex
                        uint32_t r = 255, g = 255, b = 255, a = 255;
                        if (poly.vertex_count > 0) {
                            r = static_cast<uint32_t>(std::clamp(vertices[poly.vertex_indices[0]].r * 255.0f, 0.0f, 255.0f));
                            g = static_cast<uint32_t>(std::clamp(vertices[poly.vertex_indices[0]].g * 255.0f, 0.0f, 255.0f));
                            b = static_cast<uint32_t>(std::clamp(vertices[poly.vertex_indices[0]].b * 255.0f, 0.0f, 255.0f));
                        }
                        
                        m_pixels_top[y * 256 + x] = (r << 24) | (g << 16) | (b << 8) | a;
                    }
                }
            }
        }
    }
}

void SoftwareRenderer::SubmitFrame2D(const std::vector<Sprite2D>& sprites,
                                 const std::array<BGLayer2D, 4>& bg_layers,
                                 const BlendControl& blend,
                                 const WindowControl& windows,
                                 bool render_to_top,
                                 const uint8_t* vram_data,
                                 size_t vram_size,
                                 const uint8_t* palette_data,
                                 size_t palette_size) {
    std::lock_guard<std::mutex> lock(m_render_mutex);

    uint32_t* target_pixels = render_to_top ? m_pixels_top : m_pixels_bottom;
    for (int i = 0; i < 256 * 192; ++i) target_pixels[i] = 0x000000FF;

    const bool have_vram = (vram_data != nullptr && vram_size > 0);
    const bool have_palette = (palette_data != nullptr && palette_size > 0);

    // --- Render BG and OBJ by priority (3 = lowest, 0 = highest) ---
    for (int p = 3; p >= 0; --p) {
        // --- BG layers at this priority ---
        for (int i = 0; i < 4; ++i) {
            const auto& bg = bg_layers[i];
            if (!bg.enabled || bg.priority != p) continue;

            if (have_vram && have_palette && bg.mode == 0) {
                // ---- Text mode: decode real tiles from VRAM ----
                // Engine A: char_base selects tile data in 16KB blocks
                // Engine A: screen_base selects tile map in 2KB blocks
                // VRAM layout for Engine A BG: starts at offset 0 in VRAM
                const uint32_t char_offset =
                    static_cast<uint32_t>(bg.control.char_base) * 0x4000;
                const uint32_t screen_offset =
                    static_cast<uint32_t>(bg.control.screen_base) * 0x0800;
                const bool is_8bpp = bg.control.is_8bpp;
                const int tile_bytes = is_8bpp ? 64 : 32;

                const int map_w = bg.control.map_width;
                const int map_h = bg.control.map_height;

                for (int y = 0; y < 192; ++y) {
                    for (int x = 0; x < 256; ++x) {
                        const uint8_t windowMask = ResolveWindowMask(windows, x, y);
                        const bool bg_in_window = ((windowMask >> i) & 0x1) != 0;
                        const bool effects_enabled = ((windowMask >> 5) & 0x1) != 0;
                        if (!bg_in_window) continue;

                        // Apply scrolling
                        int px_x = (x + bg.scroll_x) % map_w;
                        int px_y = (y + bg.scroll_y) % map_h;
                        if (px_x < 0) px_x += map_w;
                        if (px_y < 0) px_y += map_h;

                        // Determine which 256x256 block we're in
                        // (for screen sizes > 256x256)
                        int block_x = px_x / 256;
                        int block_y = px_y / 256;
                        int local_x = px_x % 256;
                        int local_y = px_y % 256;

                        // Block offset in the screen map
                        uint32_t block_offset = 0;
                        if (bg.control.screen_size == 0) {
                            block_offset = 0;
                        } else if (bg.control.screen_size == 1) {
                            // 512x256: 2 blocks side by side
                            block_offset = block_x * 0x800;
                        } else if (bg.control.screen_size == 2) {
                            // 256x512: 2 blocks stacked
                            block_offset = block_y * 0x800;
                        } else {
                            // 512x512: 2x2 grid
                            block_offset = (block_y * 2 + block_x) * 0x800;
                        }

                        // Tile coordinates within the 256x256 block
                        int tile_x = local_x / 8;
                        int tile_y = local_y / 8;
                        int pixel_in_tile_x = local_x % 8;
                        int pixel_in_tile_y = local_y % 8;

                        // Read the screen map entry (2 bytes per tile)
                        uint32_t map_addr = screen_offset + block_offset +
                                            (tile_y * 32 + tile_x) * 2;
                        if (map_addr + 2 > vram_size) continue;

                        uint16_t map_entry =
                            static_cast<uint16_t>(vram_data[map_addr]) |
                            (static_cast<uint16_t>(vram_data[map_addr + 1]) << 8);

                        uint16_t tile_idx = map_entry & 0x3FF;
                        bool h_flip = (map_entry >> 10) & 1;
                        bool v_flip = (map_entry >> 11) & 1;
                        uint8_t pal_num = (map_entry >> 12) & 0xF;

                        // Apply flip
                        int src_px = h_flip ? (7 - pixel_in_tile_x) : pixel_in_tile_x;
                        int src_py = v_flip ? (7 - pixel_in_tile_y) : pixel_in_tile_y;

                        // Read the tile pixel from character data
                        uint32_t tile_addr = char_offset +
                            static_cast<uint32_t>(tile_idx) * tile_bytes;
                        int color_idx;

                        if (is_8bpp) {
                            uint32_t byte_addr = tile_addr + src_py * 8 + src_px;
                            if (byte_addr >= vram_size) continue;
                            color_idx = vram_data[byte_addr];
                        } else {
                            uint32_t byte_addr = tile_addr + src_py * 4 + src_px / 2;
                            if (byte_addr >= vram_size) continue;
                            uint8_t byte_val = vram_data[byte_addr];
                            color_idx = (src_px & 1) ?
                                ((byte_val >> 4) & 0x0F) : (byte_val & 0x0F);
                        }

                        if (color_idx == 0) continue;  // Transparent

                        // Look up in palette RAM
                        uint32_t pal_addr;
                        if (is_8bpp) {
                            pal_addr = static_cast<uint32_t>(color_idx) * 2;
                        } else {
                            pal_addr = (static_cast<uint32_t>(pal_num) * 16 +
                                        color_idx) * 2;
                        }
                        // BG palette for engine A starts at offset 0,
                        // for engine B at offset 0x200 (handled by caller)
                        if (pal_addr + 2 > palette_size) continue;

                        uint16_t rgb555 =
                            static_cast<uint16_t>(palette_data[pal_addr]) |
                            (static_cast<uint16_t>(palette_data[pal_addr + 1]) << 8);

                        uint32_t r = ((rgb555 >> 0)  & 0x1F) * 255 / 31;
                        uint32_t g = ((rgb555 >> 5)  & 0x1F) * 255 / 31;
                        uint32_t b = ((rgb555 >> 10) & 0x1F) * 255 / 31;

                        uint32_t src_color = (r << 24) | (g << 16) | (b << 8) | 0xFF;
                        uint32_t& dst = target_pixels[y * 256 + x];
                        dst = BlendPixel(dst, src_color, blend, effects_enabled);
                    }
                }
            } else if (have_vram && have_palette && bg.mode == 1) {
                // ---- Affine mode ----
                const uint32_t char_offset =
                    static_cast<uint32_t>(bg.control.char_base) * 0x4000;
                const uint32_t screen_offset =
                    static_cast<uint32_t>(bg.control.screen_base) * 0x0800;

                // Affine BGs use 8bpp, map entries are single bytes (tile index)
                int map_dim;  // map is square: 128, 256, 512, 1024 pixels
                switch (bg.control.screen_size) {
                    case 0: map_dim = 128; break;
                    case 1: map_dim = 256; break;
                    case 2: map_dim = 512; break;
                    default: map_dim = 1024; break;
                }
                int tiles_per_row = map_dim / 8;

                int32_t ref_x = bg.affine.ref_x;
                int32_t ref_y = bg.affine.ref_y;

                for (int y = 0; y < 192; ++y) {
                    int32_t map_x = ref_x;
                    int32_t map_y = ref_y;

                    for (int x = 0; x < 256; ++x) {
                        const uint8_t windowMask = ResolveWindowMask(windows, x, y);
                        const bool bg_in_window = ((windowMask >> i) & 0x1) != 0;
                        const bool effects_enabled = ((windowMask >> 5) & 0x1) != 0;
                        if (!bg_in_window) { map_x += bg.affine.pa; map_y += bg.affine.pc; continue; }

                        int px_x = map_x >> 8;
                        int px_y = map_y >> 8;

                        if (bg.control.wrap) {
                            px_x = ((px_x % map_dim) + map_dim) % map_dim;
                            px_y = ((px_y % map_dim) + map_dim) % map_dim;
                        } else if (px_x < 0 || px_x >= map_dim || px_y < 0 || px_y >= map_dim) {
                            map_x += bg.affine.pa;
                            map_y += bg.affine.pc;
                            continue;
                        }

                        int tile_x = px_x / 8;
                        int tile_y = px_y / 8;
                        int in_tile_x = px_x % 8;
                        int in_tile_y = px_y % 8;

                        uint32_t map_addr = screen_offset +
                            static_cast<uint32_t>(tile_y * tiles_per_row + tile_x);
                        if (map_addr >= vram_size) { map_x += bg.affine.pa; map_y += bg.affine.pc; continue; }

                        uint8_t tile_idx = vram_data[map_addr];
                        uint32_t tile_addr = char_offset +
                            static_cast<uint32_t>(tile_idx) * 64 + in_tile_y * 8 + in_tile_x;
                        if (tile_addr >= vram_size) { map_x += bg.affine.pa; map_y += bg.affine.pc; continue; }

                        int color_idx = vram_data[tile_addr];
                        if (color_idx == 0) { map_x += bg.affine.pa; map_y += bg.affine.pc; continue; }

                        uint32_t pal_addr = static_cast<uint32_t>(color_idx) * 2;
                        if (pal_addr + 2 > palette_size) { map_x += bg.affine.pa; map_y += bg.affine.pc; continue; }

                        uint16_t rgb555 =
                            static_cast<uint16_t>(palette_data[pal_addr]) |
                            (static_cast<uint16_t>(palette_data[pal_addr + 1]) << 8);
                        uint32_t r = ((rgb555 >> 0)  & 0x1F) * 255 / 31;
                        uint32_t g = ((rgb555 >> 5)  & 0x1F) * 255 / 31;
                        uint32_t b = ((rgb555 >> 10) & 0x1F) * 255 / 31;

                        uint32_t src_color = (r << 24) | (g << 16) | (b << 8) | 0xFF;
                        uint32_t& dst = target_pixels[y * 256 + x];
                        dst = BlendPixel(dst, src_color, blend, effects_enabled);

                        map_x += bg.affine.pa;
                        map_y += bg.affine.pc;
                    }

                    ref_x += bg.affine.pb;
                    ref_y += bg.affine.pd;
                }
            } else {
                // ---- Fallback: checkerboard pattern (no VRAM data available) ----
                int32_t current_ref_x = bg.affine.ref_x;
                int32_t current_ref_y = bg.affine.ref_y;

                for (int y = 0; y < 192; ++y) {
                    int32_t map_x = current_ref_x;
                    int32_t map_y = current_ref_y;

                    for (int x = 0; x < 256; ++x) {
                        const uint8_t windowMask = ResolveWindowMask(windows, x, y);
                        const bool bg_enabled_in_window = ((windowMask >> i) & 0x1) != 0;
                        const bool effects_enabled = ((windowMask >> 5) & 0x1) != 0;
                        if (!bg_enabled_in_window) continue;

                        int px_x, px_y;
                        if (bg.mode == 0) {
                            px_x = (x + bg.scroll_x) & 0x1FF;
                            px_y = (y + bg.scroll_y) & 0x1FF;
                        } else {
                            px_x = map_x >> 8;
                            px_y = map_y >> 8;
                            map_x += bg.affine.pa;
                            map_y += bg.affine.pc;
                        }

                        if (px_x < 0 || px_x >= 512 || px_y < 0 || px_y >= 512) continue;

                        const bool tile_on = ((px_x / 8) + (px_y / 8)) % 2 == 0;
                        if (!tile_on) continue;

                        const uint32_t src = PackRGBA((i * 60) + 80, px_x & 0xFF, px_y & 0xFF, 0xFF);
                        uint32_t& dst = target_pixels[y * 256 + x];
                        dst = BlendPixel(dst, src, blend, effects_enabled);
                    }

                    current_ref_x += bg.affine.pb;
                    current_ref_y += bg.affine.pd;
                }
            }
        }

        // --- OBJ sprites at this priority ---
        for (const auto& sprite : sprites) {
            if (sprite.priority != p) continue;

            const int sx = static_cast<int>(sprite.x);
            const int sy = static_cast<int>(sprite.y);
            const int sw = static_cast<int>(sprite.width);
            const int sh = static_cast<int>(sprite.height);
            const int cx = sw / 2;
            const int cy = sh / 2;

            // OBJ tile data is separate from BG tile data.
            // Engine A OBJ VRAM offset: typically at 0x10000 in the VRAM
            // (after the first 64KB used for BG).
            // This offset depends on DISPCNT OBJ Character VRAM mapping bit.
            const uint32_t obj_vram_base = 0x10000;
            const int tile_bytes = sprite.is_8bpp ? 64 : 32;

            for (int py = 0; py < sh; ++py) {
                for (int px = 0; px < sw; ++px) {
                    int src_x = px;
                    int src_y = py;

                    if (sprite.has_affine) {
                        const int dx = (px - cx) << 8;
                        const int dy = (py - cy) << 8;
                        const int tx = (sprite.affine_pa * dx + sprite.affine_pb * dy) >> 16;
                        const int ty = (sprite.affine_pc * dx + sprite.affine_pd * dy) >> 16;
                        src_x = tx + cx;
                        src_y = ty + cy;
                    } else {
                        if (sprite.hflip) src_x = sw - 1 - px;
                        if (sprite.vflip) src_y = sh - 1 - py;
                    }

                    if (src_x < 0 || src_x >= sw || src_y < 0 || src_y >= sh) continue;

                    const int tx = sx + px;
                    const int ty = sy + py;
                    if (tx < 0 || tx >= 256 || ty < 0 || ty >= 192) continue;

                    const uint8_t windowMask = ResolveWindowMask(windows, tx, ty);
                    const bool obj_enabled_in_window = ((windowMask >> 4) & 0x1) != 0;
                    const bool effects_enabled = ((windowMask >> 5) & 0x1) != 0;
                    if (!obj_enabled_in_window) continue;

                    if (have_vram && have_palette) {
                        // Decode actual OBJ tile pixel from VRAM
                        // NDS uses 1D OBJ mapping (common setting):
                        // tile address = base + tile_index * (32 or 64) + in-tile offset
                        int tiles_w = sw / 8;
                        int tile_col = src_x / 8;
                        int tile_row = src_y / 8;
                        int in_tile_x = src_x % 8;
                        int in_tile_y = src_y % 8;

                        uint32_t tile_num = sprite.tile_index + tile_row * tiles_w + tile_col;
                        uint32_t tile_addr = obj_vram_base +
                            tile_num * static_cast<uint32_t>(tile_bytes) +
                            in_tile_y * (sprite.is_8bpp ? 8 : 4);

                        int color_idx;
                        if (sprite.is_8bpp) {
                            uint32_t byte_addr = tile_addr + in_tile_x;
                            if (byte_addr >= vram_size) continue;
                            color_idx = vram_data[byte_addr];
                        } else {
                            uint32_t byte_addr = tile_addr + in_tile_x / 2;
                            if (byte_addr >= vram_size) continue;
                            uint8_t byte_val = vram_data[byte_addr];
                            color_idx = (in_tile_x & 1) ?
                                ((byte_val >> 4) & 0x0F) : (byte_val & 0x0F);
                        }

                        if (color_idx == 0) continue;  // Transparent

                        // OBJ palette starts at offset 0x200 in palette RAM
                        uint32_t pal_addr;
                        if (sprite.is_8bpp) {
                            pal_addr = 0x200 + static_cast<uint32_t>(color_idx) * 2;
                        } else {
                            pal_addr = 0x200 +
                                (static_cast<uint32_t>(sprite.palette) * 16 + color_idx) * 2;
                        }
                        if (pal_addr + 2 > palette_size) continue;

                        uint16_t rgb555 =
                            static_cast<uint16_t>(palette_data[pal_addr]) |
                            (static_cast<uint16_t>(palette_data[pal_addr + 1]) << 8);
                        uint32_t r = ((rgb555 >> 0)  & 0x1F) * 255 / 31;
                        uint32_t g = ((rgb555 >> 5)  & 0x1F) * 255 / 31;
                        uint32_t b = ((rgb555 >> 10) & 0x1F) * 255 / 31;

                        uint32_t src_color = (r << 24) | (g << 16) | (b << 8) | 0xFF;
                        uint32_t& dst = target_pixels[ty * 256 + tx];
                        dst = BlendPixel(dst, src_color, blend, effects_enabled);
                    } else {
                        // Fallback pattern when no VRAM data
                        const bool opaque_pixel = ((src_x + src_y + sprite.tile_index) & 0x3) != 0;
                        if (!opaque_pixel) continue;

                        const uint32_t src = PackRGBA(
                            180 + (sprite.tile_index % 60),
                            ((sprite.tile_index * 13) + src_x * 5) & 0xFF,
                            ((sprite.tile_index * 7) + src_y * 9) & 0xFF,
                            0xFF);
                        uint32_t& dst = target_pixels[ty * 256 + tx];
                        dst = BlendPixel(dst, src, blend, effects_enabled);
                    }
                }
            }
        }
    }
}

void SoftwareRenderer::CopyFramebuffers(std::array<uint32_t, 256 * 192>& top,
                                    std::array<uint32_t, 256 * 192>& bottom) {
    std::lock_guard<std::mutex> lock(m_render_mutex);
    std::copy(std::begin(m_pixels_top), std::end(m_pixels_top), top.begin());
    std::copy(std::begin(m_pixels_bottom), std::end(m_pixels_bottom), bottom.begin());
}

void SoftwareRenderer::SetFramebuffers(const uint32_t* top, const uint32_t* bottom) {
    std::lock_guard<std::mutex> lock(m_render_mutex);
    if (top) {
        std::memcpy(m_pixels_top, top, sizeof(m_pixels_top));
    }
    if (bottom) {
        std::memcpy(m_pixels_bottom, bottom, sizeof(m_pixels_bottom));
    }
}
