#pragma once

// ============================================================================
// hw_2d_engine.h — Nintendo DS 2D Graphics Engine Emulation
//
// The DS has two 2D engines (A and B), each with 4 BG layers, 128 OAM
// sprites, alpha blending, window masking, and VRAM bank routing. This
// module intercepts hardware register writes, parses OAM/BG state, decodes
// tiles and palettes, and exposes per-frame 2D render data for a modern GPU.
//
// Reference: GBATEK §DS 2D Engines, §DS Video, §DS VRAM
//   Engine A regs: 0x04000000 - 0x0400006F
//   Engine B regs: 0x04001000 - 0x0400106F
//   OAM:           0x07000000 (A), 0x07000400 (B)
//   Palette RAM:   0x05000000 (A), 0x05000200 (B) for BG
//                  0x05000200 (A), 0x05000600 (B) for OBJ
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <array>
#include <vector>

// ============================================================================
// VRAM Bank Mapping Targets
// ============================================================================
enum class VRAMMapping : uint8_t {
    Unmapped = 0,
    LCDC,            // Direct CPU access
    EngineA_BG,      // Engine A background VRAM
    EngineA_OBJ,     // Engine A sprite/OBJ VRAM
    EngineB_BG,      // Engine B background VRAM
    EngineB_OBJ,     // Engine B sprite/OBJ VRAM
    Texture,         // 3D texture slot
    TexturePalette,  // 3D texture palette
    ExtPalette_BG_A, // Engine A extended BG palette
    ExtPalette_OBJ_A,// Engine A extended OBJ palette
    ExtPalette_BG_B, // Engine B extended BG palette
    ExtPalette_OBJ_B // Engine B extended OBJ palette
};

// ============================================================================
// VRAM Bank Configuration (one per bank A–I)
// ============================================================================
struct VRAMBankConfig {
    bool        enabled = false;
    uint8_t     mst = 0;     // Memory Select Type (bits 0-2)
    uint8_t     ofs = 0;     // Offset (bits 3-4)
    VRAMMapping mapping = VRAMMapping::Unmapped;
    uint32_t    size = 0;    // Bank size in bytes

    void Decode(uint8_t vramcnt_val, int bank_index);
};

// ============================================================================
// VRAM Bank Controller — Manages all 9 banks (A-I)
// ============================================================================
struct VRAMController {
    static constexpr uint32_t BANK_SIZES[9] = {
        128*1024, 128*1024, 128*1024, 128*1024,  // A-D
        64*1024, 16*1024, 16*1024, 32*1024, 16*1024  // E-I
    };

    VRAMBankConfig banks[9];  // A=0 .. I=8

    void WriteControl(int bank_index, uint8_t value);
    VRAMMapping GetBankMapping(int bank_index) const;
    bool IsBankEnabled(int bank_index) const;

    // Query which banks are mapped to a given target
    uint16_t GetBanksMappedTo(VRAMMapping target) const;
};

// ============================================================================
// OAM Object Entry — Parsed from raw 8-byte OAM data
//
// Layout per object (8 bytes):
//   Attr0 (u16): Y, RotScale, ObjDisable/DoubleSize, Mode, Mosaic, Bpp, Shape
//   Attr1 (u16): X, RotScaleParam/HFlip/VFlip, Size
//   Attr2 (u16): TileIndex, Priority, PaletteNum
// ============================================================================
struct OAMEntry {
    // Decoded fields
    int16_t  x = 0;          // X position (9-bit signed)
    uint8_t  y = 0;          // Y position (8-bit)
    uint16_t tile_index = 0; // Character/tile number (10 bits)
    uint8_t  priority = 0;   // Display priority (0 = highest)
    uint8_t  palette = 0;    // Palette number (4bpp: 0-15)
    uint8_t  shape = 0;      // 0=square, 1=horizontal, 2=vertical
    uint8_t  size_bits = 0;  // Size selector (0-3)
    bool     hflip = false;
    bool     vflip = false;
    bool     is_8bpp = false; // false=4bpp, true=8bpp
    bool     mosaic = false;
    bool     rot_scale = false;
    bool     double_size = false;
    bool     disabled = false; // When rot_scale=false and bit 9 of attr0 is set
    uint8_t  rot_param = 0;   // Rotation/scaling parameter group (0-31)
    uint8_t  obj_mode = 0;    // 0=normal, 1=semi-transparent, 2=obj window, 3=bitmap

    // Decoded pixel dimensions
    int width = 8, height = 8;

    void ParseFromRaw(const uint8_t* oam_ptr);
    static void GetObjDimensions(uint8_t shape, uint8_t size, int& w, int& h);
};

// ============================================================================
// OAM Affine Parameters — 4 parameters per group, 32 groups
// Stored interleaved in OAM: every 4th OAM entry's attr1/attr3 words
// ============================================================================
struct OAMAffineParams {
    int16_t pa = 0x100; // 8.8 fixed-point, default = 1.0
    int16_t pb = 0;
    int16_t pc = 0;
    int16_t pd = 0x100;

    void ParseFromRaw(const uint8_t* oam_base, int group);
};

// ============================================================================
// Background Control (BGxCNT register, 16-bit)
// ============================================================================
struct BGControl {
    uint8_t  priority = 0;       // 0-3 (lower = higher priority)
    uint8_t  char_base = 0;      // Character base block (tile data offset)
    bool     mosaic = false;
    bool     is_8bpp = false;    // false=4bpp, true=8bpp (text mode)
    uint8_t  screen_base = 0;    // Screen base block (tilemap offset)
    bool     wrap = false;       // Affine BG wrap-around
    uint8_t  screen_size = 0;    // Size selector (0-3)

    // Decoded pixel dimensions for text mode
    int map_width = 256, map_height = 256;

    void Decode(uint16_t bgcnt);
};

// ============================================================================
// Background Affine Transform Parameters
// ============================================================================
struct BGAffine {
    int16_t  pa = 0x100;  // 8.8 fixed-point
    int16_t  pb = 0;
    int16_t  pc = 0;
    int16_t  pd = 0x100;
    int32_t  ref_x = 0;  // 20.8 fixed-point (reference point X)
    int32_t  ref_y = 0;  // 20.8 fixed-point (reference point Y)
};

// ============================================================================
// Blend / Color Effect Control
// ============================================================================
struct BlendControl {
    uint8_t  mode = 0;      // 0=off, 1=alpha, 2=brighten, 3=darken
    uint8_t  target1 = 0;   // First target bits (BG0-3, OBJ, BD)
    uint8_t  target2 = 0;   // Second target bits
    uint8_t  eva = 0;       // EVA coefficient (0-16, first target weight)
    uint8_t  evb = 0;       // EVB coefficient (0-16, second target weight)
    uint8_t  evy = 0;       // EVY coefficient (0-16, brightness delta)

    void DecodeBLDCNT(uint16_t val);
    void DecodeBLDALPHA(uint16_t val);
    void DecodeBLDY(uint16_t val);
};

// ============================================================================
// Window Control
// ============================================================================
struct WindowControl {
    // Window dimensions (H: x1/x2, V: y1/y2)
    uint8_t win0_x1 = 0, win0_x2 = 0, win0_y1 = 0, win0_y2 = 0;
    uint8_t win1_x1 = 0, win1_x2 = 0, win1_y1 = 0, win1_y2 = 0;

    // Per-window layer enable (bits: BG0,BG1,BG2,BG3,OBJ,effects)
    uint8_t winin_win0 = 0x3F;   // Default: all enabled
    uint8_t winin_win1 = 0x3F;
    uint8_t winout_outside = 0x3F;
    uint8_t winout_objwin = 0x3F;

    void DecodeWINH(int win, uint16_t val);
    void DecodeWINV(int win, uint16_t val);
    void DecodeWININ(uint16_t val);
    void DecodeWINOUT(uint16_t val);
};

// ============================================================================
// Tile Extraction Utilities
// ============================================================================
namespace TileDecoder {
    // Decode a single 8x8 tile from VRAM (4bpp = 32 bytes, 8bpp = 64 bytes)
    // Output: 64 palette indices
    void DecodeTile4bpp(const uint8_t* tile_data, uint8_t* out_indices);
    void DecodeTile8bpp(const uint8_t* tile_data, uint8_t* out_indices);

    // Convert a palette index to RGBA32 using palette RAM
    // DS palette entry is RGB555 (16-bit), index 0 is transparent
    uint32_t PaletteToRGBA32(uint16_t rgb555);

    // Decode a full tile to RGBA32 pixels (8x8 = 64 pixels)
    void DecodeTileToRGBA(const uint8_t* tile_data, const uint8_t* palette_ram,
                          int palette_num, bool is_8bpp, uint32_t* out_rgba);
}

// ============================================================================
// 2D Sprite/Quad — Ready for GPU rendering
// ============================================================================
struct Sprite2D {
    float x, y;               // Screen position
    float width, height;      // Pixel dimensions
    uint16_t tile_index;      // VRAM tile reference
    uint8_t  priority;        // Layer priority
    uint8_t  palette;
    bool     is_8bpp;
    bool     hflip, vflip;
    uint8_t  obj_mode;        // Normal, semi-transparent, obj-window, bitmap
    bool     has_affine;
    int16_t  affine_pa, affine_pb, affine_pc, affine_pd;
};

// ============================================================================
// 2D Background Layer — Ready for GPU rendering
// ============================================================================
struct BGLayer2D {
    bool     enabled = false;
    uint8_t  mode = 0;        // 0=text, 1=affine, 2=extended
    uint8_t  priority = 0;
    uint16_t bgcnt_raw = 0;
    int16_t  scroll_x = 0, scroll_y = 0;
    BGAffine affine;
    BGControl control;
};

// ============================================================================
// Abstract 2D Rendering Backend
// ============================================================================
class Renderer2D {
public:
    virtual ~Renderer2D() = default;
    virtual void SubmitFrame2D(const std::vector<Sprite2D>& sprites,
                               const std::array<BGLayer2D, 4>& bg_layers,
                               const BlendControl& blend,
                               const WindowControl& windows,
                               const uint8_t* vram_data,
                               size_t vram_size,
                               const uint8_t* palette_data,
                               size_t palette_size,
                               bool render_to_top,
                               uint32_t dispcnt) = 0;
};

// ============================================================================
// NDS 2D Engine — One instance per engine (A and B)
// ============================================================================
class NDS2DEngine {
public:
    // ---- Display Control (DISPCNT) ----
    uint32_t dispcnt = 0;

    // ---- Background Layers ----
    std::array<BGLayer2D, 4> bg_layers;

    // ---- Blending ----
    BlendControl blend;

    // ---- Windows ----
    WindowControl windows;

    // ---- Mosaic ----
    uint8_t mosaic_bg_h = 0, mosaic_bg_v = 0;
    uint8_t mosaic_obj_h = 0, mosaic_obj_v = 0;

    // ---- Master Brightness ----
    uint16_t master_bright = 0;

    // ---- Rendering Backend ----
    Renderer2D* renderer = nullptr;
    bool is_sub_engine = false;

    NDS2DEngine();

    // ---- Register IO ----
    void WriteRegister(uint32_t offset, uint32_t value);
    uint32_t ReadRegister(uint32_t offset) const;

    // ---- OAM Parsing ----
    // Parse all 128 objects from raw OAM memory
    void ParseOAM(const uint8_t* oam_data, std::vector<Sprite2D>& out_sprites) const;

    // ---- Per-Frame Submission ----
    void SubmitFrame(const uint8_t* oam_data,
                     const uint8_t* vram_data,
                     size_t vram_size,
                     const uint8_t* palette_data,
                     size_t palette_size);

    void SetSubEngine(bool is_sub) { is_sub_engine = is_sub; }

    // ---- Decoded State Queries ----
    uint8_t GetBGMode() const { return dispcnt & 0x7; }
    bool IsBGEnabled(int bg) const { return (dispcnt >> (8 + bg)) & 1; }
    bool IsOBJEnabled() const { return (dispcnt >> 12) & 1; }
    bool IsWin0Enabled() const { return (dispcnt >> 13) & 1; }
    bool IsWin1Enabled() const { return (dispcnt >> 14) & 1; }
    bool IsOBJWinEnabled() const { return (dispcnt >> 15) & 1; }

    // --- Phase 4 State Handlers ---
    void RenderMainBackgrounds();
    void RenderSubBackgrounds();
    void RenderMainSprites();
    void RenderSubSprites();
    void CalculateAffineTransform();
    void BlendAlphaLayers();

private:
    void UpdateBGState(int bg);
};
