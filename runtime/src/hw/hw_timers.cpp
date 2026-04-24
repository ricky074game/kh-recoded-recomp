#include "hw_timers.h"

#include <algorithm>
#include <array>

#include "hw_2d_engine.h"
#include "hw_irq.h"

namespace {
constexpr uint32_t kLineCycles = 2130;
constexpr uint32_t kVisibleCycles = 1606;
constexpr uint16_t kVisibleLines = 192;
constexpr uint16_t kFrameLines = 263;

uint16_t GetTimerIRQBit(int timer_id) {
    switch (timer_id) {
        case 0:
            return static_cast<uint16_t>(IRQBits::Timer0);
        case 1:
            return static_cast<uint16_t>(IRQBits::Timer1);
        case 2:
            return static_cast<uint16_t>(IRQBits::Timer2);
        case 3:
            return static_cast<uint16_t>(IRQBits::Timer3);
        default:
            return 0;
    }
}

uint32_t AdvanceCounter(HWTimerChannel& channel, uint32_t ticks) {
    if (ticks == 0) {
        return 0;
    }

    const uint32_t start_counter = channel.counter;
    const uint64_t total = static_cast<uint64_t>(start_counter) + ticks;
    if (total < 0x10000ull) {
        channel.counter = static_cast<uint16_t>(total);
        return 0;
    }

    const uint32_t period = 0x10000u - static_cast<uint32_t>(channel.reload);
    uint64_t remaining = total - 0x10000ull;
    uint32_t overflows = 1;
    overflows += static_cast<uint32_t>(remaining / period);
    remaining %= period;

    channel.counter = static_cast<uint16_t>(channel.reload + remaining);
    return overflows;
}

void UpdateVCountMatch(NDS2DEngine* engine, HWIRQ* irq) {
    if (engine == nullptr) {
        return;
    }

    const bool old_match = engine->IsVCountMatch();
    engine->UpdateVCountMatchFlag();
    if (!old_match && engine->IsVCountMatch() && engine->IsVCountIRQEnabled() && irq != nullptr) {
        irq->RaiseIRQ(IRQBits::VCount);
    }
}

void MirrorTimingState(NDS2DEngine* source, NDS2DEngine* target) {
    if (source == nullptr || target == nullptr) {
        return;
    }
    target->MirrorTimingFrom(*source);
}
}

void TimerCore::StepTimers(int cycles) {
    if (tmr == nullptr || irq == nullptr || cycles <= 0) {
        return;
    }

    std::array<uint32_t, 4> overflow_counts{};

    for (int i = 0; i < 4; ++i) {
        auto& channel = tmr->channels[i];
        if (!channel.running || channel.countup) {
            continue;
        }

        const uint32_t divider = TimerPrescaler::GetDivider(channel.prescaler);
        const uint64_t total_cycles = static_cast<uint64_t>(channel.cycle_remainder) +
                                      static_cast<uint64_t>(cycles);
        const uint32_t ticks = static_cast<uint32_t>(total_cycles / divider);
        channel.cycle_remainder = static_cast<uint32_t>(total_cycles % divider);

        overflow_counts[i] = AdvanceCounter(channel, ticks);
        if (overflow_counts[i] != 0 && channel.irq_en) {
            irq->RaiseIRQ(GetTimerIRQBit(i));
        }
    }

    for (int i = 1; i < 4; ++i) {
        auto& channel = tmr->channels[i];
        if (!channel.running || !channel.countup) {
            continue;
        }

        overflow_counts[i] = AdvanceCounter(channel, overflow_counts[i - 1]);
        if (overflow_counts[i] != 0 && channel.irq_en) {
            irq->RaiseIRQ(GetTimerIRQBit(i));
        }
    }
}

void TimerCore::StepVideo(int cycles) {
    if (engine_a == nullptr || cycles <= 0) {
        return;
    }

    int remaining = cycles;
    while (remaining > 0) {
        const uint32_t event_cycle = engine_a->in_hblank ? kLineCycles : kVisibleCycles;
        const uint32_t until_event = event_cycle - engine_a->scanline_cycle;
        const uint32_t step = std::min<uint32_t>(static_cast<uint32_t>(remaining), until_event);

        engine_a->scanline_cycle += step;
        remaining -= static_cast<int>(step);

        if (!engine_a->in_hblank && engine_a->scanline_cycle >= kVisibleCycles) {
            engine_a->in_hblank = true;
            engine_a->UpdateBlankFlags();
            if (engine_a->vcount < kVisibleLines && engine_a->IsHBlankIRQEnabled() && irq != nullptr) {
                irq->RaiseIRQ(IRQBits::HBlank);
            }
            MirrorTimingState(engine_a, engine_b);
        }

        if (engine_a->scanline_cycle >= kLineCycles) {
            engine_a->scanline_cycle -= kLineCycles;
            engine_a->in_hblank = false;
            engine_a->vcount = static_cast<uint16_t>((engine_a->vcount + 1u) % kFrameLines);
            engine_a->UpdateBlankFlags();

            if (engine_a->vcount == kVisibleLines) {
                if (engine_a->IsVBlankIRQEnabled() && irq != nullptr) {
                    irq->RaiseIRQ(IRQBits::VBlank);
                }
                if (irq_arm7 != nullptr) {
                    irq_arm7->RaiseIRQ(IRQBits::VBlank);
                }
            }

            UpdateVCountMatch(engine_a, irq);
            MirrorTimingState(engine_a, engine_b);
        }
    }
}

void TimerCore::CheckCascades() {
}

void TimerCore::FireTimerIRQ(int timer_id) {
    if (tmr == nullptr || irq == nullptr || timer_id < 0 || timer_id > 3) {
        return;
    }
    if (tmr->channels[timer_id].irq_en) {
        irq->RaiseIRQ(GetTimerIRQBit(timer_id));
    }
}

void TimerCore::ReloadTimer(int timer_id) {
    if (tmr == nullptr || timer_id < 0 || timer_id > 3) {
        return;
    }
    auto& channel = tmr->channels[timer_id];
    channel.counter = channel.reload;
    channel.cycle_remainder = 0;
}
