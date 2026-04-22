#include "hw_2d_engine.h"

#include <algorithm>
#include <atomic>

std::atomic<uint64_t> g_debug_2d_submit_count{0};
std::atomic<uint32_t> g_debug_2d_last_sprite_count{0};

// ============================================================================
// hw_2d_engine.cpp — Nintendo DS 2D Graphics Engine Implementation
// ============================================================================

// ============================================================================
// VRAM Bank Configuration Decode
// ============================================================================
void VRAMBankConfig::Decode(uint8_t val, int bank_index) {
    enabled = (val >> 7) & 1;
    mst = val & 0x7;
    ofs = (val >> 3) & 0x3;
    size = VRAMController::BANK_SIZES[bank_index];

    if (!enabled) { mapping = VRAMMapping::Unmapped; return; }

    // Per GBATEK: MST/OFS mapping depends on bank index
    // Banks A-D (128KB each)
    if (bank_index <= 3) {
        switch (mst) {
            case 0: mapping = VRAMMapping::LCDC; break;
            case 1: mapping = VRAMMapping::EngineA_BG; break;
            case 2: mapping = VRAMMapping::EngineA_OBJ; break;
            case 3: mapping = VRAMMapping::Texture; break;
            case 4: mapping = VRAMMapping::EngineB_BG; break;
            default: mapping = VRAMMapping::LCDC; break;
        }
    }
    // Bank E (64KB)
    else if (bank_index == 4) {
        switch (mst) {
            case 0: mapping = VRAMMapping::LCDC; break;
            case 1: mapping = VRAMMapping::EngineA_BG; break;
            case 2: mapping = VRAMMapping::EngineA_OBJ; break;
            case 3: mapping = VRAMMapping::TexturePalette; break;
            case 4: mapping = VRAMMapping::ExtPalette_BG_A; break;
            default: mapping = VRAMMapping::LCDC; break;
        }
    }
    // Banks F-G (16KB each)
    else if (bank_index == 5 || bank_index == 6) {
        switch (mst) {
            case 0: mapping = VRAMMapping::LCDC; break;
            case 1: mapping = VRAMMapping::EngineA_BG; break;
            case 2: mapping = VRAMMapping::EngineA_OBJ; break;
            case 3: mapping = VRAMMapping::TexturePalette; break;
            case 4: mapping = VRAMMapping::ExtPalette_BG_A; break;
            case 5: mapping = VRAMMapping::ExtPalette_OBJ_A; break;
            default: mapping = VRAMMapping::LCDC; break;
        }
    }
    // Bank H (32KB)
    else if (bank_index == 7) {
        switch (mst) {
            case 0: mapping = VRAMMapping::LCDC; break;
            case 1: mapping = VRAMMapping::EngineB_BG; break;
            case 2: mapping = VRAMMapping::ExtPalette_BG_B; break;
            default: mapping = VRAMMapping::LCDC; break;
        }
    }
    // Bank I (16KB)
    else if (bank_index == 8) {
        switch (mst) {
            case 0: mapping = VRAMMapping::LCDC; break;
            case 1: mapping = VRAMMapping::EngineB_BG; break;
            case 2: mapping = VRAMMapping::EngineB_OBJ; break;
            case 3: mapping = VRAMMapping::ExtPalette_OBJ_B; break;
            default: mapping = VRAMMapping::LCDC; break;
        }
    }
}

void VRAMController::WriteControl(int bank_index, uint8_t value) {
    if (bank_index < 0 || bank_index >= 9) return;
    banks[bank_index].Decode(value, bank_index);
}

VRAMMapping VRAMController::GetBankMapping(int bank_index) const {
    if (bank_index < 0 || bank_index >= 9) return VRAMMapping::Unmapped;
    return banks[bank_index].mapping;
}

bool VRAMController::IsBankEnabled(int bank_index) const {
    if (bank_index < 0 || bank_index >= 9) return false;
    return banks[bank_index].enabled;
}

uint16_t VRAMController::GetBanksMappedTo(VRAMMapping target) const {
    uint16_t mask = 0;
    for (int i = 0; i < 9; ++i) {
        if (banks[i].enabled && banks[i].mapping == target)
            mask |= (1 << i);
    }
    return mask;
}

// ============================================================================
// OAM Entry Parsing
// ============================================================================
void OAMEntry::ParseFromRaw(const uint8_t* oam_ptr) {
    uint16_t attr0 = oam_ptr[0] | (oam_ptr[1] << 8);
    uint16_t attr1 = oam_ptr[2] | (oam_ptr[3] << 8);
    uint16_t attr2 = oam_ptr[4] | (oam_ptr[5] << 8);

    y         = attr0 & 0xFF;
    rot_scale = (attr0 >> 8) & 1;
    obj_mode  = (attr0 >> 10) & 0x3;
    mosaic    = (attr0 >> 12) & 1;
    is_8bpp   = (attr0 >> 13) & 1;
    shape     = (attr0 >> 14) & 0x3;

    if (rot_scale) {
        double_size = (attr0 >> 9) & 1;
        disabled = false;
    } else {
        double_size = false;
        disabled = (attr0 >> 9) & 1;
    }

    // X is 9-bit signed
    int raw_x = attr1 & 0x1FF;
    x = (raw_x >= 256) ? static_cast<int16_t>(raw_x - 512) : static_cast<int16_t>(raw_x);

    size_bits = (attr1 >> 14) & 0x3;

    if (rot_scale) {
        rot_param = (attr1 >> 9) & 0x1F;
        hflip = false;
        vflip = false;
    } else {
        rot_param = 0;
        hflip = (attr1 >> 12) & 1;
        vflip = (attr1 >> 13) & 1;
    }

    tile_index = attr2 & 0x03FF;
    priority   = (attr2 >> 10) & 0x3;
    palette    = (attr2 >> 12) & 0xF;

    GetObjDimensions(shape, size_bits, width, height);
}

// Per GBATEK: OBJ shape×size → pixel dimensions
void OAMEntry::GetObjDimensions(uint8_t shape, uint8_t size, int& w, int& h) {
    // shape: 0=square, 1=horizontal, 2=vertical
    static const int dims[3][4][2] = {
        // Square: 8x8, 16x16, 32x32, 64x64
        {{8,8}, {16,16}, {32,32}, {64,64}},
        // Horizontal: 16x8, 32x8, 32x16, 64x32
        {{16,8}, {32,8}, {32,16}, {64,32}},
        // Vertical: 8x16, 8x32, 16x32, 32x64
        {{8,16}, {8,32}, {16,32}, {32,64}}
    };
    if (shape < 3 && size < 4) {
        w = dims[shape][size][0];
        h = dims[shape][size][1];
    } else {
        w = 8; h = 8;
    }
}

// ============================================================================
// OAM Affine Parameters
// ============================================================================
void OAMAffineParams::ParseFromRaw(const uint8_t* oam_base, int group) {
    // Affine params are stored interleaved in OAM at bytes:
    // PA = OAM[group*32 + 6..7]   (attr3 of obj group*4+0)
    // PB = OAM[group*32 + 14..15] (attr3 of obj group*4+1)
    // PC = OAM[group*32 + 22..23] (attr3 of obj group*4+2)
    // PD = OAM[group*32 + 30..31] (attr3 of obj group*4+3)
    int base = group * 32;
    pa = static_cast<int16_t>(oam_base[base + 6]  | (oam_base[base + 7]  << 8));
    pb = static_cast<int16_t>(oam_base[base + 14] | (oam_base[base + 15] << 8));
    pc = static_cast<int16_t>(oam_base[base + 22] | (oam_base[base + 23] << 8));
    pd = static_cast<int16_t>(oam_base[base + 30] | (oam_base[base + 31] << 8));
}

// ============================================================================
// Background Control Decode
// ============================================================================
void BGControl::Decode(uint16_t bgcnt) {
    priority    = bgcnt & 0x3;
    char_base   = (bgcnt >> 2) & 0xF;
    mosaic      = (bgcnt >> 6) & 1;
    is_8bpp     = (bgcnt >> 7) & 1;
    screen_base = (bgcnt >> 8) & 0x1F;
    wrap        = (bgcnt >> 13) & 1;
    screen_size = (bgcnt >> 14) & 0x3;

    // Text mode screen sizes
    static const int sizes[4][2] = {{256,256},{512,256},{256,512},{512,512}};
    map_width  = sizes[screen_size][0];
    map_height = sizes[screen_size][1];
}

// ============================================================================
// Blend Control Decode
// ============================================================================
void BlendControl::DecodeBLDCNT(uint16_t val) {
    target1 = val & 0x3F;
    mode    = (val >> 6) & 0x3;
    target2 = (val >> 8) & 0x3F;
}

void BlendControl::DecodeBLDALPHA(uint16_t val) {
    eva = val & 0x1F;        if (eva > 16) eva = 16;
    evb = (val >> 8) & 0x1F; if (evb > 16) evb = 16;
}

void BlendControl::DecodeBLDY(uint16_t val) {
    evy = val & 0x1F; if (evy > 16) evy = 16;
}

// ============================================================================
// Window Control Decode
// ============================================================================
void WindowControl::DecodeWINH(int win, uint16_t val) {
    uint8_t x2 = val & 0xFF;
    uint8_t x1 = (val >> 8) & 0xFF;
    if (win == 0) { win0_x1 = x1; win0_x2 = x2; }
    else          { win1_x1 = x1; win1_x2 = x2; }
}

void WindowControl::DecodeWINV(int win, uint16_t val) {
    uint8_t y2 = val & 0xFF;
    uint8_t y1 = (val >> 8) & 0xFF;
    if (win == 0) { win0_y1 = y1; win0_y2 = y2; }
    else          { win1_y1 = y1; win1_y2 = y2; }
}

void WindowControl::DecodeWININ(uint16_t val) {
    winin_win0 = val & 0x3F;
    winin_win1 = (val >> 8) & 0x3F;
}

void WindowControl::DecodeWINOUT(uint16_t val) {
    winout_outside = val & 0x3F;
    winout_objwin  = (val >> 8) & 0x3F;
}

// ============================================================================
// Tile Decoding
// ============================================================================
void TileDecoder::DecodeTile4bpp(const uint8_t* tile_data, uint8_t* out_indices) {
    // 4bpp: 32 bytes per tile, 2 pixels per byte (lo nibble first)
    for (int i = 0; i < 32; ++i) {
        out_indices[i * 2 + 0] = tile_data[i] & 0x0F;
        out_indices[i * 2 + 1] = (tile_data[i] >> 4) & 0x0F;
    }
}

void TileDecoder::DecodeTile8bpp(const uint8_t* tile_data, uint8_t* out_indices) {
    // 8bpp: 64 bytes per tile, 1 pixel per byte
    std::memcpy(out_indices, tile_data, 64);
}

uint32_t TileDecoder::PaletteToRGBA32(uint16_t rgb555) {
    uint8_t r = ((rgb555 >> 0)  & 0x1F) * 255 / 31;
    uint8_t g = ((rgb555 >> 5)  & 0x1F) * 255 / 31;
    uint8_t b = ((rgb555 >> 10) & 0x1F) * 255 / 31;
    return (0xFF << 24) | (b << 16) | (g << 8) | r; // ABGR32
}

void TileDecoder::DecodeTileToRGBA(const uint8_t* tile_data,
                                    const uint8_t* palette_ram,
                                    int palette_num, bool is_8bpp,
                                    uint32_t* out_rgba) {
    uint8_t indices[64];

    if (is_8bpp) {
        DecodeTile8bpp(tile_data, indices);
        for (int i = 0; i < 64; ++i) {
            if (indices[i] == 0) {
                out_rgba[i] = 0; // Transparent
            } else {
                uint16_t color = palette_ram[indices[i] * 2] |
                                (palette_ram[indices[i] * 2 + 1] << 8);
                out_rgba[i] = PaletteToRGBA32(color);
            }
        }
    } else {
        DecodeTile4bpp(tile_data, indices);
        int pal_offset = palette_num * 32; // 16 colors × 2 bytes each
        for (int i = 0; i < 64; ++i) {
            if (indices[i] == 0) {
                out_rgba[i] = 0;
            } else {
                int addr = pal_offset + indices[i] * 2;
                uint16_t color = palette_ram[addr] | (palette_ram[addr + 1] << 8);
                out_rgba[i] = PaletteToRGBA32(color);
            }
        }
    }
}

// ============================================================================
// NDS 2D Engine
// ============================================================================
NDS2DEngine::NDS2DEngine() {
    for (int i = 0; i < 4; ++i) {
        bg_layers[i] = BGLayer2D{};
    }
}

void NDS2DEngine::WriteRegister(uint32_t offset, uint32_t value) {
    switch (offset) {
        case 0x00: dispcnt = value; break; // DISPCNT

        // BGxCNT (8-bit or 16-bit)
        case 0x08: bg_layers[0].bgcnt_raw = value & 0xFFFF;
                   bg_layers[0].control.Decode(bg_layers[0].bgcnt_raw);
                   UpdateBGState(0); break;
        case 0x0A: bg_layers[1].bgcnt_raw = value & 0xFFFF;
                   bg_layers[1].control.Decode(bg_layers[1].bgcnt_raw);
                   UpdateBGState(1); break;
        case 0x0C: bg_layers[2].bgcnt_raw = value & 0xFFFF;
                   bg_layers[2].control.Decode(bg_layers[2].bgcnt_raw);
                   UpdateBGState(2); break;
        case 0x0E: bg_layers[3].bgcnt_raw = value & 0xFFFF;
                   bg_layers[3].control.Decode(bg_layers[3].bgcnt_raw);
                   UpdateBGState(3); break;

        // BG scroll offsets
        case 0x10: bg_layers[0].scroll_x = value & 0x1FF; break;
        case 0x12: bg_layers[0].scroll_y = value & 0x1FF; break;
        case 0x14: bg_layers[1].scroll_x = value & 0x1FF; break;
        case 0x16: bg_layers[1].scroll_y = value & 0x1FF; break;
        case 0x18: bg_layers[2].scroll_x = value & 0x1FF; break;
        case 0x1A: bg_layers[2].scroll_y = value & 0x1FF; break;
        case 0x1C: bg_layers[3].scroll_x = value & 0x1FF; break;
        case 0x1E: bg_layers[3].scroll_y = value & 0x1FF; break;

        // BG2 Affine
        case 0x20: bg_layers[2].affine.pa = static_cast<int16_t>(value); break;
        case 0x22: bg_layers[2].affine.pb = static_cast<int16_t>(value); break;
        case 0x24: bg_layers[2].affine.pc = static_cast<int16_t>(value); break;
        case 0x26: bg_layers[2].affine.pd = static_cast<int16_t>(value); break;
        case 0x28: bg_layers[2].affine.ref_x = static_cast<int32_t>(value); break;
        case 0x2C: bg_layers[2].affine.ref_y = static_cast<int32_t>(value); break;

        // BG3 Affine
        case 0x30: bg_layers[3].affine.pa = static_cast<int16_t>(value); break;
        case 0x32: bg_layers[3].affine.pb = static_cast<int16_t>(value); break;
        case 0x34: bg_layers[3].affine.pc = static_cast<int16_t>(value); break;
        case 0x36: bg_layers[3].affine.pd = static_cast<int16_t>(value); break;
        case 0x38: bg_layers[3].affine.ref_x = static_cast<int32_t>(value); break;
        case 0x3C: bg_layers[3].affine.ref_y = static_cast<int32_t>(value); break;

        // Window
        case 0x40: windows.DecodeWINH(0, value & 0xFFFF); break;
        case 0x42: windows.DecodeWINH(1, value & 0xFFFF); break;
        case 0x44: windows.DecodeWINV(0, value & 0xFFFF); break;
        case 0x46: windows.DecodeWINV(1, value & 0xFFFF); break;
        case 0x48: windows.DecodeWININ(value & 0xFFFF); break;
        case 0x4A: windows.DecodeWINOUT(value & 0xFFFF); break;

        // Mosaic
        case 0x4C:
            mosaic_bg_h  = value & 0xF;
            mosaic_bg_v  = (value >> 4) & 0xF;
            mosaic_obj_h = (value >> 8) & 0xF;
            mosaic_obj_v = (value >> 12) & 0xF;
            break;

        // Blend
        case 0x50: blend.DecodeBLDCNT(value & 0xFFFF); break;
        case 0x52: blend.DecodeBLDALPHA(value & 0xFFFF); break;
        case 0x54: blend.DecodeBLDY(value & 0xFFFF); break;

        // Master Brightness
        case 0x6C: master_bright = value & 0xFFFF; break;

        default: break;
    }
}

uint32_t NDS2DEngine::ReadRegister(uint32_t offset) const {
    switch (offset) {
        case 0x00: return dispcnt;
        case 0x06: { static uint16_t fake_vcount = 0; fake_vcount = (fake_vcount + 1) % 263; return fake_vcount; }
        case 0x08: return bg_layers[0].bgcnt_raw;
        case 0x0A: return bg_layers[1].bgcnt_raw;
        case 0x0C: return bg_layers[2].bgcnt_raw;
        case 0x0E: return bg_layers[3].bgcnt_raw;
        case 0x50: return (blend.mode << 6) | blend.target1 | (blend.target2 << 8);
        case 0x6C: return master_bright;
        default: return 0;
    }
}

void NDS2DEngine::UpdateBGState(int bg) {
    bg_layers[bg].enabled = IsBGEnabled(bg);
    bg_layers[bg].priority = bg_layers[bg].control.priority;
}

void NDS2DEngine::ParseOAM(const uint8_t* oam_data,
                             std::vector<Sprite2D>& out_sprites) const {
    out_sprites.clear();
    out_sprites.reserve(128);

    for (int i = 0; i < 128; ++i) {
        OAMEntry entry;
        entry.ParseFromRaw(oam_data + i * 8);

        if (entry.disabled) continue;

        Sprite2D spr;
        spr.x          = static_cast<float>(entry.x);
        spr.y          = static_cast<float>(entry.y);
        spr.width      = static_cast<float>(entry.width);
        spr.height     = static_cast<float>(entry.height);
        spr.tile_index = entry.tile_index;
        spr.priority   = entry.priority;
        spr.palette    = entry.palette;
        spr.is_8bpp    = entry.is_8bpp;
        spr.hflip      = entry.hflip;
        spr.vflip      = entry.vflip;
        spr.obj_mode   = entry.obj_mode;

        if (entry.rot_scale) {
            spr.has_affine = true;
            OAMAffineParams aff;
            aff.ParseFromRaw(oam_data, entry.rot_param);
            spr.affine_pa = aff.pa;
            spr.affine_pb = aff.pb;
            spr.affine_pc = aff.pc;
            spr.affine_pd = aff.pd;
        } else {
            spr.has_affine = false;
            spr.affine_pa = 0x100;
            spr.affine_pb = 0;
            spr.affine_pc = 0;
            spr.affine_pd = 0x100;
        }

        out_sprites.push_back(spr);
    }
}

void NDS2DEngine::SubmitFrame(const uint8_t* oam_data,
                              const uint8_t* vram_data,
                              size_t vram_size,
                              const uint8_t* palette_data,
                              size_t palette_size) {
    if (!renderer) return;

    // Phase 4: Hardware evaluation before rasterization submission
    CalculateAffineTransform();
    BlendAlphaLayers();
    RenderMainBackgrounds();
    RenderSubBackgrounds();
    RenderMainSprites();
    RenderSubSprites();

    std::vector<Sprite2D> sprites;
    ParseOAM(oam_data, sprites);
    g_debug_2d_last_sprite_count.store(static_cast<uint32_t>(sprites.size()), std::memory_order_relaxed);
    g_debug_2d_submit_count.fetch_add(1, std::memory_order_relaxed);
    renderer->SubmitFrame2D(sprites,
                            bg_layers,
                            blend,
                            windows,
                            vram_data,
                            vram_size,
                            palette_data,
                            palette_size,
                            !is_sub_engine,
                            dispcnt);
}



// ============================================================================
// Phase 4: Graphics Engine Hardware Evaluation
// ============================================================================

void NDS2DEngine::RenderMainBackgrounds() {
    const uint8_t mode = GetBGMode() & 0x7;

    for (int i = 0; i < 4; ++i) {
        UpdateBGState(i);
        bg_layers[i].control.Decode(bg_layers[i].bgcnt_raw);

        // Engine A mode mapping: BG2/BG3 can become affine in higher modes.
        const bool affine_capable = (i >= 2) && (mode >= 1 && mode <= 5);
        bg_layers[i].mode = affine_capable ? 1 : 0;

        // If not affine-capable, enforce identity affine matrix to avoid stale state.
        if (!affine_capable) {
            bg_layers[i].affine.pa = 0x0100;
            bg_layers[i].affine.pb = 0;
            bg_layers[i].affine.pc = 0;
            bg_layers[i].affine.pd = 0x0100;
        }
    }
}

void NDS2DEngine::RenderSubBackgrounds() {
    // Sub engine generally runs text/affine limited modes.
    uint8_t mode = GetBGMode() & 0x7;
    if (mode > 1) mode = 0;

    for (int i = 0; i < 4; ++i) {
        UpdateBGState(i);
        bg_layers[i].control.Decode(bg_layers[i].bgcnt_raw);

        const bool affine_capable = (i >= 2) && (mode == 1);
        bg_layers[i].mode = affine_capable ? 1 : 0;

        if (!affine_capable) {
            bg_layers[i].affine.pa = 0x0100;
            bg_layers[i].affine.pb = 0;
            bg_layers[i].affine.pc = 0;
            bg_layers[i].affine.pd = 0x0100;
        }
    }
}

void NDS2DEngine::RenderMainSprites() {
    if (!IsOBJEnabled()) {
        // If OBJ is disabled globally, remove OBJ from blend targets.
        blend.target1 &= static_cast<uint8_t>(~(1u << 4));
        blend.target2 &= static_cast<uint8_t>(~(1u << 4));
    }
}

void NDS2DEngine::RenderSubSprites() {
    if (!IsOBJEnabled()) {
        blend.target1 &= static_cast<uint8_t>(~(1u << 4));
        blend.target2 &= static_cast<uint8_t>(~(1u << 4));
    }
}

void NDS2DEngine::CalculateAffineTransform() {
    // BG2/BG3 are affine-capable. Clamp to hardware-sized signed coefficients and
    // prevent singular transforms from propagating into render passes.
    for (int i = 2; i <= 3; ++i) {
        if (!bg_layers[i].enabled) continue;

        int32_t pa = std::clamp<int32_t>(bg_layers[i].affine.pa, -0x8000, 0x7FFF);
        int32_t pb = std::clamp<int32_t>(bg_layers[i].affine.pb, -0x8000, 0x7FFF);
        int32_t pc = std::clamp<int32_t>(bg_layers[i].affine.pc, -0x8000, 0x7FFF);
        int32_t pd = std::clamp<int32_t>(bg_layers[i].affine.pd, -0x8000, 0x7FFF);

        int64_t det = static_cast<int64_t>(pa) * pd - static_cast<int64_t>(pb) * pc;

        if (det == 0) {
            // Fall back to identity instead of collapsing the layer.
            pa = 0x0100;
            pb = 0;
            pc = 0;
            pd = 0x0100;
        } else {
            // Keep determinant in a stable numeric range.
            while (det > (1LL << 24) || det < -(1LL << 24)) {
                pa >>= 1;
                pb >>= 1;
                pc >>= 1;
                pd >>= 1;
                det = static_cast<int64_t>(pa) * pd - static_cast<int64_t>(pb) * pc;
                if (det == 0) {
                    pa = 0x0100;
                    pb = 0;
                    pc = 0;
                    pd = 0x0100;
                    break;
                }
            }
        }

        bg_layers[i].affine.pa = static_cast<int16_t>(pa);
        bg_layers[i].affine.pb = static_cast<int16_t>(pb);
        bg_layers[i].affine.pc = static_cast<int16_t>(pc);
        bg_layers[i].affine.pd = static_cast<int16_t>(pd);

        // Keep reference points in signed 20.8-compatible range.
        bg_layers[i].affine.ref_x = std::clamp<int32_t>(bg_layers[i].affine.ref_x, -0x7FFFFFFF, 0x7FFFFFFF);
        bg_layers[i].affine.ref_y = std::clamp<int32_t>(bg_layers[i].affine.ref_y, -0x7FFFFFFF, 0x7FFFFFFF);
    }
}

void NDS2DEngine::BlendAlphaLayers() {
    // Keep blend state in hardware-valid limits.
    blend.target1 &= 0x3F;
    blend.target2 &= 0x3F;

    if (blend.mode == 0) {
        blend.eva = 16;
        blend.evb = 0;
        blend.evy = 0;
    } else if (blend.mode == 1) {
        // Alpha blending.
        if (blend.eva > 16) blend.eva = 16;
        if (blend.evb > 16) blend.evb = 16;
        blend.evy = 0;

        if (blend.target1 == 0 || blend.target2 == 0) {
            // No valid source/target pair: disable blend mode.
            blend.mode = 0;
            blend.eva = 16;
            blend.evb = 0;
        }
    } else if (blend.mode == 2 || blend.mode == 3) {
        // Brightness increase/decrease.
        blend.eva = 16;
        blend.evb = 0;
        if (blend.evy > 16) blend.evy = 16;

        // Brightness mode uses only target1.
        blend.target2 = 0;
    } else {
        blend.mode = 0;
        blend.eva = 16;
        blend.evb = 0;
        blend.evy = 0;
    }
}
