#include "hw_bios.h"

#include <thread>

#include "cpu_context.h"
#include "hw_irq.h"
#include "hw_math.h"
#include "lz_decompress.h"
#include "memory_map.h"

namespace HwBios {
namespace {
thread_local CPU_Context* g_active_ctx = nullptr;

template <typename T>
T ReadMemoryValue(NDSMemory& mem, uint32_t address);

template <>
uint16_t ReadMemoryValue<uint16_t>(NDSMemory& mem, uint32_t address) {
    return mem.Read16(address);
}

template <>
uint32_t ReadMemoryValue<uint32_t>(NDSMemory& mem, uint32_t address) {
    return mem.Read32(address);
}

template <typename T>
void WriteMemoryValue(NDSMemory& mem, uint32_t address, T value);

template <>
void WriteMemoryValue<uint16_t>(NDSMemory& mem, uint32_t address, uint16_t value) {
    mem.Write16(address, value);
}

template <>
void WriteMemoryValue<uint32_t>(NDSMemory& mem, uint32_t address, uint32_t value) {
    mem.Write32(address, value);
}

template <typename T>
void CpuSetImpl(NDSMemory& mem, uint32_t src, uint32_t dst, uint32_t control) {
    const bool fixed = (control & (1u << 24)) != 0;
    const uint32_t count = control & 0x001FFFFFu;
    if (count == 0) {
        return;
    }

    T fill_value = 0;
    if (fixed) {
        fill_value = ReadMemoryValue<T>(mem, src);
    }

    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t src_addr = fixed ? src : (src + i * sizeof(T));
        const uint32_t dst_addr = dst + i * sizeof(T);
        const T value = fixed ? fill_value : ReadMemoryValue<T>(mem, src_addr);
        WriteMemoryValue<T>(mem, dst_addr, value);
    }
}

void CpuFastSet(NDSMemory& mem, uint32_t src, uint32_t dst, uint32_t control) {
    const bool fixed = (control & (1u << 24)) != 0;
    const uint32_t word_count = control & 0x001FFFFFu;
    if (word_count == 0) {
        return;
    }

    uint32_t fill_value = 0;
    if (fixed) {
        fill_value = mem.Read32(src);
    }

    for (uint32_t i = 0; i < word_count; ++i) {
        const uint32_t src_addr = fixed ? src : (src + i * 4u);
        const uint32_t dst_addr = dst + i * 4u;
        const uint32_t value = fixed ? fill_value : mem.Read32(src_addr);
        mem.Write32(dst_addr, value);
    }
}

void BitUnPack(NDSMemory& mem, uint32_t src, uint32_t dst, uint32_t info_ptr) {
    const uint32_t src_len_bytes = mem.Read16(info_ptr);
    const uint32_t src_unit_bits = mem.Read8(info_ptr + 2u);
    const uint32_t dst_unit_bits = mem.Read8(info_ptr + 3u);
    const uint32_t data_offset_word = mem.Read32(info_ptr + 4u);
    const uint32_t data_offset = data_offset_word & 0x7FFFFFFFu;
    const bool add_offset_to_zero = (data_offset_word & 0x80000000u) != 0;

    if (src_len_bytes == 0 || dst_unit_bits == 0 || dst_unit_bits > 32) {
        return;
    }

    const uint32_t supported_src_bits[] = {1u, 2u, 4u, 8u};
    bool src_bits_supported = false;
    for (uint32_t bits : supported_src_bits) {
        if (src_unit_bits == bits) {
            src_bits_supported = true;
            break;
        }
    }
    if (!src_bits_supported) {
        return;
    }

    uint32_t out_word = 0;
    uint32_t out_bits_used = 0;
    uint32_t out_addr = dst;

    const uint32_t units_per_byte = 8u / src_unit_bits;
    const uint32_t src_mask = (1u << src_unit_bits) - 1u;
    const uint64_t dst_mask =
        (dst_unit_bits == 32u) ? 0xFFFFFFFFull : ((1ull << dst_unit_bits) - 1ull);

    for (uint32_t byte_index = 0; byte_index < src_len_bytes; ++byte_index) {
        const uint32_t packed = mem.Read8(src + byte_index);
        for (uint32_t unit_index = 0; unit_index < units_per_byte; ++unit_index) {
            const uint32_t shift = unit_index * src_unit_bits;
            uint32_t value = (packed >> shift) & src_mask;
            if (value != 0 || add_offset_to_zero) {
                value += data_offset;
            }
            value &= static_cast<uint32_t>(dst_mask);

            out_word |= (value << out_bits_used);
            out_bits_used += dst_unit_bits;

            if (out_bits_used >= 32u) {
                mem.Write32(out_addr, out_word);
                out_addr += 4u;
                out_word = 0;
                out_bits_used = 0;
            }
        }
    }
}

void WaitForInterrupt(CPU_Context* ctx, HWIRQ& irq, uint32_t mask, bool acknowledge) {
    if (ctx == nullptr || ctx->mem == nullptr) {
        return;
    }

    while (ctx->running_flag == nullptr || ctx->running_flag->load(std::memory_order_relaxed)) {
        const uint32_t pending = irq.GetPendingEnabled();
        const uint32_t matching = (mask == 0) ? pending : (pending & mask);
        if (matching != 0) {
            if (acknowledge) {
                irq.AcknowledgeIRQ(matching);
            }
            return;
        }

        ctx->mem->StepHardware(64);
        std::this_thread::yield();
    }
}
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
        case 0x04: // IntrWait
            if (ctx != nullptr && ctx->mem != nullptr) {
                if (ctx->r[0] != 0) {
                    ctx->mem->irq_arm9.AcknowledgeIRQ(ctx->r[1]);
                }
                WaitForInterrupt(ctx, ctx->mem->irq_arm9, ctx->r[1], true);
            }
            return;

        case 0x06: // Halt
            if (ctx != nullptr && ctx->mem != nullptr) {
                WaitForInterrupt(ctx, ctx->mem->irq_arm9, 0, false);
            }
            return;

        case 0x09: // Div
            ComputeDiv32();
            return;

        case 0x0B: // CpuSet
            if (ctx != nullptr && ctx->mem != nullptr) {
                const uint32_t control = ctx->r[2];
                if ((control & (1u << 26)) != 0) {
                    CpuSetImpl<uint32_t>(*ctx->mem, ctx->r[0], ctx->r[1], control);
                } else {
                    CpuSetImpl<uint16_t>(*ctx->mem, ctx->r[0], ctx->r[1], control);
                }
            }
            return;

        case 0x0C: // CpuFastSet
            if (ctx != nullptr && ctx->mem != nullptr) {
                CpuFastSet(*ctx->mem, ctx->r[0], ctx->r[1], ctx->r[2]);
            }
            return;

        case 0x0D: // Sqrt
            ComputeSqrt32();
            return;

        case 0x10: // BitUnPack
            if (ctx != nullptr && ctx->mem != nullptr) {
                BitUnPack(*ctx->mem, ctx->r[0], ctx->r[1], ctx->r[2]);
            }
            return;

        case 0x11: // LZ77UnCompWrite8
            if (ctx != nullptr && ctx->mem != nullptr) {
                LZDecompress::DecompressFromMemory(
                    *ctx->mem, ctx->r[0], ctx->r[1], LZDecompress::WriteMode::Byte);
            }
            return;

        case 0x12: // LZ77UnCompWrite16
            if (ctx != nullptr && ctx->mem != nullptr) {
                LZDecompress::DecompressFromMemory(
                    *ctx->mem, ctx->r[0], ctx->r[1], LZDecompress::WriteMode::Halfword);
            }
            return;

        default:
            return;
    }
}

void HandleARM7_SWI(uint32_t swi_number) {
    CPU_Context* ctx = GetActiveContext();

    switch (swi_number) {
        case 0x04:
            if (ctx != nullptr && ctx->mem != nullptr) {
                if (ctx->r[0] != 0) {
                    ctx->mem->irq_arm7.AcknowledgeIRQ(ctx->r[1]);
                }
                WaitForInterrupt(ctx, ctx->mem->irq_arm7, ctx->r[1], true);
            }
            return;

        case 0x06:
            if (ctx != nullptr && ctx->mem != nullptr) {
                WaitForInterrupt(ctx, ctx->mem->irq_arm7, 0, false);
            }
            return;

        case 0x07:
            return;

        default:
            return;
    }
}
}
