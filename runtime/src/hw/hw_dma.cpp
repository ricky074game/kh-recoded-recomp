#include "hw_dma.h"
#include "hw_irq.h"
#include "memory_map.h"

// Note: For a pure standalone environment, we simulate memory bounds.
extern uint8_t* g_memory_base;
extern uint32_t g_memory_size;

namespace {
int32_t DMAStep(DMAAddrCtrl ctrl, uint32_t unit_size) {
    switch (ctrl) {
        case DMAAddrCtrl::Increment:  return static_cast<int32_t>(unit_size);
        case DMAAddrCtrl::Decrement:  return -static_cast<int32_t>(unit_size);
        case DMAAddrCtrl::Fixed:      return 0;
        case DMAAddrCtrl::IncrReload: return static_cast<int32_t>(unit_size);
    }
    return static_cast<int32_t>(unit_size);
}
}

void DMAChannel::Execute(NDSMemory* mem) {
    if (!enabled || mem == nullptr) return;

    const uint32_t unit_size = word_size32 ? 4u : 2u;
    const uint32_t count =
        (word_count == 0) ? (word_size32 ? 0x200000u : 0x400000u) : word_count;

    uint32_t src = src_addr;
    uint32_t dst = dst_addr;
    const int32_t src_step = DMAStep(src_ctrl, unit_size);
    const int32_t dst_step = DMAStep(dst_ctrl, unit_size);

    for (uint32_t i = 0; i < count; ++i) {
        try {
            if (word_size32) {
                const uint32_t value = mem->Read32(src & ~0x3u);
                mem->Write32(dst & ~0x3u, value);
            } else {
                const uint16_t value = mem->Read16(src & ~0x1u);
                mem->Write16(dst & ~0x1u, value);
            }
        } catch (...) {
            // Abort this channel on invalid transfer addresses.
            enabled = false;
            return;
        }

        src = static_cast<uint32_t>(static_cast<int32_t>(src) + src_step);
        dst = static_cast<uint32_t>(static_cast<int32_t>(dst) + dst_step);
    }

    if (!repeat) {
        enabled = false;
    }
}

void DMACore::SetupAudioDMA() {
    dma_channels[1].timing = DMAStartTiming::Special;
    dma_channels[1].repeat = true;
    dma_channels[2].timing = DMAStartTiming::Special;
    dma_channels[2].repeat = true;
}

void DMACore::SetupVRAMDMA() {
    dma_channels[3].timing = DMAStartTiming::Special;
}

void DMACore::HandleHBlankDMA() {
    for (int channel = 0; channel < 4; ++channel) {
        if (dma_channels[channel].enabled && dma_channels[channel].timing == DMAStartTiming::HBlank) {
            RunDMATransfer(channel);
        }
    }
}

void DMACore::HandleVBlankDMA() {
    for (int channel = 0; channel < 4; ++channel) {
        if (dma_channels[channel].enabled && dma_channels[channel].timing == DMAStartTiming::VBlank) {
            RunDMATransfer(channel);
        }
    }
}

void DMACore::HandleMainDisplayDMA() {
    for (int channel = 0; channel < 4; ++channel) {
        if (dma_channels[channel].enabled && dma_channels[channel].timing == DMAStartTiming::Special) {
            RunDMATransfer(channel);
        }
    }
}

void DMACore::RunDMATransfer(int channel) {
    if (channel < 0 || channel >= 4) return;

    auto& dma = dma_channels[channel];
    if (!dma.enabled) return;

    if (g_memory_base != nullptr && g_memory_size > 0) {
        dma.Execute(g_memory_base, g_memory_size);
    } else {
        // Standalone or mock environment: just disable if not repeating
        if (!dma.repeat) {
            dma.enabled = false;
        }
    }

    if (dma.irq_on_end && irq) {
        switch(channel) {
            case 0: irq->RaiseIRQ(IRQBits::DMA0); break;
            case 1: irq->RaiseIRQ(IRQBits::DMA1); break;
            case 2: irq->RaiseIRQ(IRQBits::DMA2); break;
            case 3: irq->RaiseIRQ(IRQBits::DMA3); break;
        }
    }
}

void DMACore::CancelDMA(int channel) {
    if (channel >= 0 && channel < 4) {
        dma_channels[channel].enabled = false;
    }
}
