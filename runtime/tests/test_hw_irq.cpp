#include <gtest/gtest.h>
#include "hw_irq.h"

TEST(IRQTest, PendingRequiresIMEAndEnabledBit) {
    HWIRQ irq;
    irq.RaiseIRQ(IRQBits::VBlank);
    irq.ie = IRQBits::VBlank;

    irq.ime = 0;
    EXPECT_FALSE(irq.HasPendingIRQ());

    irq.ime = 1;
    EXPECT_TRUE(irq.HasPendingIRQ());
}

TEST(IRQTest, AcknowledgeClearsOnlySelectedBits) {
    HWIRQ irq;
    irq.RaiseIRQ(IRQBits::VBlank | IRQBits::Timer0 | IRQBits::Timer1);
    irq.AcknowledgeIRQ(IRQBits::Timer0);

    EXPECT_EQ(irq.if_reg, (IRQBits::VBlank | IRQBits::Timer1));
}

TEST(IRQTest, CheckInterruptsVectorsToIRQMode) {
    HWIRQ irq;
    irq.ime = 1;
    irq.ie = IRQBits::VBlank;
    irq.RaiseIRQ(IRQBits::VBlank);

    CPU_Context ctx{};
    ctx.cpsr = ARMMode::SYS; // IRQ enabled (I-bit clear)
    ctx.r[15] = 0x02000100;

    CheckInterrupts(&ctx, irq);

    EXPECT_EQ(ctx.spsr_irq, ARMMode::SYS);
    EXPECT_EQ(ctx.r14_irq, 0x02000100u);
    EXPECT_EQ(ctx.cpsr & 0x1Fu, ARMMode::IRQ);
    EXPECT_NE(ctx.cpsr & CPSRFlags::I, 0u);
    EXPECT_EQ(ctx.r[15], 0x00000018u);
}

TEST(IRQTest, CheckInterruptsUsesARM9HighVectorAndLROffsetWhenRequested) {
    HWIRQ irq;
    irq.ime = 1;
    irq.ie = IRQBits::VBlank;
    irq.RaiseIRQ(IRQBits::VBlank);

    CPU_Context ctx{};
    ctx.cpsr = ARMMode::SYS; // IRQ enabled (I-bit clear)
    ctx.cp15_control = 0x2000; // ARM9 V=1 (high vectors)
    ctx.r[15] = 0x02000100;

    CheckInterrupts(&ctx, irq, 4, true);

    EXPECT_EQ(ctx.spsr_irq, ARMMode::SYS);
    EXPECT_EQ(ctx.r14_irq, 0x02000104u);
    EXPECT_EQ(ctx.cpsr & 0x1Fu, ARMMode::IRQ);
    EXPECT_NE(ctx.cpsr & CPSRFlags::I, 0u);
    EXPECT_EQ(ctx.r[15], 0xFFFF0018u);
}

TEST(IRQTest, CheckInterruptsSkipsWhenIBitSet) {
    HWIRQ irq;
    irq.ime = 1;
    irq.ie = IRQBits::VBlank;
    irq.RaiseIRQ(IRQBits::VBlank);

    CPU_Context ctx{};
    ctx.cpsr = ARMMode::SYS | CPSRFlags::I;
    ctx.r[15] = 0x02001000;

    CheckInterrupts(&ctx, irq);

    EXPECT_EQ(ctx.r[15], 0x02001000u);
    EXPECT_EQ(ctx.cpsr & 0x1Fu, ARMMode::SYS);
}
