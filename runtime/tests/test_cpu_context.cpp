#include <gtest/gtest.h>
#include "cpu_context.h"
#include "memory_map.h"

// ============================================================================
// CPU Context Tests — Mode switching, SPSR, initialization, banked registers
// ============================================================================

class CPUContextTest : public ::testing::Test {
protected:
    NDSMemory mem;
    CPU_Context ctx = {};

    void SetUp() override {
        ctx.mem = &mem;
    }
};

// ---- Basic Initialization ----

TEST_F(CPUContextTest, AllRegistersZeroOnInit) {
    for (int i = 0; i < 16; ++i) {
        EXPECT_EQ(ctx.r[i], 0u) << "r[" << i << "] should be zero";
    }
    EXPECT_EQ(ctx.cpsr, 0u);
}

TEST_F(CPUContextTest, BankedRegistersZeroOnInit) {
    EXPECT_EQ(ctx.r13_irq, 0u);
    EXPECT_EQ(ctx.r14_irq, 0u);
    EXPECT_EQ(ctx.r13_svc, 0u);
    EXPECT_EQ(ctx.r14_svc, 0u);
    EXPECT_EQ(ctx.r8_fiq, 0u);
    EXPECT_EQ(ctx.r13_fiq, 0u);
    EXPECT_EQ(ctx.r13_abt, 0u);
    EXPECT_EQ(ctx.r13_und, 0u);
}

TEST_F(CPUContextTest, CP15RegistersZeroOnInit) {
    EXPECT_EQ(ctx.cp15_control, 0u);
    EXPECT_EQ(ctx.cp15_dtcm_base, 0u);
    EXPECT_EQ(ctx.cp15_itcm_base, 0u);
}

TEST_F(CPUContextTest, MemoryPointerAccess) {
    ctx.mem->Write32(0x02000000, 0x1337BEEF);
    EXPECT_EQ(ctx.mem->Read32(0x02000000), 0x1337BEEF);
}

// ---- SPSR Access ----

TEST_F(CPUContextTest, SPSRAccessInIRQMode) {
    ctx.cpsr = ARMMode::IRQ;
    ctx.SetSPSR(0xDEADBEEF);
    EXPECT_EQ(ctx.GetSPSR(), 0xDEADBEEF);
    EXPECT_EQ(ctx.spsr_irq, 0xDEADBEEF);
}

TEST_F(CPUContextTest, SPSRAccessInSVCMode) {
    ctx.cpsr = ARMMode::SVC;
    ctx.SetSPSR(0xCAFEBABE);
    EXPECT_EQ(ctx.spsr_svc, 0xCAFEBABE);
}

TEST_F(CPUContextTest, SPSRAccessInFIQMode) {
    ctx.cpsr = ARMMode::FIQ;
    ctx.SetSPSR(0x12345678);
    EXPECT_EQ(ctx.spsr_fiq, 0x12345678);
}

TEST_F(CPUContextTest, SPSRReturnsCPSRInUserMode) {
    ctx.cpsr = ARMMode::USR | 0xFF000000;
    EXPECT_EQ(ctx.GetSPSR(), ctx.cpsr);
}

// ---- Mode Switching ----

TEST_F(CPUContextTest, GetModeReturnsCorrectBits) {
    ctx.cpsr = ARMMode::IRQ | CPSRFlags::I;
    EXPECT_EQ(ctx.GetMode(), ARMMode::IRQ);
}

TEST_F(CPUContextTest, SwitchModeBanksRegisters) {
    ctx.cpsr = ARMMode::SVC;
    ctx.r[13] = 0xAAAAAAAA; // SVC SP
    ctx.r[14] = 0xBBBBBBBB; // SVC LR

    ctx.SwitchMode(ARMMode::IRQ);
    EXPECT_EQ(ctx.GetMode(), ARMMode::IRQ);

    // Old SVC registers should be banked
    EXPECT_EQ(ctx.r13_svc, 0xAAAAAAAA);
    EXPECT_EQ(ctx.r14_svc, 0xBBBBBBBB);

    // IRQ registers should be restored (zero since never set)
    EXPECT_EQ(ctx.r[13], 0u);
    EXPECT_EQ(ctx.r[14], 0u);
}

TEST_F(CPUContextTest, SwitchModeRoundTrip) {
    ctx.cpsr = ARMMode::SVC;
    ctx.r[13] = 0x12345678;

    ctx.SwitchMode(ARMMode::IRQ);
    ctx.r[13] = 0xABCDEF00;

    ctx.SwitchMode(ARMMode::SVC);
    EXPECT_EQ(ctx.r[13], 0x12345678) << "SVC SP should be restored";
}

TEST_F(CPUContextTest, SwitchModeNoOpForSameMode) {
    ctx.cpsr = ARMMode::IRQ;
    ctx.r[13] = 0x11111111;
    ctx.SwitchMode(ARMMode::IRQ); // Same mode
    EXPECT_EQ(ctx.r[13], 0x11111111) << "Should be unchanged";
}

TEST_F(CPUContextTest, FIQModeBanksR8ThroughR14) {
    ctx.cpsr = ARMMode::SYS;
    ctx.r[8]  = 0x08;
    ctx.r[9]  = 0x09;
    ctx.r[10] = 0x0A;
    ctx.r[11] = 0x0B;
    ctx.r[12] = 0x0C;

    ctx.SwitchMode(ARMMode::FIQ);
    // FIQ registers are zero
    EXPECT_EQ(ctx.r[8], 0u);
    EXPECT_EQ(ctx.r[12], 0u);
}

// ---- NDS9 Boot Initialization ----

TEST_F(CPUContextTest, NDS9BootState) {
    ctx.InitializeNDS9();

    // Should be in SVC mode with IRQs disabled
    EXPECT_EQ(ctx.GetMode(), ARMMode::SVC);
    EXPECT_TRUE(ctx.cpsr & CPSRFlags::I);
    EXPECT_TRUE(ctx.cpsr & CPSRFlags::F);

    // PC should point to ARM9 entry
    EXPECT_EQ(ctx.r[15], 0x02000000u);

    // SVC SP should be set
    EXPECT_NE(ctx.r[13], 0u);
}

TEST_F(CPUContextTest, NDS7BootState) {
    ctx.InitializeNDS7();
    EXPECT_EQ(ctx.GetMode(), ARMMode::SVC);
    EXPECT_EQ(ctx.r[15], 0x03800000u);
}
