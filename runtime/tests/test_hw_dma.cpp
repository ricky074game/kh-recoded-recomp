#include <gtest/gtest.h>
#include "hw_dma.h"
#include "hw_irq.h"

class DMATest : public ::testing::Test {
protected:
    DMACore core;
    HWIRQ irq;
};

extern uint8_t* g_memory_base;
extern uint32_t g_memory_size;

TEST_F(DMATest, DMA_ExecuteMemCopy_Increment) {
    DMAChannel d;
    d.src_addr = 0x0;
    d.dst_addr = 0x10;
    d.word_count = 2; // 2 words
    d.word_size32 = true; // 32 bit = 4 bytes per word. So 8 bytes total.
    d.src_ctrl = DMAAddrCtrl::Increment;
    d.dst_ctrl = DMAAddrCtrl::Increment;
    d.enabled = true;

    uint8_t mem[32] = {0};
    mem[0] = 0x11; mem[1] = 0x22; mem[2] = 0x33; mem[3] = 0x44;
    mem[4] = 0x55; mem[5] = 0x66; mem[6] = 0x77; mem[7] = 0x88;

    d.Execute(mem, 32);

    EXPECT_EQ(mem[0x10], 0x11);
    EXPECT_EQ(mem[0x13], 0x44);
    EXPECT_EQ(mem[0x14], 0x55);
    EXPECT_EQ(mem[0x17], 0x88);
    EXPECT_FALSE(d.enabled); // Should complete
}

TEST_F(DMATest, DMA_ExecuteMemCopy_Decrement) {
    DMAChannel d;
    d.src_addr = 0x4; // Start at 0x4, copy downwards
    d.dst_addr = 0x14; // Dest at 0x14, copy downwards
    d.word_count = 2; // 2 words
    d.word_size32 = true;
    d.src_ctrl = DMAAddrCtrl::Decrement;
    d.dst_ctrl = DMAAddrCtrl::Decrement;
    d.enabled = true;

    uint8_t mem[32] = {0};
    mem[4] = 0x11; mem[5] = 0x22; mem[6] = 0x33; mem[7] = 0x44;
    mem[0] = 0x55; mem[1] = 0x66; mem[2] = 0x77; mem[3] = 0x88;

    d.Execute(mem, 32);

    // It should first copy from 0x4 to 0x14 (4 bytes), then decrement addresses by 4.
    // So next it copies from 0x0 to 0x10 (4 bytes).
    EXPECT_EQ(mem[0x14], 0x11);
    EXPECT_EQ(mem[0x17], 0x44);
    EXPECT_EQ(mem[0x10], 0x55);
    EXPECT_EQ(mem[0x13], 0x88);
}

TEST_F(DMATest, DMA_ExecuteMemCopy_Fixed) {
    DMAChannel d;
    d.src_addr = 0x0; // Fixed source
    d.dst_addr = 0x10; // Increment dest
    d.word_count = 2; // 2 words
    d.word_size32 = true;
    d.src_ctrl = DMAAddrCtrl::Fixed;
    d.dst_ctrl = DMAAddrCtrl::Increment;
    d.enabled = true;

    uint8_t mem[32] = {0};
    mem[0] = 0x11; mem[1] = 0x22; mem[2] = 0x33; mem[3] = 0x44;
    // src is fixed, so it should copy the same 4 bytes twice.

    d.Execute(mem, 32);

    EXPECT_EQ(mem[0x10], 0x11);
    EXPECT_EQ(mem[0x14], 0x11); // Second word should be the same
}

TEST_F(DMATest, DMA_ExecuteMemCopy_IncrReload) {
    DMAChannel d;
    d.src_addr = 0x0;
    d.dst_addr = 0x10;
    d.word_count = 2;
    d.word_size32 = true;
    d.src_ctrl = DMAAddrCtrl::Increment;
    d.dst_ctrl = DMAAddrCtrl::IncrReload;
    d.repeat = true; // Repeat must be true for IncrReload to happen
    d.enabled = true;

    uint8_t mem[32] = {0};

    d.Execute(mem, 32);

    // In actual repeat logic, dst_addr gets reloaded to its original latched value.
    // The test in execute doesn't have the latch, it says: "Would reload from latch in real HW"
    // So we just check that enabled stays true
    EXPECT_TRUE(d.enabled);
}

TEST_F(DMATest, DMA_WordSize_16bit) {
    DMAChannel d;
    d.src_addr = 0x0;
    d.dst_addr = 0x10;
    d.word_count = 2; // 2 * 16-bit words = 4 bytes
    d.word_size32 = false; // 16 bit
    d.src_ctrl = DMAAddrCtrl::Increment;
    d.dst_ctrl = DMAAddrCtrl::Increment;
    d.enabled = true;

    uint8_t mem[32] = {0};
    mem[0] = 0x11; mem[1] = 0x22;
    mem[2] = 0x33; mem[3] = 0x44;
    mem[4] = 0x55; // Should not be copied

    d.Execute(mem, 32);

    EXPECT_EQ(mem[0x10], 0x11);
    EXPECT_EQ(mem[0x11], 0x22);
    EXPECT_EQ(mem[0x12], 0x33);
    EXPECT_EQ(mem[0x13], 0x44);
    EXPECT_EQ(mem[0x14], 0x00); // Beyond 4 bytes copied
}

TEST_F(DMATest, DMA_BoundsCheckFails) {
    DMAChannel d;
    d.src_addr = 0x0;
    d.dst_addr = 0x10;
    d.word_count = 10; // 10 * 4 = 40 bytes. Exceeds mem size of 32
    d.word_size32 = true;
    d.enabled = true;

    uint8_t mem[32] = {0};

    d.Execute(mem, 32);

    // Because bounds check fails, it should disable without copying
    EXPECT_FALSE(d.enabled);
}

TEST_F(DMATest, DMA_WriteControlDecodesFields) {
    DMAChannel d;
    const uint32_t reg =
        (0x1234u) |
        (static_cast<uint32_t>(DMAAddrCtrl::Fixed) << 21) |
        (static_cast<uint32_t>(DMAAddrCtrl::Decrement) << 23) |
        (1u << 25) |
        (1u << 26) |
        (static_cast<uint32_t>(DMAStartTiming::VBlank) << 27) |
        (1u << 30) |
        (1u << 31);

    d.WriteControl(reg);
    EXPECT_EQ(d.word_count, 0x1234u);
    EXPECT_EQ(d.dst_ctrl, DMAAddrCtrl::Fixed);
    EXPECT_EQ(d.src_ctrl, DMAAddrCtrl::Decrement);
    EXPECT_TRUE(d.repeat);
    EXPECT_TRUE(d.word_size32);
    EXPECT_EQ(d.timing, DMAStartTiming::VBlank);
    EXPECT_TRUE(d.irq_on_end);
    EXPECT_TRUE(d.enabled);
}

TEST_F(DMATest, DMACoreRunDMATransferRaisesIRQ) {
    uint8_t mem[64] = {};
    mem[0] = 0xAA;
    mem[1] = 0xBB;
    mem[2] = 0xCC;
    mem[3] = 0xDD;

    g_memory_base = mem;
    g_memory_size = sizeof(mem);

    core.irq = &irq;
    DMAChannel& ch = core.dma_channels[0];
    ch.src_addr = 0;
    ch.dst_addr = 16;
    ch.word_count = 1;
    ch.word_size32 = true;
    ch.src_ctrl = DMAAddrCtrl::Increment;
    ch.dst_ctrl = DMAAddrCtrl::Increment;
    ch.enabled = true;
    ch.irq_on_end = true;

    core.RunDMATransfer(0);

    EXPECT_EQ(mem[16], 0xAA);
    EXPECT_EQ(mem[19], 0xDD);
    EXPECT_NE(irq.if_reg & IRQBits::DMA0, 0u);
    EXPECT_FALSE(ch.enabled);
}
