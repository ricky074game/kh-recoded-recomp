#include "cpu_context.h"
#include "memory_map.h"
#include "optimization.h"
#include "ds_debug.h"
#include "hw_bios.h"
#include <iostream>

namespace {

void mainram_helper_0202B454_chunk(CPU_Context* ctx) {
    switch (ctx->dispatch_pc) {
        case 0x202B454: goto block_0x202B454;
        case 0x202B456: goto block_0x202B456;
        case 0x202B45A: goto block_0x202B45A;
        default: return;
    }

block_0x202B454:
DS_ADDR(0x0202B454);
    ctx->r[2] = ctx->mem->Read32((ctx->r[4] + 20));
block_0x202B456:
DS_ADDR(0x0202B456);
    ctx->r[14] = 0x202B45A; // LR = return address
    ctx->r[15] = 0x2014090;
    return;
block_0x202B45A:
DS_ADDR(0x0202B45A);
    {
        static bool logged_202b45a = false;
        if (!logged_202b45a) {
            logged_202b45a = true;
            const uint32_t sp = ctx->r[13];
            const uint32_t w0 = ctx->mem->Read32(sp);
            const uint32_t w1 = ctx->mem->Read32(sp + 4);
            std::cerr << "mainram_helper_0202B454: probe 0x0202B45A"
                      << " sp=0x" << std::hex << sp
                      << " w0=0x" << w0
                      << " w1=0x" << w1
                      << " lr=0x" << ctx->r[14]
                      << "\n";
        }
    }
    const uint32_t sp = ctx->r[13];
    const uint32_t popped_r4 = ctx->mem->Read32(sp);
    const uint32_t popped_pc_raw = ctx->mem->Read32(sp + 4u);
    const uint32_t popped_pc = popped_pc_raw & ~1u;
    const uint32_t lr_exec_addr = ctx->r[14] & ~1u;

    ctx->r[4] = popped_r4;
    ctx->r[13] = sp + 8u;

    // Keep this helper epilogue on sane caller flow when stack return is junk.
    if (popped_pc >= 0x02000000 && popped_pc < 0x02440000 && popped_pc != 0x0202B45A) {
        ctx->r[15] = popped_pc;
    } else if (lr_exec_addr >= 0x02000000 && lr_exec_addr < 0x02440000 && lr_exec_addr != 0x0202B45A) {
        ctx->r[15] = lr_exec_addr;
    } else {
        ctx->r[15] = 0x02000C0C;
    }
    return;
}

void mainram_helper_0204D150_stub(CPU_Context* ctx) {
    const uint32_t lr_exec_addr = ctx->r[14] & ~1u;

    if (lr_exec_addr != 0 && lr_exec_addr != 0x0204D150) {
        ctx->r[15] = lr_exec_addr;
        return;
    }

    ctx->r[15] = 0x02000D14;
}

void mainram_helper_0204D050_stub(CPU_Context* ctx) {
    const uint32_t lr_exec_addr = ctx->r[14] & ~1u;

    // This address is a Thumb one-time initializer in retail code. For
    // bring-up, seed the published global pointer so downstream code can
    // observe an initialized state instead of a permanent null pointer.
    constexpr uint32_t kHelperGlobalPtr = 0x0206084C;
    constexpr uint32_t kHelperStateWord = 0x02060394;
    constexpr uint32_t kBootState603C8 = 0x020603C8;
    constexpr uint32_t kBootState561C0 = 0x020561C0;
    constexpr uint32_t kSharedFlag2FFFFA8 = 0x02FFFFA8;
    constexpr uint32_t kHelperStateBase = 0x02100000;
    if (ctx->mem->Read32(kHelperGlobalPtr) == 0) {
        ctx->mem->Write32(kHelperGlobalPtr, kHelperStateBase);
        if (ctx->mem->Read32(kHelperStateWord) == 0) {
            ctx->mem->Write32(kHelperStateWord, 1);
        }
        if (ctx->mem->Read8(kBootState603C8) == 0) {
            ctx->mem->Write8(kBootState603C8, 1);
        }
        if (ctx->mem->Read8(kBootState561C0) == 0) {
            ctx->mem->Write8(kBootState561C0, 1);
        }
        if ((ctx->mem->Read16(kSharedFlag2FFFFA8) & 0x8000u) == 0) {
            ctx->mem->Write16(kSharedFlag2FFFFA8, 0x8000u);
        }
    }

    ctx->r[0] = 1;

    if (lr_exec_addr != 0 && lr_exec_addr != 0x0204D050) {
        ctx->r[15] = lr_exec_addr;
        return;
    }

    const uint8_t last_idx = static_cast<uint8_t>(ctx->trace_idx - 1);
    const uint32_t last_trace = ctx->trace_buffer[last_idx];
    if (last_trace >= 0x02000000) {
        ctx->r[15] = last_trace + 4u;
        return;
    }

    ctx->r[15] = 0x02000C88;
}

void mainram_sparse_return_lr_stub(CPU_Context* ctx) {
    const uint32_t lr_exec_addr = ctx->r[14] & ~1u;
    if (ctx->dispatch_pc == 0x02D2C3D0) {
        static bool logged_02d2c3d0 = false;
        if (!logged_02d2c3d0) {
            logged_02d2c3d0 = true;
            const uint8_t last_idx = static_cast<uint8_t>(ctx->trace_idx - 1);
            const uint32_t last_trace = ctx->trace_buffer[last_idx];
            std::cerr << "mainram_sparse_return_lr_stub: probe 0x02D2C3D0"
                      << " lr=0x" << std::hex << lr_exec_addr
                      << " lastTrace=0x" << last_trace
                      << " r0=0x" << ctx->r[0]
                      << " r1=0x" << ctx->r[1]
                      << " r2=0x" << ctx->r[2]
                      << " r3=0x" << ctx->r[3]
                      << " r4=0x" << ctx->r[4]
                      << " r5=0x" << ctx->r[5]
                      << " r6=0x" << ctx->r[6]
                      << " r7=0x" << ctx->r[7]
                      << "\n";
        }
    }
    if (lr_exec_addr != 0 && lr_exec_addr != (ctx->dispatch_pc & ~1u)) {
        ctx->r[15] = lr_exec_addr;
        return;
    }

    // Fallback to architectural trace continuation if LR is unusable.
    const uint8_t last_idx = static_cast<uint8_t>(ctx->trace_idx - 1);
    const uint32_t last_trace = ctx->trace_buffer[last_idx];
    if (last_trace >= 0x02000000) {
        ctx->r[15] = last_trace + 4u;
        return;
    }

    ctx->r[15] = 0x02000D84;
}

void mainram_helper_02004AE4_stub(CPU_Context* ctx) {
    // Isolated lifting from 0x02004AE4 resolves into the helper body at
    // 0x02029B54. Route sparse dispatch directly there.
    ctx->r[15] = 0x02029B54;
}

void mainram_helper_02014090_stub(CPU_Context* ctx) {
    const uint32_t lr_exec_addr = ctx->r[14] & ~1u;

    // Preserve the existing 0x0202B45A callsite recovery path: this helper can
    // be reached without a materialized stacked return address.
    if (lr_exec_addr == 0x0202B45A) {
        const uint32_t sp = ctx->r[13];
        const uint32_t stacked_pc = ctx->mem->Read32(sp + 4u);
        if (stacked_pc == 0) {
            ctx->mem->Write32(sp + 4u, 0x02000C0C);
        }
        ctx->r[15] = lr_exec_addr;
        return;
    }

    // Isolated lifting from 0x02014090 shows a conditional coprocessor op
    // followed by normal fallthrough to 0x02014094.
    ctx->r[15] = 0x02014094;
}

void mainram_helper_02029B64_tail_stub(CPU_Context* ctx) {
    // The sparse tail at 0x02029B64+ is not currently covered by the main
    // generated block in this build. Keep control flow on caller return.
    const uint32_t lr_exec_addr = ctx->r[14] & ~1u;
    if (lr_exec_addr >= 0x02000000 && lr_exec_addr < 0x02440000 &&
        lr_exec_addr != (ctx->dispatch_pc & ~1u)) {
        ctx->r[15] = lr_exec_addr;
        return;
    }

    const uint8_t last_idx = static_cast<uint8_t>(ctx->trace_idx - 1);
    const uint32_t last_trace = ctx->trace_buffer[last_idx];
    if (last_trace >= 0x02000000) {
        ctx->r[15] = last_trace + 4u;
        return;
    }

    ctx->r[15] = 0x02000C04;
}

void mainram_helper_0200D510_stub(CPU_Context* ctx) {
    // This call target currently falls through an untranslated tail at
    // 0x0200D538. Preserve caller progress by treating it as a helper stub.
    const uint32_t lr_exec_addr = ctx->r[14] & ~1u;
    if (lr_exec_addr != 0 && lr_exec_addr != 0x0200D510) {
        ctx->r[15] = lr_exec_addr;
        return;
    }

    const uint8_t last_idx = static_cast<uint8_t>(ctx->trace_idx - 1);
    const uint32_t last_trace = ctx->trace_buffer[last_idx];
    if (last_trace >= 0x02000000) {
        ctx->r[15] = last_trace + 4u;
        return;
    }

    ctx->r[15] = 0x02000C50;
}


void mainram_helper_0203A85A_stub(CPU_Context* ctx) {
    uint32_t r0 = ctx->mem->Read32(0x0203A89C);
    
    ctx->mem->Write32(r0, ctx->r[1]);
    for (int i = 0; i < 13; i++) {
        uint32_t val = ctx->mem->Read32(0x0203A8A4 + i * 4);
        ctx->mem->Write32(r0 + 4 + i * 4, val);
    }
    
    ctx->r[0] = 1;
    ctx->r[15] = ctx->r[14] & ~1u;
}

void mainram_helper_02019D20_stub(CPU_Context* ctx) {
    ctx->mem->Write32(ctx->r[0] + 4, ctx->r[1]);
    ctx->mem->Write32(ctx->r[0] + 0x14, ctx->r[1]);
    if (ctx->r[6] == 0) {
        ctx->r[15] = 0x02019D50;
    } else {
        ctx->r[4] = ctx->r[13];
        ctx->r[0] = ctx->r[4];
        ctx->r[14] = 0x02019D3C;
        ctx->r[15] = 0x02006B90;
    }
}

void mainram_helper_02019D3C_stub(CPU_Context* ctx) {
    if (ctx->r[0] != 0) {
        ctx->r[0] = ctx->r[4];
        ctx->r[14] = 0x02019D3C;
        ctx->r[15] = 0x02006B90;
    } else {
        ctx->r[0] = ctx->r[4];
        ctx->r[1] = ctx->r[6];
        ctx->r[14] = 0x02019D50;
        ctx->r[15] = 0x020059D4;
    }
}

void mainram_helper_02019D50_stub(CPU_Context* ctx) {
    if (ctx->r[5] == 0) {
        ctx->r[15] = 0x02019D68;
    } else {
        ctx->r[0] = ctx->r[5];
        ctx->r[14] = 0x02019D60;
        ctx->r[15] = 0x02006BC0;
    }
}

void mainram_helper_02019D60_stub(CPU_Context* ctx) {
    if (ctx->r[0] != 0) {
        ctx->r[0] = ctx->r[5];
        ctx->r[14] = 0x02019D60;
        ctx->r[15] = 0x02006BC0;
    } else {
        ctx->r[15] = 0x02019D68;
    }
}

void mainram_helper_02019D68_stub(CPU_Context* ctx) {
    ctx->mem->Write32(0x04000448, 1);
    ctx->mem->Write32(0x04000440, 2);
    ctx->r[13] += 0x40;
    ctx->r[4] = ctx->mem->Read32(ctx->r[13]);
    ctx->r[5] = ctx->mem->Read32(ctx->r[13] + 4);
    ctx->r[6] = ctx->mem->Read32(ctx->r[13] + 8);
    ctx->r[15] = ctx->mem->Read32(ctx->r[13] + 12) & ~1u;
    ctx->r[13] += 16;
}

void mainram_helper_0202ABA8_stub(CPU_Context* ctx) {
    ctx->r[1] = ctx->mem->Read32(0x0202ABC0);
    ctx->r[0] = 0;
    
    ctx->mem->Write16(ctx->r[1] + 2, 0);
    ctx->mem->Write16(ctx->r[1] + 4, 0);
    ctx->mem->Write16(ctx->r[1], 0);

    ctx->r[1] = ctx->mem->Read32(0x0202ABC4);
    ctx->r[2] = 0x30;

    // Inline memset (blx 0x1FF86FC)
    for(uint32_t i = 0; i < ctx->r[2]; i++) {
        ctx->mem->Write8(ctx->r[1] + i, ctx->r[0] & 0xFF);
    }

    // Inline pop {r3, pc}
    ctx->r[0] = 1;
    ctx->r[15] = ctx->r[14] & ~1u;
}

void mainram_helper_0202ABBC_stub(CPU_Context* ctx) {
    ctx->r[0] = 1;
    ctx->r[3] = ctx->mem->Read32(ctx->r[13]);
    ctx->r[15] = ctx->mem->Read32(ctx->r[13] + 4);
    ctx->r[13] += 8;
}

} // namespace

void RegisterMainRAMHelperFunctions(NDSMemory* mem) {
    if (mem == nullptr) {
        return;
    }

    mem->GetOverlayManager().RegisterStaticFunction(0x0202B454, mainram_helper_0202B454_chunk);
    mem->GetOverlayManager().RegisterStaticFunction(0x0202B456, mainram_helper_0202B454_chunk);
    mem->GetOverlayManager().RegisterStaticFunction(0x0202B45A, mainram_helper_0202B454_chunk);
    mem->GetOverlayManager().RegisterStaticFunction(0x0204D050, mainram_helper_0204D050_stub);
    mem->GetOverlayManager().RegisterStaticFunction(0x0204D150, mainram_helper_0204D150_stub);
    mem->GetOverlayManager().RegisterStaticFunction(0x020049D4, mainram_sparse_return_lr_stub);
    mem->GetOverlayManager().RegisterStaticFunction(0x02004AE4, mainram_helper_02004AE4_stub);
    mem->GetOverlayManager().RegisterStaticFunction(0x02014090, mainram_helper_02014090_stub);
    mem->GetOverlayManager().RegisterStaticFunction(0x02029B64, mainram_helper_02029B64_tail_stub);
    mem->GetOverlayManager().RegisterStaticFunction(0x02029B68, mainram_helper_02029B64_tail_stub);
    mem->GetOverlayManager().RegisterStaticFunction(0x02029B6C, mainram_helper_02029B64_tail_stub);
    mem->GetOverlayManager().RegisterStaticFunction(0x02029B70, mainram_helper_02029B64_tail_stub);
    mem->GetOverlayManager().RegisterStaticFunction(0x0200D510, mainram_helper_0200D510_stub);
    mem->GetOverlayManager().RegisterStaticFunction(0x0200D538, mainram_helper_0200D510_stub);
    mem->GetOverlayManager().RegisterStaticFunction(0x02014038, mainram_sparse_return_lr_stub);
    mem->GetOverlayManager().RegisterStaticFunction(0x02025082, mainram_sparse_return_lr_stub);
    mem->GetOverlayManager().RegisterStaticFunction(0x0202A650, mainram_sparse_return_lr_stub);

    // Bootflow helpers
    mem->GetOverlayManager().RegisterStaticFunction(0x0203A85A, mainram_helper_0203A85A_stub);
    mem->GetOverlayManager().RegisterStaticFunction(0x0202ABA8, mainram_helper_0202ABA8_stub);
    mem->GetOverlayManager().RegisterStaticFunction(0x0202ABBC, mainram_helper_0202ABBC_stub);
    mem->GetOverlayManager().RegisterStaticFunction(0x02019D20, mainram_helper_02019D20_stub);
    mem->GetOverlayManager().RegisterStaticFunction(0x02019D3C, mainram_helper_02019D3C_stub);
    mem->GetOverlayManager().RegisterStaticFunction(0x02019D50, mainram_helper_02019D50_stub);
    mem->GetOverlayManager().RegisterStaticFunction(0x02019D60, mainram_helper_02019D60_stub);
    mem->GetOverlayManager().RegisterStaticFunction(0x02019D68, mainram_helper_02019D68_stub);
    mem->GetOverlayManager().RegisterStaticFunction(0x02D2C3D0, mainram_sparse_return_lr_stub);

    // Additional unmapped boot branches
    mem->GetOverlayManager().RegisterStaticFunction(0x01FFA1F4, mainram_sparse_return_lr_stub);
    mem->GetOverlayManager().RegisterStaticFunction(0x02FFFD9C, mainram_sparse_return_lr_stub);
    mem->GetOverlayManager().RegisterStaticFunction(0x0202A73A, mainram_sparse_return_lr_stub);
    mem->GetOverlayManager().RegisterStaticFunction(0x0202A786, mainram_sparse_return_lr_stub);
}
