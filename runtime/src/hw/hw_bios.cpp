#include "hw_bios.h"

#include "cpu_context.h"
#include "hw_math.h"
#include "lz_decompress.h"

namespace HwBios {
namespace {
thread_local CPU_Context* g_active_ctx = nullptr;
}

void SetActiveContext(CPU_Context* ctx) {
    g_active_ctx = ctx;
}

CPU_Context* GetActiveContext() {
    return g_active_ctx;
}

void HandleARM9_SWI(uint32_t swi_number) {
    CPU_Context* ctx = GetActiveContext();

    switch (swi_number) {
        case 0x11: { // LZ77UnCompWrite8
            if (ctx == nullptr || ctx->mem == nullptr) {
                return;
            }
            LZDecompress::DecompressFromMemory(*ctx->mem, ctx->r[0], ctx->r[1], LZDecompress::WriteMode::Byte);
            return;
        }
        case 0x12: { // LZ77UnCompWrite16
            if (ctx == nullptr || ctx->mem == nullptr) {
                return;
            }
            LZDecompress::DecompressFromMemory(*ctx->mem, ctx->r[0], ctx->r[1], LZDecompress::WriteMode::Halfword);
            return;
        }
        case 0x09: // Div
            ComputeDiv32();
            break;
        case 0x0D: // Sqrt
            ComputeSqrt32();
            break;
        default:
            break;
    }
}

void HandleARM7_SWI(uint32_t swi_number) {
    switch (swi_number) {
        case 0x06: // Halt
            // CPU halts execution until an IRQ fires.
            // Depending on the threading model, we might yield the CPU context
            break;
        case 0x07: // Stop / Sleep
            break;
        default:
            break;
    }
}
}
