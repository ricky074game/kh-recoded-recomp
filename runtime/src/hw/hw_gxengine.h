#pragma once

// ============================================================================
// hw_gxengine.h — Nintendo DS 3D Geometry Engine Emulation
//
// The DS has a fixed-function 3D Geometry Engine with no programmable shaders.
// Games send geometry commands via memory-mapped IO ports (0x04000400+).
// Instead of emulating pixel-level rasterization, we intercept the geometry
// commands, convert DS fixed-point vertex data to IEEE 754 float, maintain
// software matrix stacks, collect transformed vertices/polygons per frame,
// and expose them for modern GPU rendering (Vulkan/OpenGL).
//
// Reference: GBATEK §DS 3D Engine
//   Command Ports:     0x04000440 - 0x040005CC
//   GXFIFO:            0x04000400 (packed command interface)
//   Status:            0x04000600 (GXSTAT)
//   RAM Count:         0x04000604 (polygon/vertex count)
//   Clip Matrix:       0x04000640 - 0x0400067F (read-only)
//   Direction Matrix:  0x04000680 - 0x040006A3 (read-only)
// ============================================================================

#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <array>
#include <algorithm>

// ============================================================================
// Fixed-Point Conversion Utilities
// ============================================================================
namespace DSFixed {
    // 4.12 signed fixed-point (16-bit) → float (used by VTX_16, TEXCOORD)
    inline float ToFloat_s4_12(uint16_t raw) {
        return static_cast<float>(static_cast<int16_t>(raw)) / 4096.0f;
    }

    // 20.12 signed fixed-point (32-bit) → float (used by matrix entries)
    inline float ToFloat_20_12(uint32_t raw) {
        return static_cast<float>(static_cast<int32_t>(raw)) / 4096.0f;
    }

    // 1.3.6 signed fixed-point (10-bit) → float (used by VTX_10)
    inline float ToFloat_s1_3_6(uint32_t raw10) {
        int32_t val = static_cast<int32_t>(raw10 & 0x3FF);
        if (val & 0x200) val |= ~0x3FF; // Sign-extend from 10 bits
        return static_cast<float>(val) / 64.0f;
    }

    // 1.0.9 signed fixed-point (10-bit) → float (used by NORMAL)
    inline float ToFloat_s1_0_9(uint32_t raw10) {
        int32_t val = static_cast<int32_t>(raw10 & 0x3FF);
        if (val & 0x200) val |= ~0x3FF;
        return static_cast<float>(val) / 512.0f;
    }

    // 12.4 signed fixed-point (16-bit) → float (used by TEXCOORD S/T)
    inline float ToFloat_s12_4(uint16_t raw) {
        return static_cast<float>(static_cast<int16_t>(raw)) / 16.0f;
    }

    // VTX_DIFF offset: 10-bit signed, 1/8 unit relative to previous vertex
    inline float ToFloat_vtx_diff(uint32_t raw10) {
        int32_t val = static_cast<int32_t>(raw10 & 0x3FF);
        if (val & 0x200) val |= ~0x3FF;
        return static_cast<float>(val) / 4096.0f;
    }

    // RGB555 color → normalized float [0.0, 1.0]
    inline void RGB555ToFloat(uint16_t rgb555, float& r, float& g, float& b) {
        r = static_cast<float>((rgb555 >> 0)  & 0x1F) / 31.0f;
        g = static_cast<float>((rgb555 >> 5)  & 0x1F) / 31.0f;
        b = static_cast<float>((rgb555 >> 10) & 0x1F) / 31.0f;
    }
}

// ============================================================================
// Vector Types
// ============================================================================
struct Vec3f {
    float x = 0, y = 0, z = 0;
    Vec3f() = default;
    Vec3f(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
};

struct Vec4f {
    float x = 0, y = 0, z = 0, w = 1.0f;
    Vec4f() = default;
    Vec4f(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    explicit Vec4f(const Vec3f& v) : x(v.x), y(v.y), z(v.z), w(1.0f) {}
};

// ============================================================================
// 4x4 Matrix — Row-major order (matches DS hardware convention)
//
// Layout:
//   m[0]  m[1]  m[2]  m[3]     ← row 0
//   m[4]  m[5]  m[6]  m[7]     ← row 1
//   m[8]  m[9]  m[10] m[11]    ← row 2
//   m[12] m[13] m[14] m[15]    ← row 3
// ============================================================================
struct Mat4 {
    float m[16] = {};

    Mat4() { Identity(); }

    // Set to identity matrix
    void Identity() {
        std::memset(m, 0, sizeof(m));
        m[0] = m[5] = m[10] = m[15] = 1.0f;
    }

    // Load 4x4 matrix from 16 DS fixed-point (20.12) parameters
    void LoadFromFixed4x4(const uint32_t* params) {
        for (int i = 0; i < 16; ++i) {
            m[i] = DSFixed::ToFloat_20_12(params[i]);
        }
    }

    // Load 4x3 matrix from 12 DS fixed-point parameters (row3 = 0,0,0,1)
    void LoadFromFixed4x3(const uint32_t* params) {
        m[0] = DSFixed::ToFloat_20_12(params[0]);
        m[1] = DSFixed::ToFloat_20_12(params[1]);
        m[2] = DSFixed::ToFloat_20_12(params[2]);
        m[3] = 0.0f;
        m[4] = DSFixed::ToFloat_20_12(params[3]);
        m[5] = DSFixed::ToFloat_20_12(params[4]);
        m[6] = DSFixed::ToFloat_20_12(params[5]);
        m[7] = 0.0f;
        m[8] = DSFixed::ToFloat_20_12(params[6]);
        m[9] = DSFixed::ToFloat_20_12(params[7]);
        m[10] = DSFixed::ToFloat_20_12(params[8]);
        m[11] = 0.0f;
        m[12] = DSFixed::ToFloat_20_12(params[9]);
        m[13] = DSFixed::ToFloat_20_12(params[10]);
        m[14] = DSFixed::ToFloat_20_12(params[11]);
        m[15] = 1.0f;
    }

    // Load 3x3 matrix from 9 DS fixed-point parameters (col3 = 0, row3 = 0,0,0,1)
    void LoadFromFixed3x3(const uint32_t* params) {
        m[0] = DSFixed::ToFloat_20_12(params[0]);
        m[1] = DSFixed::ToFloat_20_12(params[1]);
        m[2] = DSFixed::ToFloat_20_12(params[2]);
        m[3] = 0.0f;
        m[4] = DSFixed::ToFloat_20_12(params[3]);
        m[5] = DSFixed::ToFloat_20_12(params[4]);
        m[6] = DSFixed::ToFloat_20_12(params[5]);
        m[7] = 0.0f;
        m[8] = DSFixed::ToFloat_20_12(params[6]);
        m[9] = DSFixed::ToFloat_20_12(params[7]);
        m[10] = DSFixed::ToFloat_20_12(params[8]);
        m[11] = 0.0f;
        m[12] = 0.0f;
        m[13] = 0.0f;
        m[14] = 0.0f;
        m[15] = 1.0f;
    }

    // Right-multiply: this = this × other
    void Multiply(const Mat4& other) {
        float result[16] = {};
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    sum += m[row * 4 + k] * other.m[k * 4 + col];
                }
                result[row * 4 + col] = sum;
            }
        }
        std::memcpy(m, result, sizeof(m));
    }

    // Transform a vertex: result = vertex × matrix (row-vector convention)
    Vec4f Transform(const Vec4f& v) const {
        return Vec4f(
            v.x * m[0] + v.y * m[4] + v.z * m[8]  + v.w * m[12],
            v.x * m[1] + v.y * m[5] + v.z * m[9]  + v.w * m[13],
            v.x * m[2] + v.y * m[6] + v.z * m[10] + v.w * m[14],
            v.x * m[3] + v.y * m[7] + v.z * m[11] + v.w * m[15]
        );
    }

    // Transform a 3D normal using the 3x3 upper-left submatrix
    Vec3f TransformNormal(const Vec3f& n) const {
        return Vec3f(
            n.x * m[0] + n.y * m[4] + n.z * m[8],
            n.x * m[1] + n.y * m[5] + n.z * m[9],
            n.x * m[2] + n.y * m[6] + n.z * m[10]
        );
    }

    // Apply scale
    void Scale(float sx, float sy, float sz) {
        Mat4 s;
        s.m[0] = sx; s.m[5] = sy; s.m[10] = sz;
        Multiply(s);
    }

    // Apply translation
    void Translate(float tx, float ty, float tz) {
        Mat4 t;
        t.m[12] = tx; t.m[13] = ty; t.m[14] = tz;
        Multiply(t);
    }

    // Access element by row, col
    float& at(int row, int col) { return m[row * 4 + col]; }
    float  at(int row, int col) const { return m[row * 4 + col]; }
};

// ============================================================================
// Geometry Vertex — Collected per frame for rendering
// ============================================================================
struct GXVertex {
    Vec4f position;     // Clip-space position (after matrix transform)
    Vec3f world_pos;    // World-space position (before clip matrix)
    float r = 1, g = 1, b = 1; // Vertex color [0,1]
    float s = 0, t = 0;        // Texture coordinates
    Vec3f normal;               // Transformed normal
};

// ============================================================================
// Geometry Polygon — References vertices in the per-frame vertex buffer
// ============================================================================
struct GXPolygon {
    uint32_t vertex_indices[4] = {}; // Up to 4 indices (quads)
    int      vertex_count = 0;       // 3 = triangle, 4 = quad
    uint32_t poly_attr = 0;          // Raw polygon attributes
    uint32_t tex_param = 0;          // Raw texture parameters
    uint16_t pltt_base = 0;          // Palette base address

    // Decoded polygon attributes
    uint8_t  alpha() const { return (poly_attr >> 16) & 0x1F; }
    uint8_t  polygon_id() const { return (poly_attr >> 24) & 0x3F; }
    bool     backface_cull() const { return !((poly_attr >> 6) & 1); }
    bool     frontface_cull() const { return !((poly_attr >> 7) & 1); }
};

// ============================================================================
// Polygon Primitive Types (BEGIN_VTXS parameter)
// ============================================================================
enum class PolygonType : uint8_t {
    Triangles = 0,     // 3 vertices per polygon
    Quads     = 1,     // 4 vertices per polygon
    TriStrip  = 2,     // Triangle strip
    QuadStrip = 3      // Quad strip
};

// ============================================================================
// Matrix Mode (MTX_MODE parameter)
// ============================================================================
enum class MatrixMode : uint8_t {
    Projection      = 0, // 1-deep stack
    Position        = 1, // 31-deep stack
    PositionVector  = 2, // 31-deep stack (both position + direction)
    Texture         = 3  // 1-deep stack
};

// ============================================================================
// GX Command IDs (per GBATEK, also = (port_addr - 0x04000400) / 4)
// ============================================================================
namespace GXCmd {
    constexpr uint8_t MTX_MODE       = 0x10;
    constexpr uint8_t MTX_PUSH       = 0x11;
    constexpr uint8_t MTX_POP        = 0x12;
    constexpr uint8_t MTX_STORE      = 0x13;
    constexpr uint8_t MTX_RESTORE    = 0x14;
    constexpr uint8_t MTX_IDENTITY   = 0x15;
    constexpr uint8_t MTX_LOAD_4x4   = 0x16;
    constexpr uint8_t MTX_LOAD_4x3   = 0x17;
    constexpr uint8_t MTX_MULT_4x4   = 0x18;
    constexpr uint8_t MTX_MULT_4x3   = 0x19;
    constexpr uint8_t MTX_MULT_3x3   = 0x1A;
    constexpr uint8_t MTX_SCALE      = 0x1B;
    constexpr uint8_t MTX_TRANS      = 0x1C;
    constexpr uint8_t COLOR          = 0x20;
    constexpr uint8_t NORMAL         = 0x21;
    constexpr uint8_t TEXCOORD       = 0x22;
    constexpr uint8_t VTX_16         = 0x23;
    constexpr uint8_t VTX_10         = 0x24;
    constexpr uint8_t VTX_XY         = 0x25;
    constexpr uint8_t VTX_XZ         = 0x26;
    constexpr uint8_t VTX_YZ         = 0x27;
    constexpr uint8_t VTX_DIFF       = 0x28;
    constexpr uint8_t POLYGON_ATTR   = 0x29;
    constexpr uint8_t TEXIMAGE_PARAM = 0x2A;
    constexpr uint8_t PLTT_BASE      = 0x2B;
    constexpr uint8_t DIF_AMB        = 0x30;
    constexpr uint8_t SPE_EMI        = 0x31;
    constexpr uint8_t LIGHT_VECTOR   = 0x32;
    constexpr uint8_t LIGHT_COLOR    = 0x33;
    constexpr uint8_t SHININESS      = 0x34;
    constexpr uint8_t BEGIN_VTXS     = 0x40;
    constexpr uint8_t END_VTXS       = 0x41;
    constexpr uint8_t SWAP_BUFFERS   = 0x50;
    constexpr uint8_t VIEWPORT       = 0x60;
    constexpr uint8_t BOX_TEST       = 0x70;
    constexpr uint8_t POS_TEST       = 0x71;
    constexpr uint8_t VEC_TEST       = 0x72;
    constexpr uint8_t NOP            = 0x00;

    // Returns the number of 32-bit parameters a command expects.
    inline int GetParamCount(uint8_t cmd) {
        switch (cmd) {
            case MTX_MODE:       return 1;
            case MTX_PUSH:       return 0;
            case MTX_POP:        return 1;
            case MTX_STORE:      return 1;
            case MTX_RESTORE:    return 1;
            case MTX_IDENTITY:   return 0;
            case MTX_LOAD_4x4:   return 16;
            case MTX_LOAD_4x3:   return 12;
            case MTX_MULT_4x4:   return 16;
            case MTX_MULT_4x3:   return 12;
            case MTX_MULT_3x3:   return 9;
            case MTX_SCALE:      return 3;
            case MTX_TRANS:      return 3;
            case COLOR:          return 1;
            case NORMAL:         return 1;
            case TEXCOORD:       return 1;
            case VTX_16:         return 2;
            case VTX_10:         return 1;
            case VTX_XY:         return 1;
            case VTX_XZ:         return 1;
            case VTX_YZ:         return 1;
            case VTX_DIFF:       return 1;
            case POLYGON_ATTR:   return 1;
            case TEXIMAGE_PARAM: return 1;
            case PLTT_BASE:      return 1;
            case DIF_AMB:        return 1;
            case SPE_EMI:        return 1;
            case LIGHT_VECTOR:   return 1;
            case LIGHT_COLOR:    return 1;
            case SHININESS:      return 32;
            case BEGIN_VTXS:     return 1;
            case END_VTXS:       return 0;
            case SWAP_BUFFERS:   return 1;
            case VIEWPORT:       return 1;
            case BOX_TEST:       return 3;
            case POS_TEST:       return 2;
            case VEC_TEST:       return 1;
            case NOP:            return 0;
            default:             return 0;
        }
    }
}

// ============================================================================
// Abstract Rendering Backend
//
// Implement this interface to receive per-frame geometry for Vulkan/OpenGL.
// On SWAP_BUFFERS, the GXEngine calls SubmitFrame() with the collected
// vertex and polygon buffers.
// ============================================================================
class GXRenderer {
public:
    virtual ~GXRenderer() = default;
    virtual void SubmitFrame(const std::vector<GXVertex>& vertices,
                             const std::vector<GXPolygon>& polygons) = 0;
};

// ============================================================================
// DS 3D Geometry Engine
// ============================================================================
class GXEngine {
public:
    // ---- DS Hardware Limits ----
    static constexpr int MAX_VERTICES = 6144;
    static constexpr int MAX_POLYGONS = 2048;
    static constexpr int POSITION_STACK_DEPTH = 31;
    static constexpr int PROJECTION_STACK_DEPTH = 1;
    static constexpr int TEXTURE_STACK_DEPTH = 1;

    // ---- Matrix State ----
    MatrixMode matrix_mode = MatrixMode::Projection;

    Mat4 projection_matrix;
    Mat4 position_matrix;
    Mat4 vector_matrix;  // Direction/normal matrix (3x3 submatrix used)
    Mat4 texture_matrix;

    // Matrix stacks
    Mat4 projection_stack[1];
    int  projection_sp = 0;
    Mat4 position_stack[31];
    Mat4 vector_stack[31];
    int  position_sp = 0;
    Mat4 texture_stack_storage[1];
    int  texture_sp = 0;

    // Clip matrix = projection × position (cached, recomputed when dirty)
    Mat4 clip_matrix;
    bool clip_dirty = true;

    // ---- Vertex State ----
    PolygonType current_poly_type = PolygonType::Triangles;
    bool        in_vertex_list = false;
    Vec3f       last_vtx_pos;              // For VTX_XY/XZ/YZ/DIFF
    float       current_color_r = 1.0f;
    float       current_color_g = 1.0f;
    float       current_color_b = 1.0f;
    float       current_texcoord_s = 0.0f;
    float       current_texcoord_t = 0.0f;
    Vec3f       current_normal;
    int         vtx_count_in_primitive = 0; // Vertices since BEGIN_VTXS

    // ---- Polygon Attributes ----
    uint32_t poly_attr_pending = 0; // Set by POLYGON_ATTR, applied at BEGIN_VTXS
    uint32_t poly_attr_current = 0; // Active during vertex list
    uint32_t tex_param = 0;
    uint16_t pltt_base = 0;

    // ---- Lighting ----
    Vec3f    light_vectors[4];
    uint16_t light_colors[4] = {};
    uint16_t diffuse_color  = 0;
    uint16_t ambient_color  = 0;
    uint16_t specular_color = 0;
    uint16_t emission_color = 0;
    bool     shininess_enabled = false;
    uint8_t  shininess_table[128] = {};

    // ---- Viewport ----
    int viewport_x1 = 0, viewport_y1 = 0;
    int viewport_x2 = 255, viewport_y2 = 191;

    // ---- Per-Frame Geometry Buffers ----
    std::vector<GXVertex>  vertex_buffer;
    std::vector<GXPolygon> polygon_buffer;

    // ---- GXFIFO Packed Command State ----
    uint8_t  fifo_packed_cmds[4] = {};
    int      fifo_cmd_index = 0;
    int      fifo_cmd_count = 0;
    bool     fifo_processing = false;

    // ---- Command Parameter Accumulator ----
    uint8_t  pending_cmd = 0;
    int      params_remaining = 0;
    std::vector<uint32_t> param_buffer;

    // ---- Status Flags ----
    bool matrix_stack_error = false;
    int  swap_buffer_count = 0; // Number of SWAP_BUFFERS calls (frame counter)

    // ---- Rendering Backend ----
    GXRenderer* renderer = nullptr;

    // ---- 3D Display Control (0x04000060) ----
    uint32_t disp3dcnt = 0;

    // ---- Clear Buffers ----
    uint32_t clear_color = 0; // 0x04000350
    uint16_t clear_depth = 0x7FFF; // 0x04000354

    // ---- Native Hardware Lookup Tables ----
    uint8_t  fog_density_table[32] = {0}; // 0x04000360
    uint16_t fog_offset = 0;              // 0x0400036C
    uint16_t toon_table[32] = {0};        // 0x04000380

    GXEngine();

    // ---- Primary IO Interface ----
    // Called by NDSMemory when the game writes to GX command ports.
    void WriteCommandPort(uint32_t address, uint32_t value);
    void WriteGXFIFO(uint32_t value);
    uint32_t ReadRegister(uint32_t address) const;

    // ---- Clip Matrix ----
    const Mat4& GetClipMatrix();

    // ---- Frame Management ----
    uint32_t GetVertexCount() const { return static_cast<uint32_t>(vertex_buffer.size()); }
    uint32_t GetPolygonCount() const { return static_cast<uint32_t>(polygon_buffer.size()); }

private:
    // ---- Command Dispatch ----
    void ExecuteCommand(uint8_t cmd, const uint32_t* params, int param_count);

    // ---- Matrix Commands ----
    void Cmd_MTX_MODE(const uint32_t* params);
    void Cmd_MTX_PUSH();
    void Cmd_MTX_POP(const uint32_t* params);
    void Cmd_MTX_STORE(const uint32_t* params);
    void Cmd_MTX_RESTORE(const uint32_t* params);
    void Cmd_MTX_IDENTITY();
    void Cmd_MTX_LOAD_4x4(const uint32_t* params);
    void Cmd_MTX_LOAD_4x3(const uint32_t* params);
    void Cmd_MTX_MULT_4x4(const uint32_t* params);
    void Cmd_MTX_MULT_4x3(const uint32_t* params);
    void Cmd_MTX_MULT_3x3(const uint32_t* params);
    void Cmd_MTX_SCALE(const uint32_t* params);
    void Cmd_MTX_TRANS(const uint32_t* params);

    // ---- Vertex/Polygon Commands ----
    void Cmd_COLOR(const uint32_t* params);
    void Cmd_NORMAL(const uint32_t* params);
    void Cmd_TEXCOORD(const uint32_t* params);
    void Cmd_VTX_16(const uint32_t* params);
    void Cmd_VTX_10(const uint32_t* params);
    void Cmd_VTX_XY(const uint32_t* params);
    void Cmd_VTX_XZ(const uint32_t* params);
    void Cmd_VTX_YZ(const uint32_t* params);
    void Cmd_VTX_DIFF(const uint32_t* params);
    void Cmd_POLYGON_ATTR(const uint32_t* params);
    void Cmd_TEXIMAGE_PARAM(const uint32_t* params);
    void Cmd_PLTT_BASE(const uint32_t* params);
    void Cmd_BEGIN_VTXS(const uint32_t* params);
    void Cmd_END_VTXS();
    void Cmd_SWAP_BUFFERS(const uint32_t* params);
    void Cmd_VIEWPORT(const uint32_t* params);

    // ---- Lighting Commands ----
    void Cmd_DIF_AMB(const uint32_t* params);
    void Cmd_SPE_EMI(const uint32_t* params);
    void Cmd_LIGHT_VECTOR(const uint32_t* params);
    void Cmd_LIGHT_COLOR(const uint32_t* params);
    void Cmd_SHININESS(const uint32_t* params);

    // ---- Internal Helpers ----
    void SubmitVertex(float x, float y, float z);
    void FormPolygon();
    Mat4* GetCurrentMatrix();
    void  MarkClipDirty();

    // ---- GXFIFO Helpers ----
    void ProcessPendingFIFO();
    void FeedParameter(uint32_t value);

    // --- Phase 4 State Math Handlers ---
    void SortTranslucentPolygons();
    void RenderWBuffer();
    void MarkEdges();
    void ApplyAntiAliasing();
    void CalculateFog();
    void ApplyToonTable();
    void CalculateNormalMatrix();
};
