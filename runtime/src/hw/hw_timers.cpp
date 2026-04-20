#include "hw_timers.h"
#include "hw_irq.h"

void TimerCore::StepTimers(int cycles) {
    if (!tmr || !irq) return;

    bool overflowed[4] = {false, false, false, false};

    for (int i = 0; i < 4; ++i) {
        auto& channel = tmr->channels[i];

        if (!channel.running) continue;

        // Count-up (Cascade) mode is handled by CheckCascades, not here
        if (channel.countup) continue;

        uint32_t divider = TimerPrescaler::GetDivider(channel.prescaler);

        channel.cycle_remainder += cycles;

        if (channel.cycle_remainder >= divider) {
            uint64_t ticks = channel.cycle_remainder / divider;
            channel.cycle_remainder %= divider;

            // Advance the 16-bit counter
            uint64_t old_counter = channel.accumulated_ticks;
            channel.accumulated_ticks += ticks;

            // Did it overflow 16 bits? (Exceeded 0xFFFF)
            // It could overflow multiple times if 'ticks' is large,
            // but usually cycles is small per step.
            if ((old_counter & 0xFFFF) + ticks > 0xFFFF) {
                overflowed[i] = true;

                // On overflow, counter is reloaded with the reload value,
                // plus any remaining ticks that pushed it over 0xFFFF.
                uint64_t excess = ((old_counter & 0xFFFF) + ticks) - 0x10000;
                channel.accumulated_ticks = channel.reload + excess;

                FireTimerIRQ(i);
            }
        }
    }

    // Process cascading timers
    for (int i = 1; i < 4; ++i) {
        auto& channel = tmr->channels[i];
        if (channel.running && channel.countup && overflowed[i - 1]) {
            uint64_t old_counter = channel.accumulated_ticks;
            channel.accumulated_ticks += 1;

            if ((old_counter & 0xFFFF) + 1 > 0xFFFF) {
                overflowed[i] = true;
                channel.accumulated_ticks = channel.reload;
                FireTimerIRQ(i);
            }
        }
    }
}

void TimerCore::CheckCascades() {
    // Moved logic into StepTimers for synchronized overflow handling
}

void TimerCore::FireTimerIRQ(int timer_id) {
    if (!tmr || !irq) return;
    if (timer_id < 0 || timer_id > 3) return;

    if (tmr->channels[timer_id].irq_en) {
        switch(timer_id) {
            case 0: irq->RaiseIRQ(IRQBits::Timer0); break;
            case 1: irq->RaiseIRQ(IRQBits::Timer1); break;
            case 2: irq->RaiseIRQ(IRQBits::Timer2); break;
            case 3: irq->RaiseIRQ(IRQBits::Timer3); break;
        }
    }
}

void TimerCore::ReloadTimer(int timer_id) {
    if (!tmr) return;
    if (timer_id < 0 || timer_id > 3) return;
    tmr->channels[timer_id].accumulated_ticks = tmr->channels[timer_id].reload;
}
