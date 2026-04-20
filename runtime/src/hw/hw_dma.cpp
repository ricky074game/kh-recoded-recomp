#include "hw_dma.h"
#include "hw_irq.h"

// Note: For a pure standalone environment, we simulate memory bounds.
extern uint8_t* g_memory_base;
extern uint32_t g_memory_size;

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
