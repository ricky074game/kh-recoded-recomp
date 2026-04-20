#include "cpu_context.h"
#include "memory_map.h"
#include "optimization.h"
#include "ds_debug.h"
#include "hw_bios.h"
#include <iostream>

namespace {

inline void ExecSMULL(CPU_Context* ctx, int rd_lo, int rd_hi, int rm, int rs) {
    const int64_t lhs = static_cast<int64_t>(static_cast<int32_t>(ctx->r[rm]));
    const int64_t rhs = static_cast<int64_t>(static_cast<int32_t>(ctx->r[rs]));
    const int64_t result = lhs * rhs;
    const uint64_t bits = static_cast<uint64_t>(result);
    ctx->r[rd_lo] = static_cast<uint32_t>(bits & 0xFFFFFFFFu);
    ctx->r[rd_hi] = static_cast<uint32_t>(bits >> 32);
}

inline void ExecSMLAL(CPU_Context* ctx, int rd_lo, int rd_hi, int rm, int rs) {
    const int64_t lhs = static_cast<int64_t>(static_cast<int32_t>(ctx->r[rm]));
    const int64_t rhs = static_cast<int64_t>(static_cast<int32_t>(ctx->r[rs]));
    const uint64_t acc_bits = (static_cast<uint64_t>(ctx->r[rd_hi]) << 32) |
                              static_cast<uint64_t>(ctx->r[rd_lo]);
    const int64_t acc = static_cast<int64_t>(acc_bits);
    const int64_t result = acc + (lhs * rhs);
    const uint64_t bits = static_cast<uint64_t>(result);
    ctx->r[rd_lo] = static_cast<uint32_t>(bits & 0xFFFFFFFFu);
    ctx->r[rd_hi] = static_cast<uint32_t>(bits >> 32);
}

void itcm_helper_01001304_chunk(CPU_Context* ctx) {
    switch (ctx->dispatch_pc) {
        case 0x1001304: goto block_0x1001304;
        case 0x1001308: goto block_0x1001308;
        case 0x100130C: goto block_0x100130C;
        case 0x1001310: goto block_0x1001310;
        case 0x1001314: goto block_0x1001314;
        case 0x1001318: goto block_0x1001318;
        case 0x100131C: goto block_0x100131C;
        case 0x1001320: goto block_0x1001320;
        case 0x1001324: goto block_0x1001324;
        case 0x1001328: goto block_0x1001328;
        case 0x100132C: goto block_0x100132C;
        case 0x1001330: goto block_0x1001330;
        case 0x1001334: goto block_0x1001334;
        case 0x1001338: goto block_0x1001338;
        case 0x100133C: goto block_0x100133C;
        case 0x1001340: goto block_0x1001340;
        case 0x1001344: goto block_0x1001344;
        case 0x1001348: goto block_0x1001348;
        case 0x100134C: goto block_0x100134C;
        case 0x1001350: goto block_0x1001350;
        case 0x1001354: goto block_0x1001354;
        case 0x1001358: goto block_0x1001358;
        case 0x100135C: goto block_0x100135C;
        case 0x1001360: goto block_0x1001360;
        case 0x1001364: goto block_0x1001364;
        case 0x1001368: goto block_0x1001368;
        case 0x100136C: goto block_0x100136C;
        case 0x1001370: goto block_0x1001370;
        case 0x1001374: goto block_0x1001374;
        case 0x1001378: goto block_0x1001378;
        case 0x100137C: goto block_0x100137C;
        case 0x1001380: goto block_0x1001380;
        case 0x1001384: goto block_0x1001384;
        case 0x1001388: goto block_0x1001388;
        case 0x100138C: goto block_0x100138C;
        case 0x1001390: goto block_0x1001390;
        case 0x1001394: goto block_0x1001394;
        case 0x1001398: goto block_0x1001398;
        case 0x100139C: goto block_0x100139C;
        case 0x10013A0: goto block_0x10013A0;
        case 0x10013A4: goto block_0x10013A4;
        case 0x10013A8: goto block_0x10013A8;
        case 0x10013AC: goto block_0x10013AC;
        case 0x10013B0: goto block_0x10013B0;
        case 0x10013B4: goto block_0x10013B4;
        case 0x10013B8: goto block_0x10013B8;
        case 0x10013BC: goto block_0x10013BC;
        case 0x10013C0: goto block_0x10013C0;
        case 0x10013C4: goto block_0x10013C4;
        case 0x10013C8: goto block_0x10013C8;
        case 0x10013CC: goto block_0x10013CC;
        case 0x10013D0: goto block_0x10013D0;
        case 0x10013D4: goto block_0x10013D4;
        case 0x10013D8: goto block_0x10013D8;
        case 0x10013DC: goto block_0x10013DC;
        case 0x10013E0: goto block_0x10013E0;
        default: return;
    }

block_0x1001304:
DS_ADDR(0x01001304);
    ExecSMLAL(ctx, 6, 5, 8, 7);
block_0x1001308:
DS_ADDR(0x01001308);
    ctx->r[6] = ((uint32_t)(ctx->r[6]) >> 12);
block_0x100130C:
DS_ADDR(0x0100130C);
    ctx->r[6] = ctx->r[6] | ((ctx->r[5] << 20));
block_0x1001310:
DS_ADDR(0x01001310);
    ExecSMLAL(ctx, 10, 9, 11, 7);
block_0x1001314:
DS_ADDR(0x01001314);
    ctx->r[5] = ((uint32_t)(ctx->r[10]) >> 12);
block_0x1001318:
DS_ADDR(0x01001318);
    ctx->mem->Write32((ctx->r[2] + 8), ctx->r[6]);
block_0x100131C:
DS_ADDR(0x0100131C);
    ctx->r[5] = ctx->r[5] | ((ctx->r[9] << 20));
block_0x1001320:
DS_ADDR(0x01001320);
    ctx->mem->Write32((ctx->r[2] + 20), ctx->r[5]);
block_0x1001324:
DS_ADDR(0x01001324);
    ctx->r[5] = ctx->mem->Read32((ctx->r[1] + 16));
block_0x1001328:
DS_ADDR(0x01001328);
    ctx->r[6] = ctx->mem->Read32((ctx->r[1] + 4));
block_0x100132C:
DS_ADDR(0x0100132C);
    ExecSMULL(ctx, 8, 7, 3, 5);
block_0x1001330:
DS_ADDR(0x01001330);
    ExecSMLAL(ctx, 8, 7, 4, 6);
block_0x1001334:
DS_ADDR(0x01001334);
    ctx->r[5] = ctx->mem->Read32((ctx->r[1] + 28));
block_0x1001338:
DS_ADDR(0x01001338);
    ctx->r[6] = ctx->mem->Read32((ctx->r[0] + 28));
block_0x100133C:
DS_ADDR(0x0100133C);
    ExecSMLAL(ctx, 8, 7, 11, 5);
block_0x1001340:
DS_ADDR(0x01001340);
    ctx->r[5] = ((uint32_t)(ctx->r[8]) >> 12);
block_0x1001344:
DS_ADDR(0x01001344);
    ctx->r[5] = ctx->r[5] | ((ctx->r[7] << 20));
block_0x1001348:
DS_ADDR(0x01001348);
    ctx->mem->Write32((ctx->r[2] + 16), ctx->r[5]);
block_0x100134C:
DS_ADDR(0x0100134C);
    ctx->r[5] = ctx->mem->Read32((ctx->r[1] + 12));
block_0x1001350:
DS_ADDR(0x01001350);
    ctx->r[10] = ctx->mem->Read32(ctx->r[1]);
block_0x1001354:
DS_ADDR(0x01001354);
    ExecSMULL(ctx, 8, 7, 3, 5);
block_0x1001358:
DS_ADDR(0x01001358);
    ExecSMLAL(ctx, 8, 7, 4, 10);
block_0x100135C:
DS_ADDR(0x0100135C);
    ExecSMULL(ctx, 4, 3, 6, 5);
block_0x1001360:
DS_ADDR(0x01001360);
    ctx->r[5] = ctx->mem->Read32((ctx->r[0] + 24));
block_0x1001364:
DS_ADDR(0x01001364);
    ctx->r[9] = ctx->mem->Read32((ctx->r[1] + 24));
block_0x1001368:
DS_ADDR(0x01001368);
    ExecSMLAL(ctx, 4, 3, 5, 10);
block_0x100136C:
DS_ADDR(0x0100136C);
    ctx->r[0] = ctx->mem->Read32((ctx->r[0] + 32));
block_0x1001370:
DS_ADDR(0x01001370);
    ExecSMLAL(ctx, 8, 7, 11, 9);
block_0x1001374:
DS_ADDR(0x01001374);
    ExecSMLAL(ctx, 4, 3, 0, 9);
block_0x1001378:
DS_ADDR(0x01001378);
    ctx->r[8] = ((uint32_t)(ctx->r[8]) >> 12);
block_0x100137C:
DS_ADDR(0x0100137C);
    ctx->r[8] = ctx->r[8] | ((ctx->r[7] << 20));
block_0x1001380:
DS_ADDR(0x01001380);
    ctx->r[4] = ((uint32_t)(ctx->r[4]) >> 12);
block_0x1001384:
DS_ADDR(0x01001384);
    CalculateFlags_SUB(ctx, ctx->r[2], (ctx->r[14]));
block_0x1001388:
DS_ADDR(0x01001388);
    ctx->mem->Write32((ctx->r[2] + 12), ctx->r[8]);
block_0x100138C:
DS_ADDR(0x0100138C);
    ctx->r[4] = ctx->r[4] | ((ctx->r[3] << 20));
block_0x1001390:
DS_ADDR(0x01001390);
    ctx->mem->Write32((ctx->r[2] + 24), ctx->r[4]);
block_0x1001394:
DS_ADDR(0x01001394);
    ctx->r[3] = ctx->mem->Read32((ctx->r[1] + 16));
block_0x1001398:
DS_ADDR(0x01001398);
    ctx->r[4] = ctx->mem->Read32((ctx->r[1] + 4));
block_0x100139C:
DS_ADDR(0x0100139C);
    ExecSMULL(ctx, 8, 3, 6, 3);
block_0x10013A0:
DS_ADDR(0x010013A0);
    ExecSMLAL(ctx, 8, 3, 5, 4);
block_0x10013A4:
DS_ADDR(0x010013A4);
    ctx->r[7] = ctx->mem->Read32((ctx->r[1] + 28));
block_0x10013A8:
DS_ADDR(0x010013A8);
    // Skip conditional SP advance on this bring-up path.
block_0x10013AC:
DS_ADDR(0x010013AC);
    ExecSMLAL(ctx, 8, 3, 0, 7);
block_0x10013B0:
DS_ADDR(0x010013B0);
    ctx->r[4] = ((uint32_t)(ctx->r[8]) >> 12);
block_0x10013B4:
DS_ADDR(0x010013B4);
    ctx->r[4] = ctx->r[4] | ((ctx->r[3] << 20));
block_0x10013B8:
DS_ADDR(0x010013B8);
    ctx->mem->Write32((ctx->r[2] + 28), ctx->r[4]);
block_0x10013BC:
DS_ADDR(0x010013BC);
    ctx->r[4] = ctx->mem->Read32((ctx->r[1] + 32));
block_0x10013C0:
DS_ADDR(0x010013C0);
    ctx->r[3] = ctx->mem->Read32((ctx->r[1] + 8));
block_0x10013C4:
DS_ADDR(0x010013C4);
    ctx->r[1] = ctx->mem->Read32((ctx->r[1] + 20));
block_0x10013C8:
DS_ADDR(0x010013C8);
    ExecSMULL(ctx, 7, 1, 6, 1);
block_0x10013CC:
DS_ADDR(0x010013CC);
    ExecSMLAL(ctx, 7, 1, 5, 3);
block_0x10013D0:
DS_ADDR(0x010013D0);
    ExecSMLAL(ctx, 7, 1, 0, 4);
block_0x10013D4:
DS_ADDR(0x010013D4);
    ctx->r[0] = ((uint32_t)(ctx->r[7]) >> 12);
block_0x10013D8:
DS_ADDR(0x010013D8);
    ctx->r[0] = ctx->r[0] | ((ctx->r[1] << 20));
block_0x10013DC:
DS_ADDR(0x010013DC);
    ctx->mem->Write32((ctx->r[2] + 32), ctx->r[0]);
block_0x10013E0:
DS_ADDR(0x010013E0);
    // Keep this helper on the call-return path during bring-up. The raw
    // conditional pop sequence can restore an invalid PC before stack/state
    // semantics are fully modeled.
    ctx->r[15] = ctx->r[14];
    return;
}

void itcm_helper_010080d4_chunk(CPU_Context* ctx) {
    switch (ctx->dispatch_pc) {
        case 0x010080D4: goto block_0x10080D4;
        case 0x010080E4: goto block_0x10080E4;
        default: return;
    }

block_0x10080D4:
DS_ADDR(0x010080D4);
    {
        const uint32_t lr_exec_addr = ctx->r[14] & ~1u;
        const uint8_t last_idx = static_cast<uint8_t>(ctx->trace_idx - 1);
        const uint32_t last_trace = ctx->trace_buffer[last_idx];

        // This helper behaves like a time/tick source on bring-up paths.
        // Always provide a hardware-like monotonic value in r0.
        ctx->r[0] = ctx->mem->Read16(0x04000006);

        static int debug_call_count = 0;
        if (debug_call_count < 12) {
            ++debug_call_count;
            std::cerr << "itcm_helper_010080D4 call"
                      << " last_trace=0x" << std::hex << last_trace
                      << " lr=0x" << lr_exec_addr
                      << " r0=0x" << ctx->r[0]
                      << "\n";
        }

        if (lr_exec_addr != 0 && lr_exec_addr != 0x010080D4) {
            ctx->r[15] = lr_exec_addr;
            return;
        }

        if (last_trace >= 0x02000000) {
            ctx->r[15] = last_trace + 4u;
            return;
        }

        ctx->r[15] = 0x02000CA0;
        return;
    }

block_0x10080E4:
DS_ADDR(0x010080E4);
    {
        const uint32_t lr_exec_addr = ctx->r[14] & ~1u;
        if (lr_exec_addr != 0 && lr_exec_addr != 0x010080E4) {
            ctx->r[15] = lr_exec_addr;
            return;
        }

        const uint8_t last_idx = static_cast<uint8_t>(ctx->trace_idx - 1);
        const uint32_t last_trace = ctx->trace_buffer[last_idx];
        if (last_trace >= 0x02000000) {
            ctx->r[15] = last_trace + 4u;
            return;
        }

        ctx->r[15] = 0x02000D84;
        return;
    }
}

} // namespace

void RegisterITCMHelperFunctions(NDSMemory* mem) {
    if (mem == nullptr) {
        return;
    }

    for (uint32_t addr = 0x01001304; addr <= 0x010013E0; addr += 4) {
        mem->GetOverlayManager().RegisterStaticFunction(addr, itcm_helper_01001304_chunk);
    }

    mem->GetOverlayManager().RegisterStaticFunction(0x010080D4, itcm_helper_010080d4_chunk);
    mem->GetOverlayManager().RegisterStaticFunction(0x010080E4, itcm_helper_010080d4_chunk);
}
