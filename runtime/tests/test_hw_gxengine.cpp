#include <gtest/gtest.h>
#include <cmath>
#include "hw_gxengine.h"
#include "memory_map.h"

// Helper: convert float to DS 20.12 fixed-point
static uint32_t ToFixed(float f) { return static_cast<uint32_t>(static_cast<int32_t>(f * 4096.0f)); }

// ============================================================================
// Fixed-Point Conversion Tests
// ============================================================================

TEST(GXFixed, s4_12_Positive) { EXPECT_NEAR(DSFixed::ToFloat_s4_12(0x1000), 1.0f, 0.001f); }
TEST(GXFixed, s4_12_Negative) { EXPECT_NEAR(DSFixed::ToFloat_s4_12(0xF000), -1.0f, 0.001f); }
TEST(GXFixed, s4_12_Zero)     { EXPECT_FLOAT_EQ(DSFixed::ToFloat_s4_12(0), 0.0f); }
TEST(GXFixed, s4_12_Half)     { EXPECT_NEAR(DSFixed::ToFloat_s4_12(0x0800), 0.5f, 0.001f); }

TEST(GXFixed, f20_12_Positive) { EXPECT_NEAR(DSFixed::ToFloat_20_12(ToFixed(2.5f)), 2.5f, 0.001f); }
TEST(GXFixed, f20_12_Negative) { EXPECT_NEAR(DSFixed::ToFloat_20_12(ToFixed(-3.0f)), -3.0f, 0.001f); }

TEST(GXFixed, s1_3_6_Positive) { EXPECT_NEAR(DSFixed::ToFloat_s1_3_6(0x040), 1.0f, 0.02f); }
TEST(GXFixed, s1_3_6_Negative) {
    uint32_t neg1 = 0x3C0; // -1.0 in 10-bit 1.3.6
    EXPECT_NEAR(DSFixed::ToFloat_s1_3_6(neg1), -1.0f, 0.02f);
}

TEST(GXFixed, s1_0_9_Normal) { EXPECT_NEAR(DSFixed::ToFloat_s1_0_9(0x100), 0.5f, 0.01f); }

TEST(GXFixed, RGB555_White) {
    float r, g, b;
    DSFixed::RGB555ToFloat(0x7FFF, r, g, b);
    EXPECT_FLOAT_EQ(r, 1.0f); EXPECT_FLOAT_EQ(g, 1.0f); EXPECT_FLOAT_EQ(b, 1.0f);
}
TEST(GXFixed, RGB555_Red) {
    float r, g, b;
    DSFixed::RGB555ToFloat(0x001F, r, g, b);
    EXPECT_FLOAT_EQ(r, 1.0f); EXPECT_FLOAT_EQ(g, 0.0f); EXPECT_FLOAT_EQ(b, 0.0f);
}
TEST(GXFixed, RGB555_Black) {
    float r, g, b;
    DSFixed::RGB555ToFloat(0x0000, r, g, b);
    EXPECT_FLOAT_EQ(r, 0.0f); EXPECT_FLOAT_EQ(g, 0.0f); EXPECT_FLOAT_EQ(b, 0.0f);
}

// ============================================================================
// Mat4 Tests
// ============================================================================

TEST(GXMat4, Identity) {
    Mat4 m;
    EXPECT_FLOAT_EQ(m.m[0], 1.0f); EXPECT_FLOAT_EQ(m.m[5], 1.0f);
    EXPECT_FLOAT_EQ(m.m[10], 1.0f); EXPECT_FLOAT_EQ(m.m[15], 1.0f);
    EXPECT_FLOAT_EQ(m.m[1], 0.0f); EXPECT_FLOAT_EQ(m.m[4], 0.0f);
}

TEST(GXMat4, IdentityTransform) {
    Mat4 m;
    Vec4f v(1.0f, 2.0f, 3.0f, 1.0f);
    Vec4f r = m.Transform(v);
    EXPECT_FLOAT_EQ(r.x, 1.0f); EXPECT_FLOAT_EQ(r.y, 2.0f); EXPECT_FLOAT_EQ(r.z, 3.0f);
}

TEST(GXMat4, Scale) {
    Mat4 m;
    m.Scale(2.0f, 3.0f, 4.0f);
    Vec4f r = m.Transform(Vec4f(1.0f, 1.0f, 1.0f, 1.0f));
    EXPECT_FLOAT_EQ(r.x, 2.0f); EXPECT_FLOAT_EQ(r.y, 3.0f); EXPECT_FLOAT_EQ(r.z, 4.0f);
}

TEST(GXMat4, Translate) {
    Mat4 m;
    m.Translate(10.0f, 20.0f, 30.0f);
    Vec4f r = m.Transform(Vec4f(0.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_FLOAT_EQ(r.x, 10.0f); EXPECT_FLOAT_EQ(r.y, 20.0f); EXPECT_FLOAT_EQ(r.z, 30.0f);
}

TEST(GXMat4, MultiplyIdentity) {
    Mat4 a, b;
    a.Scale(2.0f, 2.0f, 2.0f);
    a.Multiply(b); // Multiply by identity — no change
    Vec4f r = a.Transform(Vec4f(1.0f, 1.0f, 1.0f, 1.0f));
    EXPECT_FLOAT_EQ(r.x, 2.0f); EXPECT_FLOAT_EQ(r.y, 2.0f);
}

TEST(GXMat4, LoadFromFixed4x4) {
    Mat4 m;
    uint32_t params[16] = {};
    params[0] = ToFixed(2.0f); params[5] = ToFixed(3.0f);
    params[10] = ToFixed(4.0f); params[15] = ToFixed(1.0f);
    m.LoadFromFixed4x4(params);
    EXPECT_NEAR(m.m[0], 2.0f, 0.001f); EXPECT_NEAR(m.m[5], 3.0f, 0.001f);
}

TEST(GXMat4, LoadFromFixed4x3) {
    Mat4 m;
    uint32_t params[12] = {};
    params[0] = ToFixed(1.0f); params[4] = ToFixed(1.0f); params[8] = ToFixed(1.0f);
    params[9] = ToFixed(5.0f); params[10] = ToFixed(6.0f); params[11] = ToFixed(7.0f);
    m.LoadFromFixed4x3(params);
    EXPECT_NEAR(m.m[12], 5.0f, 0.001f); EXPECT_FLOAT_EQ(m.m[15], 1.0f);
}

TEST(GXMat4, TransformNormal) {
    Mat4 m;
    m.Scale(2.0f, 3.0f, 4.0f);
    Vec3f n = m.TransformNormal(Vec3f(1.0f, 0.0f, 0.0f));
    EXPECT_FLOAT_EQ(n.x, 2.0f); EXPECT_FLOAT_EQ(n.y, 0.0f);
}

// ============================================================================
// GX Engine — Matrix Stack Tests
// ============================================================================

TEST(GXEngine, MatrixModeSwitch) {
    GXEngine gx;
    uint32_t p;
    p = 1; gx.WriteCommandPort(0x04000440, p); // MTX_MODE = Position
    EXPECT_EQ(static_cast<int>(gx.matrix_mode), 1);
    p = 0; gx.WriteCommandPort(0x04000440, p); // MTX_MODE = Projection
    EXPECT_EQ(static_cast<int>(gx.matrix_mode), 0);
}

TEST(GXEngine, PushPopProjection) {
    GXEngine gx;
    uint32_t p = 0;
    gx.WriteCommandPort(0x04000440, p); // MODE = Projection
    gx.projection_matrix.m[0] = 42.0f;
    gx.WriteCommandPort(0x04000444, 0); // PUSH
    gx.projection_matrix.Identity();
    EXPECT_FLOAT_EQ(gx.projection_matrix.m[0], 1.0f);
    p = 1; gx.WriteCommandPort(0x04000448, p); // POP(1)
    EXPECT_FLOAT_EQ(gx.projection_matrix.m[0], 42.0f);
}

TEST(GXEngine, PushPopPosition) {
    GXEngine gx;
    uint32_t p = 2;
    gx.WriteCommandPort(0x04000440, p); // MODE = PositionVector
    gx.position_matrix.m[0] = 99.0f;
    gx.vector_matrix.m[0] = 77.0f;
    gx.WriteCommandPort(0x04000444, 0); // PUSH
    gx.position_matrix.Identity();
    gx.vector_matrix.Identity();
    p = 1; gx.WriteCommandPort(0x04000448, p); // POP(1)
    EXPECT_FLOAT_EQ(gx.position_matrix.m[0], 99.0f);
    EXPECT_FLOAT_EQ(gx.vector_matrix.m[0], 77.0f);
}

TEST(GXEngine, StackOverflowSetsError) {
    GXEngine gx;
    uint32_t p = 0;
    gx.WriteCommandPort(0x04000440, p); // Projection mode (1-deep)
    gx.WriteCommandPort(0x04000444, 0); // PUSH (sp=1)
    EXPECT_FALSE(gx.matrix_stack_error);
    gx.WriteCommandPort(0x04000444, 0); // PUSH again — overflow!
    EXPECT_TRUE(gx.matrix_stack_error);
}

TEST(GXEngine, StackUnderflowSetsError) {
    GXEngine gx;
    uint32_t p = 0;
    gx.WriteCommandPort(0x04000440, p); // Projection mode
    p = 1; gx.WriteCommandPort(0x04000448, p); // POP with empty stack
    EXPECT_TRUE(gx.matrix_stack_error);
}

TEST(GXEngine, MTX_IDENTITY) {
    GXEngine gx;
    uint32_t p = 1;
    gx.WriteCommandPort(0x04000440, p); // Position mode
    gx.position_matrix.m[0] = 999.0f;
    gx.WriteCommandPort(0x04000454, 0); // MTX_IDENTITY
    EXPECT_FLOAT_EQ(gx.position_matrix.m[0], 1.0f);
}

TEST(GXEngine, MTX_SCALE_ViaPort) {
    GXEngine gx;
    uint32_t p = 1;
    gx.WriteCommandPort(0x04000440, p); // Position mode
    gx.WriteCommandPort(0x04000454, 0); // MTX_IDENTITY
    // MTX_SCALE needs 3 params
    gx.WriteCommandPort(0x0400046C, ToFixed(2.0f));
    gx.WriteCommandPort(0x0400046C, ToFixed(3.0f));
    gx.WriteCommandPort(0x0400046C, ToFixed(4.0f));
    EXPECT_NEAR(gx.position_matrix.m[0], 2.0f, 0.001f);
    EXPECT_NEAR(gx.position_matrix.m[5], 3.0f, 0.001f);
    EXPECT_NEAR(gx.position_matrix.m[10], 4.0f, 0.001f);
}

TEST(GXEngine, MTX_TRANS_ViaPort) {
    GXEngine gx;
    uint32_t p = 1;
    gx.WriteCommandPort(0x04000440, p); // Position mode
    gx.WriteCommandPort(0x04000454, 0); // IDENTITY
    gx.WriteCommandPort(0x04000470, ToFixed(5.0f));
    gx.WriteCommandPort(0x04000470, ToFixed(6.0f));
    gx.WriteCommandPort(0x04000470, ToFixed(7.0f));
    EXPECT_NEAR(gx.position_matrix.m[12], 5.0f, 0.001f);
    EXPECT_NEAR(gx.position_matrix.m[13], 6.0f, 0.001f);
}

TEST(GXEngine, StoreRestore) {
    GXEngine gx;
    uint32_t p = 1;
    gx.WriteCommandPort(0x04000440, p); // Position mode
    gx.position_matrix.m[0] = 42.0f;
    p = 5; gx.WriteCommandPort(0x0400044C, p); // STORE at index 5
    gx.position_matrix.Identity();
    p = 5; gx.WriteCommandPort(0x04000450, p); // RESTORE from index 5
    EXPECT_FLOAT_EQ(gx.position_matrix.m[0], 42.0f);
}

// ============================================================================
// GX Engine — Vertex Tests
// ============================================================================

TEST(GXEngine, VTX16_Triangle) {
    GXEngine gx;
    uint32_t p;
    p = 1; gx.WriteCommandPort(0x04000440, p);  // Position mode
    gx.WriteCommandPort(0x04000454, 0);          // Identity
    p = 0; gx.WriteCommandPort(0x04000440, p);   // Projection mode
    gx.WriteCommandPort(0x04000454, 0);          // Identity

    p = 0; gx.WriteCommandPort(0x04000500, p);  // BEGIN_VTXS(Triangles)
    // V0: (1.0, 0.0)
    gx.WriteCommandPort(0x0400048C, (0x0000 << 16) | 0x1000); // X=1.0, Y=0.0
    gx.WriteCommandPort(0x0400048C, 0x0000); // Z=0.0
    // V1: (0.0, 1.0)
    gx.WriteCommandPort(0x0400048C, (0x1000 << 16) | 0x0000);
    gx.WriteCommandPort(0x0400048C, 0x0000);
    // V2: (-1.0, 0.0)
    gx.WriteCommandPort(0x0400048C, (0x0000u << 16) | 0xF000u);
    gx.WriteCommandPort(0x0400048C, 0x0000);
    gx.WriteCommandPort(0x04000504, 0); // END_VTXS

    EXPECT_EQ(gx.GetVertexCount(), 3u);
    EXPECT_EQ(gx.GetPolygonCount(), 1u);
    EXPECT_EQ(gx.polygon_buffer[0].vertex_count, 3);
}

TEST(GXEngine, QuadPrimitive) {
    GXEngine gx;
    uint32_t p = 1;
    gx.WriteCommandPort(0x04000440, p); gx.WriteCommandPort(0x04000454, 0);
    p = 0; gx.WriteCommandPort(0x04000440, p); gx.WriteCommandPort(0x04000454, 0);

    p = 1; gx.WriteCommandPort(0x04000500, p); // BEGIN_VTXS(Quads)
    for (int i = 0; i < 4; ++i) {
        gx.WriteCommandPort(0x0400048C, 0x1000); // VTX_16 param1
        gx.WriteCommandPort(0x0400048C, 0x0000); // VTX_16 param2
    }
    gx.WriteCommandPort(0x04000504, 0);
    EXPECT_EQ(gx.GetPolygonCount(), 1u);
    EXPECT_EQ(gx.polygon_buffer[0].vertex_count, 4);
}

TEST(GXEngine, TriStrip) {
    GXEngine gx;
    uint32_t p = 1;
    gx.WriteCommandPort(0x04000440, p); gx.WriteCommandPort(0x04000454, 0);
    p = 0; gx.WriteCommandPort(0x04000440, p); gx.WriteCommandPort(0x04000454, 0);

    p = 2; gx.WriteCommandPort(0x04000500, p); // BEGIN_VTXS(TriStrip)
    for (int i = 0; i < 5; ++i) {
        gx.WriteCommandPort(0x0400048C, 0x1000);
        gx.WriteCommandPort(0x0400048C, 0x0000);
    }
    gx.WriteCommandPort(0x04000504, 0);
    EXPECT_EQ(gx.GetVertexCount(), 5u);
    EXPECT_EQ(gx.GetPolygonCount(), 3u); // 5 verts in strip = 3 triangles
}

TEST(GXEngine, VTX10_Parsing) {
    GXEngine gx;
    uint32_t p = 1;
    gx.WriteCommandPort(0x04000440, p); gx.WriteCommandPort(0x04000454, 0);
    p = 0; gx.WriteCommandPort(0x04000440, p); gx.WriteCommandPort(0x04000454, 0);

    p = 0; gx.WriteCommandPort(0x04000500, p); // Triangles
    // VTX_10: pack X=1.0(0x40), Y=0, Z=0 in 10-bit fields
    uint32_t vtx10 = 0x040; // X=64 → 1.0 in 1.3.6
    gx.WriteCommandPort(0x04000490, vtx10);
    EXPECT_EQ(gx.GetVertexCount(), 1u);
    EXPECT_NEAR(gx.vertex_buffer[0].world_pos.x, 1.0f, 0.02f);
}

TEST(GXEngine, VTX_XY_ReusesZ) {
    GXEngine gx;
    uint32_t p = 1;
    gx.WriteCommandPort(0x04000440, p); gx.WriteCommandPort(0x04000454, 0);
    p = 0; gx.WriteCommandPort(0x04000440, p); gx.WriteCommandPort(0x04000454, 0);

    p = 0; gx.WriteCommandPort(0x04000500, p);
    // First vertex with Z=1.0 via VTX_16
    gx.WriteCommandPort(0x0400048C, 0x0000);
    gx.WriteCommandPort(0x0400048C, 0x1000); // Z=1.0
    // VTX_XY should reuse Z=1.0
    gx.WriteCommandPort(0x04000494, (0x2000 << 16) | 0x1000); // X=1.0, Y=2.0
    EXPECT_EQ(gx.GetVertexCount(), 2u);
    EXPECT_NEAR(gx.vertex_buffer[1].world_pos.z, 1.0f, 0.001f);
}

TEST(GXEngine, COLOR_Sets_VertexColor) {
    GXEngine gx;
    gx.WriteCommandPort(0x04000480, 0x001F); // Pure red (R=31)
    EXPECT_FLOAT_EQ(gx.current_color_r, 1.0f);
    EXPECT_FLOAT_EQ(gx.current_color_g, 0.0f);
}

TEST(GXEngine, TEXCOORD_Sets_UV) {
    GXEngine gx;
    // S=16.0 (0x0100 in 12.4), T=32.0 (0x0200 in 12.4)
    gx.WriteCommandPort(0x04000488, (0x0200 << 16) | 0x0100);
    EXPECT_NEAR(gx.current_texcoord_s, 16.0f, 0.1f);
    EXPECT_NEAR(gx.current_texcoord_t, 32.0f, 0.1f);
}

TEST(GXEngine, POLYGON_ATTR_Applied) {
    GXEngine gx;
    uint32_t attr = (0x1F << 16) | (0x3F << 24); // alpha=31, polyID=63
    gx.WriteCommandPort(0x040004A4, attr);
    uint32_t p = 0;
    gx.WriteCommandPort(0x04000440, p); gx.WriteCommandPort(0x04000454, 0);
    gx.WriteCommandPort(0x04000500, p); // BEGIN_VTXS applies pending attr
    EXPECT_EQ(gx.poly_attr_current, attr);
}

TEST(GXEngine, VIEWPORT_Parsing) {
    GXEngine gx;
    gx.WriteCommandPort(0x04000580, 0xBF00FF0A); // x1=10, y1=255, x2=0, y2=191
    EXPECT_EQ(gx.viewport_x1, 10);
    EXPECT_EQ(gx.viewport_y1, 255);
    EXPECT_EQ(gx.viewport_x2, 0);
    EXPECT_EQ(gx.viewport_y2, 191);
}

TEST(GXEngine, SWAP_BUFFERS_ClearsAndCounts) {
    GXEngine gx;
    uint32_t p = 1;
    gx.WriteCommandPort(0x04000440, p); gx.WriteCommandPort(0x04000454, 0);
    p = 0; gx.WriteCommandPort(0x04000440, p); gx.WriteCommandPort(0x04000454, 0);
    p = 0; gx.WriteCommandPort(0x04000500, p);
    for (int i = 0; i < 3; ++i) {
        gx.WriteCommandPort(0x0400048C, 0x1000);
        gx.WriteCommandPort(0x0400048C, 0x0000);
    }
    gx.WriteCommandPort(0x04000504, 0);
    EXPECT_EQ(gx.GetVertexCount(), 3u);
    gx.WriteCommandPort(0x04000540, 0); // SWAP_BUFFERS
    EXPECT_EQ(gx.GetVertexCount(), 0u);
    EXPECT_EQ(gx.swap_buffer_count, 1);
}

// ============================================================================
// GX Engine — GXSTAT & RAM_COUNT Register Readback
// ============================================================================

TEST(GXEngine, GXSTAT_StackPointer) {
    GXEngine gx;
    uint32_t p = 1;
    gx.WriteCommandPort(0x04000440, p); // Position mode
    gx.WriteCommandPort(0x04000444, 0); // PUSH
    gx.WriteCommandPort(0x04000444, 0); // PUSH
    uint32_t stat = gx.ReadRegister(0x04000600);
    EXPECT_EQ((stat >> 8) & 0x1F, 2u); // SP=2
}

TEST(GXEngine, GXSTAT_FIFOEmpty) {
    GXEngine gx;
    uint32_t stat = gx.ReadRegister(0x04000600);
    EXPECT_TRUE(stat & (1u << 26)); // FIFO empty
    EXPECT_TRUE(stat & (1u << 25)); // FIFO less than half full
}

TEST(GXEngine, RAM_COUNT) {
    GXEngine gx;
    uint32_t p = 1;
    gx.WriteCommandPort(0x04000440, p); gx.WriteCommandPort(0x04000454, 0);
    p = 0; gx.WriteCommandPort(0x04000440, p); gx.WriteCommandPort(0x04000454, 0);
    p = 0; gx.WriteCommandPort(0x04000500, p);
    for (int i = 0; i < 6; ++i) {
        gx.WriteCommandPort(0x0400048C, 0x1000);
        gx.WriteCommandPort(0x0400048C, 0x0000);
    }
    uint32_t count = gx.ReadRegister(0x04000604);
    EXPECT_EQ(count & 0xFFF, 2u);          // 2 polygons (6 verts / 3)
    EXPECT_EQ((count >> 16) & 0x1FFF, 6u); // 6 vertices
}

TEST(GXEngine, ClipMatrixReadback) {
    GXEngine gx;
    // Both projection and position are identity → clip = identity
    uint32_t val = gx.ReadRegister(0x04000640); // m[0]
    int32_t fixed = static_cast<int32_t>(val);
    EXPECT_NEAR(static_cast<float>(fixed) / 4096.0f, 1.0f, 0.001f);
}

// ============================================================================
// GX Engine — GXFIFO Packed Commands
// ============================================================================

TEST(GXEngine, GXFIFO_SingleCommand) {
    GXEngine gx;
    // Packed header: MTX_MODE(0x10) in byte 0, rest NOP
    gx.WriteGXFIFO(0x00000010);
    gx.WriteGXFIFO(2); // param: PositionVector mode
    EXPECT_EQ(static_cast<int>(gx.matrix_mode), 2);
}

TEST(GXEngine, GXFIFO_TwoCommands) {
    GXEngine gx;
    // Packed: MTX_MODE(0x10) + MTX_IDENTITY(0x15)
    gx.WriteGXFIFO(0x00001510);
    gx.WriteGXFIFO(1); // Param for MTX_MODE: position mode
    // MTX_IDENTITY has 0 params, executes immediately
    EXPECT_EQ(static_cast<int>(gx.matrix_mode), 1);
    EXPECT_FLOAT_EQ(gx.position_matrix.m[0], 1.0f);
}

// ============================================================================
// GX Engine — Lighting
// ============================================================================

TEST(GXEngine, DIF_AMB_SetsVertexColor) {
    GXEngine gx;
    // diffuse=white(0x7FFF), bit15=set vertex color, ambient=0
    uint32_t p = 0x7FFF | (1u << 15);
    gx.WriteCommandPort(0x040004C0, p);
    EXPECT_FLOAT_EQ(gx.current_color_r, 1.0f);
    EXPECT_FLOAT_EQ(gx.current_color_g, 1.0f);
}

TEST(GXEngine, LIGHT_COLOR_SetsLight) {
    GXEngine gx;
    // Light 2: color=green
    uint32_t p = (2u << 30) | (0x1F << 5);
    gx.WriteCommandPort(0x040004CC, p);
    EXPECT_EQ(gx.light_colors[2] & 0x7FFF, (0x1F << 5));
}

// ============================================================================
// GX Engine — Renderer Backend
// ============================================================================

class MockRenderer : public GXRenderer {
public:
    int frame_count = 0;
    uint32_t last_vtx_count = 0;
    uint32_t last_poly_count = 0;
    void SubmitFrame(const std::vector<GXVertex>& v, const std::vector<GXPolygon>& p) override {
        frame_count++;
        last_vtx_count = static_cast<uint32_t>(v.size());
        last_poly_count = static_cast<uint32_t>(p.size());
    }
};

TEST(GXEngine, RendererReceivesFrame) {
    GXEngine gx;
    MockRenderer mock;
    gx.renderer = &mock;
    uint32_t p = 1;
    gx.WriteCommandPort(0x04000440, p); gx.WriteCommandPort(0x04000454, 0);
    p = 0; gx.WriteCommandPort(0x04000440, p); gx.WriteCommandPort(0x04000454, 0);
    p = 0; gx.WriteCommandPort(0x04000500, p);
    for (int i = 0; i < 3; ++i) {
        gx.WriteCommandPort(0x0400048C, 0x1000);
        gx.WriteCommandPort(0x0400048C, 0x0000);
    }
    gx.WriteCommandPort(0x04000540, 0); // SWAP_BUFFERS
    EXPECT_EQ(mock.frame_count, 1);
    EXPECT_EQ(mock.last_vtx_count, 3u);
    EXPECT_EQ(mock.last_poly_count, 1u);
}

// ============================================================================
// GX Engine — Memory Map Integration
// ============================================================================

TEST(GXEngine, MemoryMap_BeginVtxs) {
    NDSMemory m;
    // Set identity matrices via memory map
    m.Write32(0x04000440, 1); // Position mode
    m.Write32(0x04000454, 0); // Identity
    m.Write32(0x04000440, 0); // Projection mode
    m.Write32(0x04000454, 0); // Identity
    m.Write32(0x04000500, 0); // BEGIN_VTXS(Triangles)
    for (int i = 0; i < 3; ++i) {
        m.Write32(0x0400048C, 0x1000);
        m.Write32(0x0400048C, 0x0000);
    }
    m.Write32(0x04000504, 0); // END_VTXS
    EXPECT_EQ(m.gx_engine.GetVertexCount(), 3u);
    EXPECT_EQ(m.gx_engine.GetPolygonCount(), 1u);
}

TEST(GXEngine, MemoryMap_GXSTAT_Read) {
    NDSMemory m;
    uint32_t stat = m.Read32(0x04000600);
    EXPECT_TRUE(stat & (1u << 26)); // FIFO empty
}

TEST(GXEngine, MemoryMap_GXFIFO_Write) {
    NDSMemory m;
    m.Write32(0x04000400, 0x00000010); // GXFIFO: MTX_MODE
    m.Write32(0x04000400, 2);          // Param: PositionVector
    EXPECT_EQ(static_cast<int>(m.gx_engine.matrix_mode), 2);
}

TEST(GXEngine, MemoryMap_ClearColorDepth) {
    NDSMemory m;
    m.Write32(0x04000350, 0x12345678);
    m.Write32(0x04000354, 0x7FFF);
    EXPECT_EQ(m.Read32(0x04000350), 0x12345678u);
    EXPECT_EQ(m.Read32(0x04000354), 0x7FFFu);
}

TEST(GXEngine, MemoryMap_DISP3DCNT) {
    NDSMemory m;
    m.Write32(0x04000060, 0x0007);
    EXPECT_EQ(m.Read32(0x04000060), 0x0007u);
}
