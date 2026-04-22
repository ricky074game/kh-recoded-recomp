#pragma once

// ============================================================================
// hw_irq.h — Nintendo DS Interrupt Controller Emulation
//
// Hardware events (VBlank, HBlank, Timers, DMA, IPC, etc.) signal the CPU
// via three registers: IME, IE, and IF. When (IME & 1) && (IE & IF) != 0
// and the CPSR I-bit is clear, the CPU vectors to the IRQ handler.
//
// Reference: GBATEK §DS Interrupt Control
//   IME: 0x04000208  — Interrupt Master Enable
//   IE:  0x04000210  — Interrupt Enable (which sources are listened to)
//   IF:  0x04000214  — Interrupt Flags (which sources have fired)
// ============================================================================

#include <cstdint>
#include "cpu_context.h"

// ---- IRQ Source Bit Definitions ----
// Each bit in IE/IF corresponds to one interrupt source.
namespace IRQBits {
    constexpr uint32_t VBlank       = (1u << 0);
    constexpr uint32_t HBlank       = (1u << 1);
    constexpr uint32_t VCount       = (1u << 2);
    constexpr uint32_t Timer0       = (1u << 3);
    constexpr uint32_t Timer1       = (1u << 4);
    constexpr uint32_t Timer2       = (1u << 5);
    constexpr uint32_t Timer3       = (1u << 6);
    constexpr uint32_t DMA0         = (1u << 8);
    constexpr uint32_t DMA1         = (1u << 9);
    constexpr uint32_t DMA2         = (1u << 10);
    constexpr uint32_t DMA3         = (1u << 11);
    constexpr uint32_t Keypad       = (1u << 12);
    constexpr uint32_t GBASlot      = (1u << 13);
    constexpr uint32_t IPCSync      = (1u << 16);
    constexpr uint32_t IPCSendFIFO  = (1u << 17);
    constexpr uint32_t IPCRecvFIFO  = (1u << 18);
    constexpr uint32_t NDSSlotDone  = (1u << 19);
    constexpr uint32_t NDSSlotIREQ  = (1u << 20);
    constexpr uint32_t GXFIFO       = (1u << 21); // Geometry FIFO (ARM9 only)
}

// ---- Interrupt Controller ----
class HWIRQ {
public:
    uint32_t ime    = 0; // Interrupt Master Enable (only bit 0 matters)
    uint32_t ie     = 0; // Interrupt Enable mask
    uint32_t if_reg = 0; // Interrupt Flags (pending interrupts)

    HWIRQ() = default;

    // Returns true if any enabled interrupt is pending AND the master is on.
    bool HasPendingIRQ() const {
        return (ime & 1) && (ie & if_reg);
    }

    // Legacy alias
    bool trigger() const { return HasPendingIRQ(); }

    // Raise one or more interrupt flags.
    void RaiseIRQ(uint32_t bits) {
        if_reg |= bits;
    }

    // Acknowledge (clear) one or more interrupt flags.
    // Per GBATEK: writing 1 to IF bits clears them.
    void AcknowledgeIRQ(uint32_t bits) {
        if_reg &= ~bits;
    }

    // Convenience: raise the VBlank interrupt.
    void vblank() {
        RaiseIRQ(IRQBits::VBlank);
    }

    // Returns the pending-and-enabled IRQ bits.
    uint32_t GetPendingEnabled() const {
        return ie & if_reg;
    }
};

// ---- IRQ Dispatch ----
// Checks for pending interrupts and, if triggered, performs the ARM IRQ
// entry sequence: saves CPSR→SPSR_irq, saves PC→LR_irq, disables IRQs
// in CPSR (sets I-bit), switches to IRQ mode, and jumps to the vector.
inline uint32_t ComputeIRQVectorTarget(const CPU_Context* ctx, bool use_cp15_high_vector) {
    const uint32_t vector_base =
        (use_cp15_high_vector && (ctx->cp15_control & 0x2000u)) ? 0xFFFF0000u : 0x00000000u;
    return vector_base + 0x18u;
}

inline void EnterIRQ(CPU_Context* ctx, uint32_t return_address, bool use_cp15_high_vector) {
    // 1. Save current CPSR to SPSR_irq
    ctx->spsr_irq = ctx->cpsr;

    // 2. Save return address to LR_irq
    ctx->r14_irq = return_address;

    // 3. Switch to IRQ mode with IRQs disabled
    ctx->cpsr = (ctx->cpsr & ~0x1Fu) | 0x12u; // Mode = IRQ (0x12)
    ctx->cpsr |= (1u << 7);                    // Set I-bit (disable further IRQs)

    // 4. Jump to IRQ vector
    ctx->r[15] = ComputeIRQVectorTarget(ctx, use_cp15_high_vector);
}

inline void CheckInterrupts(CPU_Context* ctx, HWIRQ& irq, uint32_t lr_offset = 0, bool use_cp15_high_vector = false) {
    if (!irq.HasPendingIRQ()) return;

    // Don't interrupt if IRQs are already disabled in CPSR
    // (bit 7 = I flag, 1 = disabled)
    if (ctx->cpsr & (1u << 7)) return;

    EnterIRQ(ctx, ctx->r[15] + lr_offset, use_cp15_high_vector);
}
