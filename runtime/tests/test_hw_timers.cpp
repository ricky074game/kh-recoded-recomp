#include <gtest/gtest.h>
#include "hw_timers.h"
#include "hw_irq.h"

class TimerTest : public ::testing::Test {
protected:
    HWTimerManager tmr;
    HWIRQ irq;
    TimerCore core;

    void SetUp() override {
        core.tmr = &tmr;
        core.irq = &irq;
        for (int i=0; i<4; i++) {
            tmr.channels[i].reload = 0;
            tmr.channels[i].control = 0;
            tmr.channels[i].running = false;
            tmr.channels[i].countup = false;
            tmr.channels[i].irq_en = false;
            tmr.channels[i].prescaler = 0;
            tmr.channels[i].accumulated_ticks = 0;
            tmr.channels[i].cycle_remainder = 0;
        }
        irq.if_reg = 0;
    }
};

TEST_F(TimerTest, StepTimers_Prescaler_DIV_1) {
    tmr.channels[0].reload = 0x0000;
    tmr.channels[0].WriteControl((1 << 7) | 0);

    core.StepTimers(100);
    EXPECT_EQ(tmr.channels[0].accumulated_ticks, 100);
}

TEST_F(TimerTest, StepTimers_Prescaler_DIV_64) {
    tmr.channels[0].reload = 0x0000;
    tmr.channels[0].WriteControl((1 << 7) | 1);

    core.StepTimers(64);
    EXPECT_EQ(tmr.channels[0].accumulated_ticks, 1);
    core.StepTimers(64);
    EXPECT_EQ(tmr.channels[0].accumulated_ticks, 2);
}

TEST_F(TimerTest, StepTimers_Prescaler_DIV_256) {
    tmr.channels[0].reload = 0x0000;
    tmr.channels[0].WriteControl((1 << 7) | 2);

    core.StepTimers(256);
    EXPECT_EQ(tmr.channels[0].accumulated_ticks, 1);
}

TEST_F(TimerTest, StepTimers_Prescaler_DIV_1024) {
    tmr.channels[0].reload = 0x0000;
    tmr.channels[0].WriteControl((1 << 7) | 3);

    core.StepTimers(1024);
    EXPECT_EQ(tmr.channels[0].accumulated_ticks, 1);
}

TEST_F(TimerTest, Cascade_IncrementsOnPreviousOverflow) {
    tmr.channels[0].reload = 0xFFFF;
    tmr.channels[0].WriteControl((1 << 7) | 0);

    tmr.channels[1].reload = 0x0000;
    tmr.channels[1].WriteControl((1 << 7) | (1 << 2));

    core.StepTimers(1);
    EXPECT_EQ(tmr.channels[1].accumulated_ticks, 1);
}

TEST_F(TimerTest, Cascade_MultipleCascades) {
    tmr.channels[0].reload = 0xFFFF;
    tmr.channels[0].WriteControl((1 << 7) | 0);

    tmr.channels[1].reload = 0xFFFF;
    tmr.channels[1].WriteControl((1 << 7) | (1 << 2));

    tmr.channels[2].reload = 0x0000;
    tmr.channels[2].WriteControl((1 << 7) | (1 << 2));

    core.StepTimers(1);
    EXPECT_EQ(tmr.channels[2].accumulated_ticks, 1);
}

TEST_F(TimerTest, Overflow_TriggersIRQ) {
    tmr.channels[0].reload = 0xFFFF;
    tmr.channels[0].WriteControl((1 << 7) | (1 << 6) | 0);

    core.StepTimers(1);
    EXPECT_TRUE(irq.if_reg & IRQBits::Timer0);
}

TEST_F(TimerTest, Overflow_ReloadsCorrectly) {
    tmr.channels[0].reload = 0x1234;
    tmr.channels[0].WriteControl((1 << 7) | 0);
    // Force counter close to overflow manually (as if it had been running)
    tmr.channels[0].accumulated_ticks = 0xFFFF;

    core.StepTimers(1);
    // Overflows, so should reload + excess (which is 0)
    EXPECT_EQ(tmr.channels[0].accumulated_ticks, 0x1234);
}
