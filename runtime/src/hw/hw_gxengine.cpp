#include "hw_gxengine.h"
#include "vk_renderer.h"
#include <atomic>

std::atomic<uint64_t> g_debug_gx_swap_count{0};
std::atomic<uint32_t> g_debug_gx_last_vertex_count{0};
std::atomic<uint32_t> g_debug_gx_last_polygon_count{0};

// ============================================================================
// hw_gxengine.cpp — Nintendo DS 3D Geometry Engine Implementation
// ============================================================================

GXEngine::GXEngine() {
    vertex_buffer.reserve(MAX_VERTICES);
    polygon_buffer.reserve(MAX_POLYGONS);
    param_buffer.reserve(32); // Largest command is SHININESS (32 params)
}

// ============================================================================
// IO Interface — Called from NDSMemory's HandleHardwareWrite
// ============================================================================

void GXEngine::WriteCommandPort(uint32_t address, uint32_t value) {
    // GXFIFO packed command interface
    if (address == 0x04000400) {
        WriteGXFIFO(value);
        return;
    }

    // DISP3DCNT (0x04000060)
    if (address == 0x04000060) {
        disp3dcnt = value;
        return;
    }

    // Clear color/depth
    if (address == 0x04000350) { clear_color = value; return; }
    if (address == 0x04000354) { clear_depth = static_cast<uint16_t>(value); return; }

    // Direct command port: address → command ID
    if (address >= 0x04000440 && address <= 0x040005CC) {
        uint8_t cmd = static_cast<uint8_t>((address - 0x04000400) / 4);
        int expected = GXCmd::GetParamCount(cmd);

        if (expected == 0) {
            // Zero-parameter command — execute immediately
            ExecuteCommand(cmd, nullptr, 0);
        } else if (pending_cmd == 0 || pending_cmd != cmd) {
            // Start a new multi-param command
            pending_cmd = cmd;
            params_remaining = expected;
            param_buffer.clear();
            param_buffer.push_back(value);
            params_remaining--;
            if (params_remaining == 0) {
                ExecuteCommand(pending_cmd, param_buffer.data(),
                              static_cast<int>(param_buffer.size()));
                pending_cmd = 0;
            }
        } else {
            // Continue buffering parameters for the same command
            param_buffer.push_back(value);
            params_remaining--;
            if (params_remaining == 0) {
                ExecuteCommand(pending_cmd, param_buffer.data(),
                              static_cast<int>(param_buffer.size()));
                pending_cmd = 0;
            }
        }
        return;
    }
}

void GXEngine::WriteGXFIFO(uint32_t value) {
    if (!fifo_processing) {
        // This is a new packed command header
        fifo_packed_cmds[0] = (value >> 0) & 0xFF;
        fifo_packed_cmds[1] = (value >> 8) & 0xFF;
        fifo_packed_cmds[2] = (value >> 16) & 0xFF;
        fifo_packed_cmds[3] = (value >> 24) & 0xFF;

        // Count non-NOP commands
        fifo_cmd_count = 0;
        for (int i = 0; i < 4; ++i) {
            if (fifo_packed_cmds[i] != GXCmd::NOP) {
                fifo_cmd_count = i + 1;
            }
        }

        if (fifo_cmd_count == 0) return; // All NOPs

        fifo_cmd_index = 0;
        fifo_processing = true;

        // Start collecting params for the first command
        pending_cmd = fifo_packed_cmds[0];
        params_remaining = GXCmd::GetParamCount(pending_cmd);
        param_buffer.clear();

        // If first command needs 0 params, execute it and advance
        ProcessPendingFIFO();
    } else {
        // We're collecting parameters for a packed command
        FeedParameter(value);
    }
}

void GXEngine::FeedParameter(uint32_t value) {
    param_buffer.push_back(value);
    params_remaining--;

    if (params_remaining == 0) {
        // Execute the command
        ExecuteCommand(pending_cmd, param_buffer.data(),
                      static_cast<int>(param_buffer.size()));
        pending_cmd = 0;
        param_buffer.clear();

        // Advance to next command in packed header
        fifo_cmd_index++;
        ProcessPendingFIFO();
    }
}

void GXEngine::ProcessPendingFIFO() {
    while (fifo_processing && fifo_cmd_index < fifo_cmd_count) {
        uint8_t cmd = fifo_packed_cmds[fifo_cmd_index];
        if (cmd == GXCmd::NOP) {
            fifo_cmd_index++;
            continue;
        }

        int expected = GXCmd::GetParamCount(cmd);
        if (expected == 0) {
            ExecuteCommand(cmd, nullptr, 0);
            fifo_cmd_index++;
            continue;
        }

        // Need parameters — set up and wait
        pending_cmd = cmd;
        params_remaining = expected;
        param_buffer.clear();
        return; // Will continue when more params arrive via WriteGXFIFO
    }

    // All commands in the packed header have been processed
    fifo_processing = false;
    pending_cmd = 0;
}

// ============================================================================
// Register Read — GXSTAT, RAM_COUNT, Clip Matrix, Vector Matrix
// ============================================================================

uint32_t GXEngine::ReadRegister(uint32_t address) const {
    // DISP3DCNT
    if (address == 0x04000060) return disp3dcnt;

    // GXSTAT (0x04000600)
    if (address == 0x04000600) {
        uint32_t stat = 0;
        // Bits 8-12: Position stack pointer
        stat |= (static_cast<uint32_t>(position_sp) & 0x1F) << 8;
        // Bit 13: Projection stack pointer
        stat |= (static_cast<uint32_t>(projection_sp) & 0x1) << 13;
        // Bit 15: Matrix stack error
        if (matrix_stack_error) stat |= (1u << 15);
        // Bits 16-24: FIFO entries (always 0 — we process instantly)
        // Bit 25: FIFO less than half full (always 1)
        stat |= (1u << 25);
        // Bit 26: FIFO empty (always 1)
        stat |= (1u << 26);
        // Bit 27: Geometry engine busy (always 0)
        return stat;
    }

    // RAM_COUNT (0x04000604)
    if (address == 0x04000604) {
        uint32_t count = 0;
        count |= (GetPolygonCount() & 0xFFF);         // Bits 0-11: polygon count
        count |= (GetVertexCount() & 0x1FFF) << 16;   // Bits 16-28: vertex count
        return count;
    }

    // Clear registers
    if (address == 0x04000350) return clear_color;
    if (address == 0x04000354) return clear_depth;

    // Clip Matrix (0x04000640 - 0x0400067F, 16 × 4 bytes)
    if (address >= 0x04000640 && address <= 0x0400067F) {
        int index = (address - 0x04000640) / 4;
        if (index < 16) {
            // Convert float back to DS 20.12 fixed-point for readback
            int32_t fixed = static_cast<int32_t>(
                const_cast<GXEngine*>(this)->GetClipMatrix().m[index] * 4096.0f);
            return static_cast<uint32_t>(fixed);
        }
    }

    // Direction/Vector Matrix (0x04000680 - 0x040006A3, 9 × 4 bytes = 3x3)
    if (address >= 0x04000680 && address <= 0x040006A3) {
        int index = (address - 0x04000680) / 4;
        // Map 3x3 index to 4x4 matrix position
        int row = index / 3;
        int col = index % 3;
        if (row < 3 && col < 3) {
            int32_t fixed = static_cast<int32_t>(vector_matrix.m[row * 4 + col] * 4096.0f);
            return static_cast<uint32_t>(fixed);
        }
    }

    return 0;
}

// ============================================================================
// Clip Matrix Cache
// ============================================================================

const Mat4& GXEngine::GetClipMatrix() {
    if (clip_dirty) {
        clip_matrix = projection_matrix;
        clip_matrix.Multiply(position_matrix);
        clip_dirty = false;
    }
    return clip_matrix;
}

Mat4* GXEngine::GetCurrentMatrix() {
    switch (matrix_mode) {
        case MatrixMode::Projection:     return &projection_matrix;
        case MatrixMode::Position:        return &position_matrix;
        case MatrixMode::PositionVector:  return &position_matrix;
        case MatrixMode::Texture:         return &texture_matrix;
        default: return &position_matrix;
    }
}

void GXEngine::MarkClipDirty() {
    if (matrix_mode == MatrixMode::Projection ||
        matrix_mode == MatrixMode::Position ||
        matrix_mode == MatrixMode::PositionVector) {
        clip_dirty = true;
    }
}

// ============================================================================
// Command Dispatch
// ============================================================================

void GXEngine::ExecuteCommand(uint8_t cmd, const uint32_t* params, int /*param_count*/) {
    switch (cmd) {
        case GXCmd::MTX_MODE:       Cmd_MTX_MODE(params); break;
        case GXCmd::MTX_PUSH:       Cmd_MTX_PUSH(); break;
        case GXCmd::MTX_POP:        Cmd_MTX_POP(params); break;
        case GXCmd::MTX_STORE:      Cmd_MTX_STORE(params); break;
        case GXCmd::MTX_RESTORE:    Cmd_MTX_RESTORE(params); break;
        case GXCmd::MTX_IDENTITY:   Cmd_MTX_IDENTITY(); break;
        case GXCmd::MTX_LOAD_4x4:   Cmd_MTX_LOAD_4x4(params); break;
        case GXCmd::MTX_LOAD_4x3:   Cmd_MTX_LOAD_4x3(params); break;
        case GXCmd::MTX_MULT_4x4:   Cmd_MTX_MULT_4x4(params); break;
        case GXCmd::MTX_MULT_4x3:   Cmd_MTX_MULT_4x3(params); break;
        case GXCmd::MTX_MULT_3x3:   Cmd_MTX_MULT_3x3(params); break;
        case GXCmd::MTX_SCALE:      Cmd_MTX_SCALE(params); break;
        case GXCmd::MTX_TRANS:      Cmd_MTX_TRANS(params); break;
        case GXCmd::COLOR:          Cmd_COLOR(params); break;
        case GXCmd::NORMAL:         Cmd_NORMAL(params); break;
        case GXCmd::TEXCOORD:       Cmd_TEXCOORD(params); break;
        case GXCmd::VTX_16:         Cmd_VTX_16(params); break;
        case GXCmd::VTX_10:         Cmd_VTX_10(params); break;
        case GXCmd::VTX_XY:         Cmd_VTX_XY(params); break;
        case GXCmd::VTX_XZ:         Cmd_VTX_XZ(params); break;
        case GXCmd::VTX_YZ:         Cmd_VTX_YZ(params); break;
        case GXCmd::VTX_DIFF:       Cmd_VTX_DIFF(params); break;
        case GXCmd::POLYGON_ATTR:   Cmd_POLYGON_ATTR(params); break;
        case GXCmd::TEXIMAGE_PARAM: Cmd_TEXIMAGE_PARAM(params); break;
        case GXCmd::PLTT_BASE:      Cmd_PLTT_BASE(params); break;
        case GXCmd::DIF_AMB:        Cmd_DIF_AMB(params); break;
        case GXCmd::SPE_EMI:        Cmd_SPE_EMI(params); break;
        case GXCmd::LIGHT_VECTOR:   Cmd_LIGHT_VECTOR(params); break;
        case GXCmd::LIGHT_COLOR:    Cmd_LIGHT_COLOR(params); break;
        case GXCmd::SHININESS:      Cmd_SHININESS(params); break;
        case GXCmd::BEGIN_VTXS:     Cmd_BEGIN_VTXS(params); break;
        case GXCmd::END_VTXS:       Cmd_END_VTXS(); break;
        case GXCmd::SWAP_BUFFERS:   Cmd_SWAP_BUFFERS(params); break;
        case GXCmd::VIEWPORT:       Cmd_VIEWPORT(params); break;
        default: break;
    }
}

// ============================================================================
// Matrix Commands
// ============================================================================

void GXEngine::Cmd_MTX_MODE(const uint32_t* params) {
    matrix_mode = static_cast<MatrixMode>(params[0] & 0x3);
}

void GXEngine::Cmd_MTX_PUSH() {
    switch (matrix_mode) {
        case MatrixMode::Projection:
            if (projection_sp < PROJECTION_STACK_DEPTH) {
                projection_stack[projection_sp] = projection_matrix;
                projection_sp++;
            } else {
                matrix_stack_error = true;
            }
            break;
        case MatrixMode::Position:
        case MatrixMode::PositionVector:
            if (position_sp < POSITION_STACK_DEPTH) {
                position_stack[position_sp] = position_matrix;
                vector_stack[position_sp] = vector_matrix;
                position_sp++;
            } else {
                matrix_stack_error = true;
            }
            break;
        case MatrixMode::Texture:
            if (texture_sp < TEXTURE_STACK_DEPTH) {
                texture_stack_storage[texture_sp] = texture_matrix;
                texture_sp++;
            } else {
                matrix_stack_error = true;
            }
            break;
    }
}

void GXEngine::Cmd_MTX_POP(const uint32_t* params) {
    int count = static_cast<int>(params[0] & 0x3F);
    if (count == 0) count = 1; // Pop at least 1

    switch (matrix_mode) {
        case MatrixMode::Projection:
            projection_sp -= count;
            if (projection_sp < 0) { projection_sp = 0; matrix_stack_error = true; }
            projection_matrix = projection_stack[projection_sp];
            MarkClipDirty();
            break;
        case MatrixMode::Position:
        case MatrixMode::PositionVector:
            position_sp -= count;
            if (position_sp < 0) { position_sp = 0; matrix_stack_error = true; }
            position_matrix = position_stack[position_sp];
            vector_matrix = vector_stack[position_sp];
            MarkClipDirty();
            break;
        case MatrixMode::Texture:
            texture_sp -= count;
            if (texture_sp < 0) { texture_sp = 0; matrix_stack_error = true; }
            texture_matrix = texture_stack_storage[texture_sp];
            break;
    }
}

void GXEngine::Cmd_MTX_STORE(const uint32_t* params) {
    int index = static_cast<int>(params[0] & 0x1F);
    switch (matrix_mode) {
        case MatrixMode::Projection:
            if (index < PROJECTION_STACK_DEPTH) projection_stack[index] = projection_matrix;
            break;
        case MatrixMode::Position:
        case MatrixMode::PositionVector:
            if (index < POSITION_STACK_DEPTH) {
                position_stack[index] = position_matrix;
                vector_stack[index] = vector_matrix;
            }
            break;
        case MatrixMode::Texture:
            if (index < TEXTURE_STACK_DEPTH) texture_stack_storage[index] = texture_matrix;
            break;
    }
}

void GXEngine::Cmd_MTX_RESTORE(const uint32_t* params) {
    int index = static_cast<int>(params[0] & 0x1F);
    switch (matrix_mode) {
        case MatrixMode::Projection:
            if (index < PROJECTION_STACK_DEPTH) {
                projection_matrix = projection_stack[index];
                MarkClipDirty();
            }
            break;
        case MatrixMode::Position:
        case MatrixMode::PositionVector:
            if (index < POSITION_STACK_DEPTH) {
                position_matrix = position_stack[index];
                vector_matrix = vector_stack[index];
                MarkClipDirty();
            }
            break;
        case MatrixMode::Texture:
            if (index < TEXTURE_STACK_DEPTH) texture_matrix = texture_stack_storage[index];
            break;
    }
}

void GXEngine::Cmd_MTX_IDENTITY() {
    GetCurrentMatrix()->Identity();
    if (matrix_mode == MatrixMode::PositionVector) {
        vector_matrix.Identity();
    }
    MarkClipDirty();
}

void GXEngine::Cmd_MTX_LOAD_4x4(const uint32_t* params) {
    GetCurrentMatrix()->LoadFromFixed4x4(params);
    if (matrix_mode == MatrixMode::PositionVector) {
        vector_matrix.LoadFromFixed4x4(params);
    }
    MarkClipDirty();
}

void GXEngine::Cmd_MTX_LOAD_4x3(const uint32_t* params) {
    GetCurrentMatrix()->LoadFromFixed4x3(params);
    if (matrix_mode == MatrixMode::PositionVector) {
        vector_matrix.LoadFromFixed4x3(params);
    }
    MarkClipDirty();
}

void GXEngine::Cmd_MTX_MULT_4x4(const uint32_t* params) {
    Mat4 loaded;
    loaded.LoadFromFixed4x4(params);
    GetCurrentMatrix()->Multiply(loaded);
    if (matrix_mode == MatrixMode::PositionVector) {
        vector_matrix.Multiply(loaded);
    }
    MarkClipDirty();
}

void GXEngine::Cmd_MTX_MULT_4x3(const uint32_t* params) {
    Mat4 loaded;
    loaded.LoadFromFixed4x3(params);
    GetCurrentMatrix()->Multiply(loaded);
    if (matrix_mode == MatrixMode::PositionVector) {
        vector_matrix.Multiply(loaded);
    }
    MarkClipDirty();
}

void GXEngine::Cmd_MTX_MULT_3x3(const uint32_t* params) {
    Mat4 loaded;
    loaded.LoadFromFixed3x3(params);
    GetCurrentMatrix()->Multiply(loaded);
    if (matrix_mode == MatrixMode::PositionVector) {
        vector_matrix.Multiply(loaded);
    }
    MarkClipDirty();
}

void GXEngine::Cmd_MTX_SCALE(const uint32_t* params) {
    float sx = DSFixed::ToFloat_20_12(params[0]);
    float sy = DSFixed::ToFloat_20_12(params[1]);
    float sz = DSFixed::ToFloat_20_12(params[2]);
    GetCurrentMatrix()->Scale(sx, sy, sz);
    // NOTE: MTX_SCALE does NOT affect the vector matrix (per GBATEK)
    MarkClipDirty();
}

void GXEngine::Cmd_MTX_TRANS(const uint32_t* params) {
    float tx = DSFixed::ToFloat_20_12(params[0]);
    float ty = DSFixed::ToFloat_20_12(params[1]);
    float tz = DSFixed::ToFloat_20_12(params[2]);
    GetCurrentMatrix()->Translate(tx, ty, tz);
    if (matrix_mode == MatrixMode::PositionVector) {
        vector_matrix.Translate(tx, ty, tz);
    }
    MarkClipDirty();
}

// ============================================================================
// Vertex / Polygon Commands
// ============================================================================

void GXEngine::Cmd_COLOR(const uint32_t* params) {
    uint16_t rgb555 = static_cast<uint16_t>(params[0] & 0x7FFF);
    DSFixed::RGB555ToFloat(rgb555, current_color_r, current_color_g, current_color_b);
}

void GXEngine::Cmd_NORMAL(const uint32_t* params) {
    float nx = DSFixed::ToFloat_s1_0_9((params[0] >> 0)  & 0x3FF);
    float ny = DSFixed::ToFloat_s1_0_9((params[0] >> 10) & 0x3FF);
    float nz = DSFixed::ToFloat_s1_0_9((params[0] >> 20) & 0x3FF);
    current_normal = vector_matrix.TransformNormal(Vec3f(nx, ny, nz));
}

void GXEngine::Cmd_TEXCOORD(const uint32_t* params) {
    current_texcoord_s = DSFixed::ToFloat_s12_4(static_cast<uint16_t>(params[0] & 0xFFFF));
    current_texcoord_t = DSFixed::ToFloat_s12_4(static_cast<uint16_t>((params[0] >> 16) & 0xFFFF));
}

void GXEngine::Cmd_VTX_16(const uint32_t* params) {
    float x = DSFixed::ToFloat_s4_12(static_cast<uint16_t>(params[0] & 0xFFFF));
    float y = DSFixed::ToFloat_s4_12(static_cast<uint16_t>((params[0] >> 16) & 0xFFFF));
    float z = DSFixed::ToFloat_s4_12(static_cast<uint16_t>(params[1] & 0xFFFF));
    SubmitVertex(x, y, z);
}

void GXEngine::Cmd_VTX_10(const uint32_t* params) {
    float x = DSFixed::ToFloat_s1_3_6((params[0] >> 0)  & 0x3FF);
    float y = DSFixed::ToFloat_s1_3_6((params[0] >> 10) & 0x3FF);
    float z = DSFixed::ToFloat_s1_3_6((params[0] >> 20) & 0x3FF);
    SubmitVertex(x, y, z);
}

void GXEngine::Cmd_VTX_XY(const uint32_t* params) {
    float x = DSFixed::ToFloat_s4_12(static_cast<uint16_t>(params[0] & 0xFFFF));
    float y = DSFixed::ToFloat_s4_12(static_cast<uint16_t>((params[0] >> 16) & 0xFFFF));
    SubmitVertex(x, y, last_vtx_pos.z);
}

void GXEngine::Cmd_VTX_XZ(const uint32_t* params) {
    float x = DSFixed::ToFloat_s4_12(static_cast<uint16_t>(params[0] & 0xFFFF));
    float z = DSFixed::ToFloat_s4_12(static_cast<uint16_t>((params[0] >> 16) & 0xFFFF));
    SubmitVertex(x, last_vtx_pos.y, z);
}

void GXEngine::Cmd_VTX_YZ(const uint32_t* params) {
    float y = DSFixed::ToFloat_s4_12(static_cast<uint16_t>(params[0] & 0xFFFF));
    float z = DSFixed::ToFloat_s4_12(static_cast<uint16_t>((params[0] >> 16) & 0xFFFF));
    SubmitVertex(last_vtx_pos.x, y, z);
}

void GXEngine::Cmd_VTX_DIFF(const uint32_t* params) {
    float dx = DSFixed::ToFloat_vtx_diff((params[0] >> 0)  & 0x3FF);
    float dy = DSFixed::ToFloat_vtx_diff((params[0] >> 10) & 0x3FF);
    float dz = DSFixed::ToFloat_vtx_diff((params[0] >> 20) & 0x3FF);
    SubmitVertex(last_vtx_pos.x + dx, last_vtx_pos.y + dy, last_vtx_pos.z + dz);
}

void GXEngine::Cmd_POLYGON_ATTR(const uint32_t* params) {
    poly_attr_pending = params[0];
}

void GXEngine::Cmd_TEXIMAGE_PARAM(const uint32_t* params) {
    tex_param = params[0];
}

void GXEngine::Cmd_PLTT_BASE(const uint32_t* params) {
    pltt_base = static_cast<uint16_t>(params[0] & 0x1FFF);
}

void GXEngine::Cmd_BEGIN_VTXS(const uint32_t* params) {
    current_poly_type = static_cast<PolygonType>(params[0] & 0x3);
    in_vertex_list = true;
    vtx_count_in_primitive = 0;
    poly_attr_current = poly_attr_pending; // Apply pending attributes
}

void GXEngine::Cmd_END_VTXS() {
    in_vertex_list = false;
}

void GXEngine::Cmd_SWAP_BUFFERS(const uint32_t* /*params*/) {
    // Apply fixed-function post-transform stages before backend submission.
    CalculateNormalMatrix();
    RenderWBuffer();
    ApplyToonTable();
    CalculateFog();
    MarkEdges();
    ApplyAntiAliasing();
    SortTranslucentPolygons();

    if (Backend::Vulkan::IsReady()) {
        Backend::Vulkan::UploadMatrixStack(projection_matrix, position_matrix);
        Backend::Vulkan::UploadFrameGeometry(vertex_buffer, polygon_buffer);
        Backend::Vulkan::ManageCommandBuffers();
        Backend::Vulkan::SubmitFrame();
    }

    // Submit geometry to the rendering backend
    if (renderer) {
        renderer->SubmitFrame(vertex_buffer, polygon_buffer);
    }

    g_debug_gx_swap_count.fetch_add(1, std::memory_order_relaxed);
    g_debug_gx_last_vertex_count.store(static_cast<uint32_t>(vertex_buffer.size()), std::memory_order_relaxed);
    g_debug_gx_last_polygon_count.store(static_cast<uint32_t>(polygon_buffer.size()), std::memory_order_relaxed);

    // Clear buffers for the next frame
    vertex_buffer.clear();
    polygon_buffer.clear();
    swap_buffer_count++;
}

void GXEngine::Cmd_VIEWPORT(const uint32_t* params) {
    viewport_x1 = (params[0] >> 0)  & 0xFF;
    viewport_y1 = (params[0] >> 8)  & 0xFF;
    viewport_x2 = (params[0] >> 16) & 0xFF;
    viewport_y2 = (params[0] >> 24) & 0xFF;
}

// ============================================================================
// Lighting Commands
// ============================================================================

void GXEngine::Cmd_DIF_AMB(const uint32_t* params) {
    diffuse_color = static_cast<uint16_t>(params[0] & 0x7FFF);
    ambient_color = static_cast<uint16_t>((params[0] >> 16) & 0x7FFF);

    // Bit 15: set vertex color to diffuse color
    if (params[0] & (1u << 15)) {
        DSFixed::RGB555ToFloat(diffuse_color, current_color_r, current_color_g, current_color_b);
    }
}

void GXEngine::Cmd_SPE_EMI(const uint32_t* params) {
    specular_color = static_cast<uint16_t>(params[0] & 0x7FFF);
    emission_color = static_cast<uint16_t>((params[0] >> 16) & 0x7FFF);
    shininess_enabled = (params[0] >> 15) & 1;
}

void GXEngine::Cmd_LIGHT_VECTOR(const uint32_t* params) {
    int light_id = (params[0] >> 30) & 0x3;
    float x = DSFixed::ToFloat_s1_0_9((params[0] >> 0)  & 0x3FF);
    float y = DSFixed::ToFloat_s1_0_9((params[0] >> 10) & 0x3FF);
    float z = DSFixed::ToFloat_s1_0_9((params[0] >> 20) & 0x3FF);
    // Transform light vector by the current direction matrix
    light_vectors[light_id] = vector_matrix.TransformNormal(Vec3f(x, y, z));
}

void GXEngine::Cmd_LIGHT_COLOR(const uint32_t* params) {
    int light_id = (params[0] >> 30) & 0x3;
    light_colors[light_id] = static_cast<uint16_t>(params[0] & 0x7FFF);
}

void GXEngine::Cmd_SHININESS(const uint32_t* params) {
    for (int i = 0; i < 32; ++i) {
        shininess_table[i * 4 + 0] = (params[i] >> 0) & 0xFF;
        shininess_table[i * 4 + 1] = (params[i] >> 8) & 0xFF;
        shininess_table[i * 4 + 2] = (params[i] >> 16) & 0xFF;
        shininess_table[i * 4 + 3] = (params[i] >> 24) & 0xFF;
    }
}

// ============================================================================
// Vertex Submission — Transforms and collects per-frame geometry
// ============================================================================

void GXEngine::SubmitVertex(float x, float y, float z) {
    if (!in_vertex_list) return;
    if (vertex_buffer.size() >= MAX_VERTICES) return;

    // Store world-space position for VTX_XY/XZ/YZ/DIFF
    last_vtx_pos = Vec3f(x, y, z);

    // Transform by clip matrix (projection × position)
    Vec4f world_pos(x, y, z, 1.0f);
    Vec4f clip_pos = GetClipMatrix().Transform(world_pos);

    // Build vertex
    GXVertex vtx;
    vtx.position = clip_pos;
    vtx.world_pos = Vec3f(x, y, z);
    vtx.r = current_color_r;
    vtx.g = current_color_g;
    vtx.b = current_color_b;
    vtx.s = current_texcoord_s;
    vtx.t = current_texcoord_t;
    vtx.normal = current_normal;

    vertex_buffer.push_back(vtx);
    vtx_count_in_primitive++;

    // Attempt to form a polygon based on the current primitive type
    FormPolygon();
}

void GXEngine::FormPolygon() {
    if (polygon_buffer.size() >= MAX_POLYGONS) return;

    uint32_t vtx_idx = static_cast<uint32_t>(vertex_buffer.size()) - 1;

    switch (current_poly_type) {
        case PolygonType::Triangles:
            // Every 3 vertices = 1 triangle
            if (vtx_count_in_primitive >= 3) {
                GXPolygon poly;
                poly.vertex_count = 3;
                poly.vertex_indices[0] = vtx_idx - 2;
                poly.vertex_indices[1] = vtx_idx - 1;
                poly.vertex_indices[2] = vtx_idx;
                poly.poly_attr = poly_attr_current;
                poly.tex_param = tex_param;
                poly.pltt_base = pltt_base;
                polygon_buffer.push_back(poly);
                vtx_count_in_primitive = 0;
            }
            break;

        case PolygonType::Quads:
            // Every 4 vertices = 1 quad
            if (vtx_count_in_primitive >= 4) {
                GXPolygon poly;
                poly.vertex_count = 4;
                poly.vertex_indices[0] = vtx_idx - 3;
                poly.vertex_indices[1] = vtx_idx - 2;
                poly.vertex_indices[2] = vtx_idx - 1;
                poly.vertex_indices[3] = vtx_idx;
                poly.poly_attr = poly_attr_current;
                poly.tex_param = tex_param;
                poly.pltt_base = pltt_base;
                polygon_buffer.push_back(poly);
                vtx_count_in_primitive = 0;
            }
            break;

        case PolygonType::TriStrip:
            // After the first 3 vertices, each new vertex forms a triangle
            // with the 2 previous vertices. Winding alternates.
            if (vtx_count_in_primitive >= 3) {
                GXPolygon poly;
                poly.vertex_count = 3;
                if (vtx_count_in_primitive % 2 == 1) {
                    // Odd: normal winding
                    poly.vertex_indices[0] = vtx_idx - 2;
                    poly.vertex_indices[1] = vtx_idx - 1;
                    poly.vertex_indices[2] = vtx_idx;
                } else {
                    // Even: reversed winding to maintain consistent face direction
                    poly.vertex_indices[0] = vtx_idx - 1;
                    poly.vertex_indices[1] = vtx_idx - 2;
                    poly.vertex_indices[2] = vtx_idx;
                }
                poly.poly_attr = poly_attr_current;
                poly.tex_param = tex_param;
                poly.pltt_base = pltt_base;
                polygon_buffer.push_back(poly);
            }
            break;

        case PolygonType::QuadStrip:
            // After the first 4 vertices, every 2 new vertices form a quad
            // with the 2 previous vertices.
            if (vtx_count_in_primitive >= 4 && vtx_count_in_primitive % 2 == 0) {
                GXPolygon poly;
                poly.vertex_count = 4;
                poly.vertex_indices[0] = vtx_idx - 3;
                poly.vertex_indices[1] = vtx_idx - 2;
                poly.vertex_indices[2] = vtx_idx;     // Note: DS quad strip ordering
                poly.vertex_indices[3] = vtx_idx - 1;
                poly.poly_attr = poly_attr_current;
                poly.tex_param = tex_param;
                poly.pltt_base = pltt_base;
                polygon_buffer.push_back(poly);
            }
            break;
    }
}

// ============================================================================
// Phase 4: Graphics Engine Hardware Math Operations
// ============================================================================

void GXEngine::SortTranslucentPolygons() {
    if (polygon_buffer.empty() || vertex_buffer.empty()) return;

    const bool z_sort_enabled = (disp3dcnt & (1 << 3)) != 0;

    auto polygon_depth = [this](const GXPolygon& poly) -> float {
        float accum = 0.0f;
        int count = 0;
        for (int i = 0; i < poly.vertex_count; ++i) {
            const uint32_t idx = poly.vertex_indices[i];
            if (idx >= vertex_buffer.size()) continue;
            const GXVertex& v = vertex_buffer[idx];
            const float w = std::fabs(v.position.w) > 1e-6f ? v.position.w : 1.0f;
            accum += (v.position.z / w);
            count++;
        }
        if (count == 0) return 0.0f;
        return accum / static_cast<float>(count);
    };

    auto polygon_average_y = [this](const GXPolygon& poly) -> float {
        float accum = 0.0f;
        int count = 0;
        for (int i = 0; i < poly.vertex_count; ++i) {
            const uint32_t idx = poly.vertex_indices[i];
            if (idx >= vertex_buffer.size()) continue;
            accum += vertex_buffer[idx].world_pos.y;
            count++;
        }
        if (count == 0) return 0.0f;
        return accum / static_cast<float>(count);
    };

    std::vector<GXPolygon> opaque;
    std::vector<GXPolygon> translucent;
    opaque.reserve(polygon_buffer.size());
    translucent.reserve(polygon_buffer.size());

    for (const GXPolygon& poly : polygon_buffer) {
        const uint8_t alpha = poly.alpha();
        if (alpha > 0 && alpha < 31) translucent.push_back(poly);
        else opaque.push_back(poly);
    }

    if (z_sort_enabled) {
        std::stable_sort(translucent.begin(), translucent.end(),
            [&](const GXPolygon& a, const GXPolygon& b) {
                // Draw far-to-near for translucency.
                return polygon_depth(a) > polygon_depth(b);
            });
    } else {
        std::stable_sort(translucent.begin(), translucent.end(),
            [&](const GXPolygon& a, const GXPolygon& b) {
                // Manual fallback sorting approximates DS Y-sorting behavior.
                return polygon_average_y(a) > polygon_average_y(b);
            });
    }

    polygon_buffer.clear();
    polygon_buffer.insert(polygon_buffer.end(), opaque.begin(), opaque.end());
    polygon_buffer.insert(polygon_buffer.end(), translucent.begin(), translucent.end());
}

void GXEngine::RenderWBuffer() {
    const bool w_buffer_enabled = (disp3dcnt & (1 << 2)) != 0;
    if (!w_buffer_enabled) return;

    for (GXVertex& vtx : vertex_buffer) {
        const float w = std::fabs(vtx.position.w) > 1e-6f ? std::fabs(vtx.position.w) : 1e-6f;
        const float depth = std::clamp(1.0f / w, 0.0f, 1.0f);
        vtx.position.z = depth;
    }
}

void GXEngine::MarkEdges() {
    const bool edge_marking_enabled = (disp3dcnt & (1 << 5)) != 0;
    if (!edge_marking_enabled) return;

    for (const GXPolygon& poly : polygon_buffer) {
        const float edge_strength = (static_cast<float>(poly.polygon_id()) + 1.0f) / 64.0f;
        for (int i = 0; i < poly.vertex_count; ++i) {
            const uint32_t idx = poly.vertex_indices[i];
            if (idx >= vertex_buffer.size()) continue;

            GXVertex& vtx = vertex_buffer[idx];
            vtx.r = std::clamp(std::max(vtx.r, edge_strength), 0.0f, 1.0f);
            vtx.g = std::clamp(vtx.g * 0.85f, 0.0f, 1.0f);
            vtx.b = std::clamp(vtx.b * 0.85f, 0.0f, 1.0f);
        }
    }
}

void GXEngine::ApplyAntiAliasing() {
    const bool antialiasing_enabled = (disp3dcnt & (1 << 4)) != 0;
    if (!antialiasing_enabled) return;

    for (const GXPolygon& poly : polygon_buffer) {
        float avg_r = 0.0f;
        float avg_g = 0.0f;
        float avg_b = 0.0f;
        int valid = 0;

        for (int i = 0; i < poly.vertex_count; ++i) {
            const uint32_t idx = poly.vertex_indices[i];
            if (idx >= vertex_buffer.size()) continue;
            avg_r += vertex_buffer[idx].r;
            avg_g += vertex_buffer[idx].g;
            avg_b += vertex_buffer[idx].b;
            valid++;
        }
        if (valid == 0) continue;

        avg_r /= static_cast<float>(valid);
        avg_g /= static_cast<float>(valid);
        avg_b /= static_cast<float>(valid);

        for (int i = 0; i < poly.vertex_count; ++i) {
            const uint32_t idx = poly.vertex_indices[i];
            if (idx >= vertex_buffer.size()) continue;

            GXVertex& vtx = vertex_buffer[idx];
            vtx.r = std::clamp(vtx.r * 0.75f + avg_r * 0.25f, 0.0f, 1.0f);
            vtx.g = std::clamp(vtx.g * 0.75f + avg_g * 0.25f, 0.0f, 1.0f);
            vtx.b = std::clamp(vtx.b * 0.75f + avg_b * 0.25f, 0.0f, 1.0f);
        }
    }
}

void GXEngine::CalculateFog() {
    const bool fog_enabled = (disp3dcnt & (1 << 7)) != 0;
    if (!fog_enabled) return;

    const uint32_t shift = (disp3dcnt >> 8) & 0xF;

    float fog_r = 0.0f;
    float fog_g = 0.0f;
    float fog_b = 0.0f;
    DSFixed::RGB555ToFloat(static_cast<uint16_t>(clear_color & 0x7FFF), fog_r, fog_g, fog_b);

    for (GXVertex& vtx : vertex_buffer) {
        const float w = std::fabs(vtx.position.w) > 1e-6f ? vtx.position.w : 1.0f;
        const float ndc_depth = std::clamp((vtx.position.z / w) * 0.5f + 0.5f, 0.0f, 1.0f);
        int32_t z_int = static_cast<int32_t>(ndc_depth * 32767.0f);

        int32_t scaled_depth = z_int << shift;
        int32_t idx = (scaled_depth >> 10) + (fog_offset >> 10);
        idx = std::clamp(idx, 0, 31);

        const float fog_factor = static_cast<float>(fog_density_table[idx]) / 127.0f;
        vtx.r = std::clamp(vtx.r * (1.0f - fog_factor) + fog_r * fog_factor, 0.0f, 1.0f);
        vtx.g = std::clamp(vtx.g * (1.0f - fog_factor) + fog_g * fog_factor, 0.0f, 1.0f);
        vtx.b = std::clamp(vtx.b * (1.0f - fog_factor) + fog_b * fog_factor, 0.0f, 1.0f);
    }
}

void GXEngine::ApplyToonTable() {
    const bool toon_enabled = (disp3dcnt & (1 << 1)) != 0;
    if (!toon_enabled) return;

    for (GXVertex& vtx : vertex_buffer) {
        float normal_len = std::sqrt(vtx.normal.x * vtx.normal.x +
                                     vtx.normal.y * vtx.normal.y +
                                     vtx.normal.z * vtx.normal.z);
        if (normal_len < 1e-6f) normal_len = 1.0f;
        const float nx = vtx.normal.x / normal_len;
        const float ny = vtx.normal.y / normal_len;
        const float nz = vtx.normal.z / normal_len;

        float accumulated_intensity = 0.0f;
        for (int i = 0; i < 4; ++i) {
            float lr, lg, lb;
            DSFixed::RGB555ToFloat(light_colors[i], lr, lg, lb);
            const float light_weight = std::max(lr, std::max(lg, lb));

            const Vec3f& l = light_vectors[i];
            const float dot = std::max(0.0f, nx * l.x + ny * l.y + nz * l.z);
            accumulated_intensity += dot * light_weight;
        }

        const int idx = std::clamp(static_cast<int>(accumulated_intensity * 31.0f), 0, 31);
        float toon_r, toon_g, toon_b;
        DSFixed::RGB555ToFloat(toon_table[idx], toon_r, toon_g, toon_b);

        vtx.r = std::clamp(vtx.r * toon_r, 0.0f, 1.0f);
        vtx.g = std::clamp(vtx.g * toon_g, 0.0f, 1.0f);
        vtx.b = std::clamp(vtx.b * toon_b, 0.0f, 1.0f);
    }
}

void GXEngine::CalculateNormalMatrix() {
    // Compute inverse-transpose of the upper 3x3 position matrix in fixed-point.
    const int64_t s = 4096;

    const int64_t m00 = static_cast<int64_t>(position_matrix.m[0] * s);
    const int64_t m01 = static_cast<int64_t>(position_matrix.m[1] * s);
    const int64_t m02 = static_cast<int64_t>(position_matrix.m[2] * s);
    const int64_t m10 = static_cast<int64_t>(position_matrix.m[4] * s);
    const int64_t m11 = static_cast<int64_t>(position_matrix.m[5] * s);
    const int64_t m12 = static_cast<int64_t>(position_matrix.m[6] * s);
    const int64_t m20 = static_cast<int64_t>(position_matrix.m[8] * s);
    const int64_t m21 = static_cast<int64_t>(position_matrix.m[9] * s);
    const int64_t m22 = static_cast<int64_t>(position_matrix.m[10] * s);

    const int64_t c00 = (m11 * m22 - m12 * m21) / s;
    const int64_t c01 = (m12 * m20 - m10 * m22) / s;
    const int64_t c02 = (m10 * m21 - m11 * m20) / s;
    const int64_t c10 = (m02 * m21 - m01 * m22) / s;
    const int64_t c11 = (m00 * m22 - m02 * m20) / s;
    const int64_t c12 = (m01 * m20 - m00 * m21) / s;
    const int64_t c20 = (m01 * m12 - m02 * m11) / s;
    const int64_t c21 = (m02 * m10 - m00 * m12) / s;
    const int64_t c22 = (m00 * m11 - m01 * m10) / s;

    const int64_t det = (m00 * c00 + m01 * c01 + m02 * c02) / s;
    if (det > -8 && det < 8) {
        vector_matrix.Identity();
        return;
    }

    const int64_t inv_det = (s * s) / det;

    vector_matrix.m[0]  = static_cast<float>((c00 * inv_det) / s) / s;
    vector_matrix.m[1]  = static_cast<float>((c10 * inv_det) / s) / s;
    vector_matrix.m[2]  = static_cast<float>((c20 * inv_det) / s) / s;
    vector_matrix.m[3]  = 0.0f;

    vector_matrix.m[4]  = static_cast<float>((c01 * inv_det) / s) / s;
    vector_matrix.m[5]  = static_cast<float>((c11 * inv_det) / s) / s;
    vector_matrix.m[6]  = static_cast<float>((c21 * inv_det) / s) / s;
    vector_matrix.m[7]  = 0.0f;

    vector_matrix.m[8]  = static_cast<float>((c02 * inv_det) / s) / s;
    vector_matrix.m[9]  = static_cast<float>((c12 * inv_det) / s) / s;
    vector_matrix.m[10] = static_cast<float>((c22 * inv_det) / s) / s;
    vector_matrix.m[11] = 0.0f;

    vector_matrix.m[12] = 0.0f;
    vector_matrix.m[13] = 0.0f;
    vector_matrix.m[14] = 0.0f;
    vector_matrix.m[15] = 1.0f;
}
