#include <gtest/gtest.h>
#include "hw_2d_engine.h"
#include "memory_map.h"

// ============================================================================
// VRAM Bank Control Tests
// ============================================================================

TEST(VRAM, BankA_LCDC) {
    VRAMController ctrl;
    ctrl.WriteControl(0, 0x80); // Enable, MST=0 → LCDC
    EXPECT_TRUE(ctrl.IsBankEnabled(0));
    EXPECT_EQ(ctrl.GetBankMapping(0), VRAMMapping::LCDC);
}

TEST(VRAM, BankA_EngineA_BG) {
    VRAMController ctrl;
    ctrl.WriteControl(0, 0x81); // Enable, MST=1 → Engine A BG
    EXPECT_EQ(ctrl.GetBankMapping(0), VRAMMapping::EngineA_BG);
}

TEST(VRAM, BankA_Texture) {
    VRAMController ctrl;
    ctrl.WriteControl(0, 0x83); // Enable, MST=3 → Texture
    EXPECT_EQ(ctrl.GetBankMapping(0), VRAMMapping::Texture);
}

TEST(VRAM, BankDisabled) {
    VRAMController ctrl;
    ctrl.WriteControl(0, 0x01); // MST=1 but Enable=0
    EXPECT_FALSE(ctrl.IsBankEnabled(0));
    EXPECT_EQ(ctrl.GetBankMapping(0), VRAMMapping::Unmapped);
}

TEST(VRAM, BankE_ExtPalette) {
    VRAMController ctrl;
    ctrl.WriteControl(4, 0x84); // Bank E, MST=4 → ExtPalette_BG_A
    EXPECT_EQ(ctrl.GetBankMapping(4), VRAMMapping::ExtPalette_BG_A);
}

TEST(VRAM, BankH_EngineB_BG) {
    VRAMController ctrl;
    ctrl.WriteControl(7, 0x81); // Bank H, MST=1 → Engine B BG
    EXPECT_EQ(ctrl.GetBankMapping(7), VRAMMapping::EngineB_BG);
}

TEST(VRAM, BankI_EngineB_OBJ) {
    VRAMController ctrl;
    ctrl.WriteControl(8, 0x82); // Bank I, MST=2 → Engine B OBJ
    EXPECT_EQ(ctrl.GetBankMapping(8), VRAMMapping::EngineB_OBJ);
}

TEST(VRAM, GetBanksMappedTo) {
    VRAMController ctrl;
    ctrl.WriteControl(0, 0x81); // A → Engine A BG
    ctrl.WriteControl(1, 0x81); // B → Engine A BG
    ctrl.WriteControl(2, 0x82); // C → Engine A OBJ
    uint16_t bg = ctrl.GetBanksMappedTo(VRAMMapping::EngineA_BG);
    EXPECT_EQ(bg, 0x0003); // Banks A and B
    uint16_t obj = ctrl.GetBanksMappedTo(VRAMMapping::EngineA_OBJ);
    EXPECT_EQ(obj, 0x0004); // Bank C
}

TEST(VRAM, BankSizes) {
    VRAMController ctrl;
    ctrl.WriteControl(0, 0x80); // A = 128KB
    ctrl.WriteControl(4, 0x80); // E = 64KB
    ctrl.WriteControl(5, 0x80); // F = 16KB
    ctrl.WriteControl(7, 0x80); // H = 32KB
    EXPECT_EQ(ctrl.banks[0].size, 128u * 1024);
    EXPECT_EQ(ctrl.banks[4].size, 64u * 1024);
    EXPECT_EQ(ctrl.banks[5].size, 16u * 1024);
    EXPECT_EQ(ctrl.banks[7].size, 32u * 1024);
}

// ============================================================================
// OAM Entry Parsing Tests
// ============================================================================

static void WriteOAMEntry(uint8_t* oam, int idx,
                           uint16_t a0, uint16_t a1, uint16_t a2) {
    int off = idx * 8;
    oam[off+0] = a0 & 0xFF; oam[off+1] = (a0 >> 8) & 0xFF;
    oam[off+2] = a1 & 0xFF; oam[off+3] = (a1 >> 8) & 0xFF;
    oam[off+4] = a2 & 0xFF; oam[off+5] = (a2 >> 8) & 0xFF;
    oam[off+6] = 0; oam[off+7] = 0;
}

TEST(OAM, BasicParse) {
    uint8_t oam[8] = {};
    // attr0: y=50, rotscale=0, disable=0, mode=0, 4bpp, shape=0 (square)
    uint16_t a0 = 50;
    // attr1: x=100, size=1 (16x16 for square)
    uint16_t a1 = 100 | (1 << 14);
    // attr2: tile=42, priority=2, palette=5
    uint16_t a2 = 42 | (2 << 10) | (5 << 12);
    WriteOAMEntry(oam, 0, a0, a1, a2);

    OAMEntry entry;
    entry.ParseFromRaw(oam);
    EXPECT_EQ(entry.y, 50);
    EXPECT_EQ(entry.x, 100);
    EXPECT_EQ(entry.tile_index, 42);
    EXPECT_EQ(entry.priority, 2);
    EXPECT_EQ(entry.palette, 5);
    EXPECT_EQ(entry.width, 16);
    EXPECT_EQ(entry.height, 16);
    EXPECT_FALSE(entry.disabled);
}

TEST(OAM, DisabledEntry) {
    uint8_t oam[8] = {};
    // attr0: rotscale=0, disable=1 (bit 9)
    uint16_t a0 = (1 << 9);
    WriteOAMEntry(oam, 0, a0, 0, 0);
    OAMEntry entry;
    entry.ParseFromRaw(oam);
    EXPECT_TRUE(entry.disabled);
}

TEST(OAM, NegativeX) {
    uint8_t oam[8] = {};
    // X = -10 → 9-bit: 502 (0x1F6)
    uint16_t a1 = 0x1F6;
    WriteOAMEntry(oam, 0, 0, a1, 0);
    OAMEntry entry;
    entry.ParseFromRaw(oam);
    EXPECT_EQ(entry.x, -10);
}

TEST(OAM, RotScale) {
    uint8_t oam[8] = {};
    // rotscale=1, double_size=1, rot_param=3
    uint16_t a0 = (1 << 8) | (1 << 9);
    uint16_t a1 = (3 << 9);
    WriteOAMEntry(oam, 0, a0, a1, 0);
    OAMEntry entry;
    entry.ParseFromRaw(oam);
    EXPECT_TRUE(entry.rot_scale);
    EXPECT_TRUE(entry.double_size);
    EXPECT_EQ(entry.rot_param, 3);
    EXPECT_FALSE(entry.hflip); // No flip in rot mode
}

TEST(OAM, HVFlip) {
    uint8_t oam[8] = {};
    uint16_t a1 = (1 << 12) | (1 << 13); // hflip=1, vflip=1
    WriteOAMEntry(oam, 0, 0, a1, 0);
    OAMEntry entry;
    entry.ParseFromRaw(oam);
    EXPECT_TRUE(entry.hflip);
    EXPECT_TRUE(entry.vflip);
}

TEST(OAM, Is8bpp) {
    uint8_t oam[8] = {};
    uint16_t a0 = (1 << 13); // 8bpp
    WriteOAMEntry(oam, 0, a0, 0, 0);
    OAMEntry entry;
    entry.ParseFromRaw(oam);
    EXPECT_TRUE(entry.is_8bpp);
}

TEST(OAM, Dimensions_HorizontalRect) {
    int w, h;
    OAMEntry::GetObjDimensions(1, 2, w, h); // Horizontal, size=2 → 32x16
    EXPECT_EQ(w, 32);
    EXPECT_EQ(h, 16);
}

TEST(OAM, Dimensions_VerticalRect) {
    int w, h;
    OAMEntry::GetObjDimensions(2, 3, w, h); // Vertical, size=3 → 32x64
    EXPECT_EQ(w, 32);
    EXPECT_EQ(h, 64);
}

// ============================================================================
// OAM Affine Parameters
// ============================================================================

TEST(OAM, AffineParams) {
    uint8_t oam[1024] = {};
    // Group 0: PA at byte 6, PB at byte 14, PC at byte 22, PD at byte 30
    oam[6] = 0x00; oam[7] = 0x01;   // PA = 0x0100 = 1.0 in 8.8
    oam[14] = 0x80; oam[15] = 0x00; // PB = 0x0080 = 0.5
    oam[22] = 0x00; oam[23] = 0xFF; // PC = 0xFF00 = -1.0 (approx)
    oam[30] = 0x00; oam[31] = 0x02; // PD = 0x0200 = 2.0
    OAMAffineParams aff;
    aff.ParseFromRaw(oam, 0);
    EXPECT_EQ(aff.pa, 0x0100);
    EXPECT_EQ(aff.pb, 0x0080);
    EXPECT_EQ(aff.pd, 0x0200);
}

// ============================================================================
// BG Control Tests
// ============================================================================

TEST(BGCtrl, TextModeDecode) {
    BGControl bg;
    // priority=2, char_base=3, 8bpp, screen_base=10, size=3 (512x512)
    uint16_t bgcnt = 2 | (3 << 2) | (1 << 7) | (10 << 8) | (3 << 14);
    bg.Decode(bgcnt);
    EXPECT_EQ(bg.priority, 2);
    EXPECT_EQ(bg.char_base, 3);
    EXPECT_TRUE(bg.is_8bpp);
    EXPECT_EQ(bg.screen_base, 10);
    EXPECT_EQ(bg.screen_size, 3);
    EXPECT_EQ(bg.map_width, 512);
    EXPECT_EQ(bg.map_height, 512);
}

TEST(BGCtrl, SmallestSize) {
    BGControl bg;
    bg.Decode(0); // Size=0 → 256x256
    EXPECT_EQ(bg.map_width, 256);
    EXPECT_EQ(bg.map_height, 256);
}

// ============================================================================
// Blend Control Tests
// ============================================================================

TEST(Blend, AlphaMode) {
    BlendControl bld;
    // mode=1 (alpha), target1=BG0+BG1 (0x03), target2=OBJ (0x10)
    bld.DecodeBLDCNT((1 << 6) | 0x03 | (0x10 << 8));
    EXPECT_EQ(bld.mode, 1);
    EXPECT_EQ(bld.target1, 0x03);
    EXPECT_EQ(bld.target2, 0x10);
}

TEST(Blend, AlphaCoefficients) {
    BlendControl bld;
    bld.DecodeBLDALPHA(8 | (12 << 8)); // EVA=8, EVB=12
    EXPECT_EQ(bld.eva, 8);
    EXPECT_EQ(bld.evb, 12);
}

TEST(Blend, BrightnessClamp) {
    BlendControl bld;
    bld.DecodeBLDY(20); // Clamped to 16
    EXPECT_EQ(bld.evy, 16);
}

// ============================================================================
// Window Control Tests
// ============================================================================

TEST(Window, WINH_Decode) {
    WindowControl win;
    win.DecodeWINH(0, (100 << 8) | 200); // x1=100, x2=200
    EXPECT_EQ(win.win0_x1, 100);
    EXPECT_EQ(win.win0_x2, 200);
}

TEST(Window, WININ_Decode) {
    WindowControl win;
    win.DecodeWININ(0x3F | (0x15 << 8)); // win0=all, win1=custom
    EXPECT_EQ(win.winin_win0, 0x3F);
    EXPECT_EQ(win.winin_win1, 0x15);
}

// ============================================================================
// Tile Decoder Tests
// ============================================================================

TEST(TileDecode, Tile4bpp) {
    uint8_t tile[32] = {};
    tile[0] = 0xAB; // pixel 0=0xB, pixel 1=0xA
    uint8_t indices[64] = {};
    TileDecoder::DecodeTile4bpp(tile, indices);
    EXPECT_EQ(indices[0], 0x0B);
    EXPECT_EQ(indices[1], 0x0A);
}

TEST(TileDecode, Tile8bpp) {
    uint8_t tile[64] = {};
    tile[0] = 42; tile[63] = 255;
    uint8_t indices[64] = {};
    TileDecoder::DecodeTile8bpp(tile, indices);
    EXPECT_EQ(indices[0], 42);
    EXPECT_EQ(indices[63], 255);
}

TEST(TileDecode, PaletteToRGBA_White) {
    uint32_t rgba = TileDecoder::PaletteToRGBA32(0x7FFF);
    EXPECT_EQ(rgba & 0xFF, 255);       // R
    EXPECT_EQ((rgba >> 8) & 0xFF, 255); // G
    EXPECT_EQ((rgba >> 16) & 0xFF, 255);// B
    EXPECT_EQ((rgba >> 24) & 0xFF, 255);// A
}

TEST(TileDecode, PaletteToRGBA_Red) {
    uint32_t rgba = TileDecoder::PaletteToRGBA32(0x001F); // R=31
    EXPECT_EQ(rgba & 0xFF, 255);        // R=255
    EXPECT_EQ((rgba >> 8) & 0xFF, 0);   // G=0
}

TEST(TileDecode, DecodeTileToRGBA_Transparent) {
    uint8_t tile[64] = {}; // All zeros = transparent
    uint8_t palette[512] = {};
    uint32_t rgba[64] = {};
    TileDecoder::DecodeTileToRGBA(tile, palette, 0, true, rgba);
    for (int i = 0; i < 64; ++i) EXPECT_EQ(rgba[i], 0u);
}

TEST(TileDecode, DecodeTileToRGBA_4bpp) {
    uint8_t tile[32] = {};
    tile[0] = 0x01; // Index 1 at pixel 0
    uint8_t palette[512] = {};
    // Palette 0, color 1 = pure red (0x001F)
    palette[2] = 0x1F; palette[3] = 0x00;
    uint32_t rgba[64] = {};
    TileDecoder::DecodeTileToRGBA(tile, palette, 0, false, rgba);
    EXPECT_EQ(rgba[0] & 0xFF, 255); // R=255
    EXPECT_NE(rgba[0], 0u);         // Not transparent
}

// ============================================================================
// NDS 2D Engine Register Tests
// ============================================================================

TEST(Engine2D, DISPCNT_Write) {
    NDS2DEngine eng;
    eng.WriteRegister(0x00, 0x00010003); // BG mode 3, BG0 enabled
    EXPECT_EQ(eng.GetBGMode(), 3);
}

TEST(Engine2D, BGEnabled) {
    NDS2DEngine eng;
    eng.WriteRegister(0x00, (1 << 8) | (1 << 10)); // BG0 + BG2 enabled
    EXPECT_TRUE(eng.IsBGEnabled(0));
    EXPECT_FALSE(eng.IsBGEnabled(1));
    EXPECT_TRUE(eng.IsBGEnabled(2));
}

TEST(Engine2D, BGControl_Write) {
    NDS2DEngine eng;
    uint16_t bgcnt = 1 | (5 << 2) | (1 << 7) | (8 << 8);
    eng.WriteRegister(0x08, bgcnt); // BG0CNT
    EXPECT_EQ(eng.bg_layers[0].control.priority, 1);
    EXPECT_EQ(eng.bg_layers[0].control.char_base, 5);
    EXPECT_TRUE(eng.bg_layers[0].control.is_8bpp);
    EXPECT_EQ(eng.bg_layers[0].control.screen_base, 8);
}

TEST(Engine2D, ScrollOffset) {
    NDS2DEngine eng;
    eng.WriteRegister(0x10, 100); // BG0 HOFS
    eng.WriteRegister(0x12, 200); // BG0 VOFS
    EXPECT_EQ(eng.bg_layers[0].scroll_x, 100);
    EXPECT_EQ(eng.bg_layers[0].scroll_y, 200);
}

TEST(Engine2D, Affine_BG2) {
    NDS2DEngine eng;
    eng.WriteRegister(0x20, 0x0100); // PA = 1.0 (8.8)
    eng.WriteRegister(0x28, 0x00010000); // ref_x = 0x10000
    EXPECT_EQ(eng.bg_layers[2].affine.pa, 0x0100);
    EXPECT_EQ(eng.bg_layers[2].affine.ref_x, 0x00010000);
}

TEST(Engine2D, BlendViaRegister) {
    NDS2DEngine eng;
    eng.WriteRegister(0x50, (1 << 6) | 0x03 | (0x10 << 8));
    EXPECT_EQ(eng.blend.mode, 1);
    EXPECT_EQ(eng.blend.target1, 0x03);
}

TEST(Engine2D, WindowViaRegister) {
    NDS2DEngine eng;
    eng.WriteRegister(0x40, (50 << 8) | 150);
    EXPECT_EQ(eng.windows.win0_x1, 50);
    EXPECT_EQ(eng.windows.win0_x2, 150);
}

TEST(Engine2D, MosaicWrite) {
    NDS2DEngine eng;
    eng.WriteRegister(0x4C, 0x0302); // bg_h=2, bg_v=0, obj_h=3, obj_v=0
    EXPECT_EQ(eng.mosaic_bg_h, 2);
    EXPECT_EQ(eng.mosaic_obj_h, 3);
}

TEST(Engine2D, MasterBrightness) {
    NDS2DEngine eng;
    eng.WriteRegister(0x6C, 0x4010); // Mode=1 (brighten), factor=16
    EXPECT_EQ(eng.master_bright, 0x4010);
}

// ============================================================================
// OAM → Sprite2D Pipeline
// ============================================================================

TEST(Engine2D, ParseOAM_ActiveSprites) {
    NDS2DEngine eng;
    uint8_t oam[1024];
    // Initialize all entries as disabled (bit 9 set, rotscale=0)
    for (int i = 0; i < 128; ++i) {
        WriteOAMEntry(oam, i, (1 << 9), 0, 0);
    }
    // Object 0: active at (10, 20), 8x8
    WriteOAMEntry(oam, 0, 20, 10, 42);
    // Object 1: stays disabled
    // Object 2: active
    WriteOAMEntry(oam, 2, 80, 50, 100);

    std::vector<Sprite2D> sprites;
    eng.ParseOAM(oam, sprites);
    EXPECT_EQ(sprites.size(), 2u); // Only 2 active (obj 1 is disabled)
    EXPECT_FLOAT_EQ(sprites[0].x, 10.0f);
    EXPECT_FLOAT_EQ(sprites[0].y, 20.0f);
}

// ============================================================================
// Mock 2D Renderer
// ============================================================================

class MockRenderer2D : public Renderer2D {
public:
    int frame_count = 0;
    size_t last_sprite_count = 0;
    void SubmitFrame2D(const std::vector<Sprite2D>& sprites,
                       const std::array<BGLayer2D, 4>&,
                       const BlendControl&,
                       const WindowControl&) override {
        frame_count++;
        last_sprite_count = sprites.size();
    }
};

TEST(Engine2D, RendererReceivesFrame) {
    NDS2DEngine eng;
    MockRenderer2D mock;
    eng.renderer = &mock;
    uint8_t oam[1024];
    // Initialize all entries as disabled
    for (int i = 0; i < 128; ++i) {
        WriteOAMEntry(oam, i, (1 << 9), 0, 0);
    }
    WriteOAMEntry(oam, 0, 10, 20, 42); // 1 active sprite
    eng.SubmitFrame(oam);
    EXPECT_EQ(mock.frame_count, 1);
    EXPECT_EQ(mock.last_sprite_count, 1u);
}

// ============================================================================
// Memory Map Integration
// ============================================================================

TEST(Engine2D, MemMap_DISPCNT_EngineA) {
    NDSMemory m;
    m.Write32(0x04000000, 0x00010003);
    EXPECT_EQ(m.engine2d_a.GetBGMode(), 3);
}

TEST(Engine2D, MemMap_DISPCNT_EngineB) {
    NDSMemory m;
    m.Write32(0x04001000, 0x00000005);
    EXPECT_EQ(m.engine2d_b.GetBGMode(), 5);
}

TEST(Engine2D, MemMap_BG0CNT_EngineA) {
    NDSMemory m;
    m.Write32(0x04000008, 0x0801); // priority=1, screen_base=8
    EXPECT_EQ(m.engine2d_a.bg_layers[0].control.priority, 1);
}

TEST(Engine2D, MemMap_VRAMCNT_Sync) {
    NDSMemory m;
    m.Write8(0x04000241, 0x81); // Bank B → Engine A BG
    EXPECT_EQ(m.vram_ctrl.GetBankMapping(1), VRAMMapping::EngineA_BG);
}

TEST(Engine2D, MemMap_EngineA_Read) {
    NDSMemory m;
    m.Write32(0x04000000, 0x12345678);
    EXPECT_EQ(m.Read32(0x04000000), 0x12345678u);
}
