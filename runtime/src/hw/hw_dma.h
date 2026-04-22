#pragma once

// ============================================================================
// hw_dma.h — Nintendo DS DMA Controller Emulation
//
// The DS has 4 DMA channels per CPU. Each channel can perform fast memory
// copies (typically VRAM uploads, sound buffer fills, or general-purpose
// block transfers). When the Enable bit is set in DMACNT_H, the transfer
// executes immediately (for "Immediate" start timing).
//
// Reference: GBATEK §DS DMA Transfers
//   ARM9 channels: 0x040000B0 - 0x040000DF (4 channels × 12 bytes)
//   ARM7 channels: 0x040000B8 - 0x040000DF (4 channels × 12 bytes)
// ============================================================================

#include <cstdint>
#include <cstring>

class NDSMemory; // Forward declaration — needed for actual memory transfers

// ---- DMA Address Control Modes ----
enum class DMAAddrCtrl : uint8_t {
    Increment  = 0, // Address increases after each unit
    Decrement  = 1, // Address decreases after each unit
    Fixed      = 2, // Address stays the same
    IncrReload = 3  // Increment, but reload original address on repeat
};

// ---- DMA Start Timing ----
enum class DMAStartTiming : uint8_t {
    Immediate = 0,
    VBlank    = 1,
    HBlank    = 2, // ARM9 only
    Special   = 3  // DS Slot / GX FIFO / Wifi, etc.
};

// ---- Single DMA Channel ----
class DMAChannel {
public:
    // Source and destination addresses (internal latched copies)
    uint32_t src_addr    = 0;
    uint32_t dst_addr    = 0;
    uint32_t word_count  = 0;

    // Control register (DMACNT_H upper 16 bits)
    DMAAddrCtrl  dst_ctrl    = DMAAddrCtrl::Increment;
    DMAAddrCtrl  src_ctrl    = DMAAddrCtrl::Increment;
    DMAStartTiming timing    = DMAStartTiming::Immediate;
    bool         repeat      = false;
    bool         word_size32 = true;  // true = 32-bit, false = 16-bit
    bool         irq_on_end  = false;
    bool         enabled     = false;

    DMAChannel() = default;

    // Executes DMA via the full memory map (IO, VRAM, palette, WRAM, etc.).
    void Execute(NDSMemory* mem);

    // Decodes the raw 32-bit control register write (DMACNT combined).
    void WriteControl(uint32_t value) {
        word_count  = value & 0x1FFFFF; // Bits 0-20 (ARM9 can do 21 bits)
        dst_ctrl    = static_cast<DMAAddrCtrl>((value >> 21) & 0x3);
        src_ctrl    = static_cast<DMAAddrCtrl>((value >> 23) & 0x3);
        repeat      = (value >> 25) & 1;
        word_size32 = (value >> 26) & 1;
        timing      = static_cast<DMAStartTiming>((value >> 27) & 0x3);
        irq_on_end  = (value >> 30) & 1;
        enabled     = (value >> 31) & 1;
    }

    // Executes the DMA transfer using raw memory pointers.
    // In a full implementation, src/dst would be resolved through NDSMemory.
    // This version operates on a flat buffer for testability.
    void Execute(uint8_t* memory_base, uint32_t memory_size) {
        if (!enabled) return;

        uint32_t unit_size = word_size32 ? 4 : 2;
        uint32_t count     = (word_count == 0) ? (word_size32 ? 0x200000 : 0x400000) : word_count;
        uint32_t total     = count * unit_size;

        // Bounds check
        if (src_addr + total > memory_size || dst_addr + total > memory_size) {
            enabled = false;
            return;
        }

        // Perform the copy with address control
        uint32_t src = src_addr;
        uint32_t dst = dst_addr;
        int32_t  src_step = 0, dst_step = 0;

        switch (src_ctrl) {
            case DMAAddrCtrl::Increment:  src_step = unit_size;  break;
            case DMAAddrCtrl::Decrement:  src_step = -static_cast<int32_t>(unit_size); break;
            case DMAAddrCtrl::Fixed:      src_step = 0;          break;
            case DMAAddrCtrl::IncrReload: src_step = unit_size;  break;
        }
        switch (dst_ctrl) {
            case DMAAddrCtrl::Increment:  dst_step = unit_size;  break;
            case DMAAddrCtrl::Decrement:  dst_step = -static_cast<int32_t>(unit_size); break;
            case DMAAddrCtrl::Fixed:      dst_step = 0;          break;
            case DMAAddrCtrl::IncrReload: dst_step = unit_size;  break;
        }

        for (uint32_t i = 0; i < count; ++i) {
            std::memcpy(memory_base + dst, memory_base + src, unit_size);
            src += src_step;
            dst += dst_step;
        }

        // Transfer complete — disable unless repeat is set
        if (!repeat) {
            enabled = false;
        } else {
            // Reload destination address on IncrReload
            if (dst_ctrl == DMAAddrCtrl::IncrReload) {
                dst_addr = dst_addr; // Would reload from latch in real HW
            }
        }
    }

    // Simple trigger for legacy test compatibility
    void trigger() {
        if (enabled) {
            enabled = false; // Mark as complete
        }
    }
};

// ---- Legacy Alias ----
using HWDMA = DMAChannel;

class DMACore {
public:
    DMAChannel dma_channels[4];
    class HWIRQ* irq = nullptr;

public:
    void SetupAudioDMA();
    void SetupVRAMDMA();
    void HandleHBlankDMA();
    void HandleVBlankDMA();
    void HandleMainDisplayDMA();
    void RunDMATransfer(int channel);
    void CancelDMA(int channel);
};
