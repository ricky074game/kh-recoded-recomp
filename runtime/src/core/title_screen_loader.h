#pragma once
#include <cstdint>
#include <string>
#include <vector>

class SoftwareRenderer;

// Loads and renders the KH Re:coded title screen from the
// extracted P2 pack files directly into the software renderer
// framebuffers.
class TitleScreenLoader {
public:
    // Load title screen data from the ROM data directory.
    // Returns true if at least partial data was loaded.
    bool Load(const std::string& dataDir);

    // Render the title screen into the given renderer.
    // Call this after Load() succeeds.
    void Render(SoftwareRenderer* renderer);

    bool IsLoaded() const { return loaded_; }

private:
    bool loaded_ = false;

    // Decoded tile graphics (8bpp pixel indices)
    std::vector<uint8_t> tile_data_;
    int tile_bpp_ = 4;

    // Decoded palette (RGB555 -> expanded to RGB888)
    struct Color { uint8_t r, g, b; };
    std::vector<Color> palette_;

    // Decoded screen map entries
    struct TileMapEntry {
        uint16_t tile_idx;
        bool h_flip;
        bool v_flip;
        uint8_t pal_idx;
    };
    std::vector<TileMapEntry> tile_map_;
    int map_width_ = 256;
    int map_height_ = 192;

    // Bottom screen data (if available)
    std::vector<uint8_t> bottom_tile_data_;
    std::vector<Color> bottom_palette_;
    std::vector<TileMapEntry> bottom_tile_map_;
    int bottom_map_width_ = 256;
    int bottom_map_height_ = 192;
    bool has_bottom_ = false;

    // LZ11 decompression
    static std::vector<uint8_t> DecompressLZ11(const uint8_t* data, size_t size);

    // P2 pack file parser
    static std::vector<std::vector<uint8_t>> ParseP2(const std::string& path);

    // NCGR tile data decoder
    bool DecodeNCGR(const uint8_t* data, size_t size,
                    std::vector<uint8_t>& out_tiles, int& out_bpp);

    // NCLR palette decoder
    bool DecodeNCLR(const uint8_t* data, size_t size,
                    std::vector<Color>& out_palette);

    // KAPH (Square Enix custom container) palette decoder
    bool DecodeKAPHPalette(const uint8_t* data, size_t size,
                           std::vector<Color>& out_palette);

    // Render tiled BG to an RGBA pixel buffer
    void RenderTiledBG(uint32_t* pixels, int screen_w, int screen_h,
                       const std::vector<uint8_t>& tiles, int bpp,
                       const std::vector<Color>& palette,
                       const std::vector<TileMapEntry>& map,
                       int map_w, int map_h);
};
