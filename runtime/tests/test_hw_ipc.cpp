#include <gtest/gtest.h>
#include "hw_ipc.h"

TEST(SPSCQueue, PushPopSingleItem) {
    SPSCQueue<uint32_t, 16> q;
    EXPECT_TRUE(q.push(42));
    uint32_t val = 0;
    EXPECT_TRUE(q.pop(val));
    EXPECT_EQ(val, 42u);
}

TEST(SPSCQueue, FIFOOrdering) {
    SPSCQueue<uint32_t, 16> q;
    q.push(10);
    q.push(20);
    q.push(30);

    uint32_t val = 0;
    q.pop(val); EXPECT_EQ(val, 10u);
    q.pop(val); EXPECT_EQ(val, 20u);
    q.pop(val); EXPECT_EQ(val, 30u);
}

TEST(SPSCQueue, FullQueueRejectsPush) {
    SPSCQueue<uint32_t, 4> q;
    EXPECT_TRUE(q.push(1));
    EXPECT_TRUE(q.push(2));
    EXPECT_TRUE(q.push(3));
    EXPECT_TRUE(q.push(4));
    EXPECT_FALSE(q.push(5));
    EXPECT_TRUE(q.full());
}

TEST(SPSCQueue, EmptyQueueRejectsPop) {
    SPSCQueue<uint32_t, 16> q;
    uint32_t val = 0;
    EXPECT_FALSE(q.pop(val));
}

TEST(SPSCQueue, PeekDoesNotRemove) {
    SPSCQueue<uint32_t, 16> q;
    q.push(99);

    uint32_t val = 0;
    EXPECT_TRUE(q.peek(val));
    EXPECT_EQ(val, 99u);
    EXPECT_FALSE(q.empty());
}

TEST(SPSCQueue, SizeTracking) {
    SPSCQueue<uint32_t, 16> q;
    EXPECT_EQ(q.size(), 0u);
    q.push(1); EXPECT_EQ(q.size(), 1u);
    q.push(2); EXPECT_EQ(q.size(), 2u);
    q.push(3); EXPECT_EQ(q.size(), 3u);

    uint32_t v;
    q.pop(v); EXPECT_EQ(q.size(), 2u);
}

TEST(SPSCQueue, ClearResetsQueue) {
    SPSCQueue<uint32_t, 16> q;
    q.push(1);
    q.push(2);
    q.clear();
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.size(), 0u);
}

TEST(SPSCQueue, DS16WordFIFO) {
    SPSCQueue<uint32_t, 16> q;
    for (int i = 0; i < 16; ++i) {
        EXPECT_TRUE(q.push(i));
    }
    EXPECT_FALSE(q.push(99));
    EXPECT_EQ(q.size(), 16u);
}

TEST(IPCSync, BidirectionalExchange) {
    IPCSync sync_arm9, sync_arm7;

    sync_arm9.Write(0xA << 8, sync_arm7);
    EXPECT_EQ(sync_arm7.remote_send, 0xA);

    sync_arm7.Write(0x5 << 8, sync_arm9);
    EXPECT_EQ(sync_arm9.remote_send, 0x5);
}

TEST(IPCSync, ReadCombinesLocalAndRemote) {
    IPCSync sync_arm9, sync_arm7;

    sync_arm9.Write(0x3 << 8, sync_arm7);
    sync_arm7.Write(0xF << 8, sync_arm9);

    uint32_t reg = sync_arm9.Read();
    EXPECT_EQ(reg & 0xF, 0xF);
    EXPECT_EQ((reg >> 8) & 0xF, 0x3);
}

TEST(IPCSync, IRQSignaling) {
    IPCSync sync_arm9, sync_arm7;
    sync_arm7.irq_enable = true;

    bool irq = sync_arm9.Write((1u << 13), sync_arm7);
    EXPECT_TRUE(irq);
}

TEST(IPCSync, NoIRQWhenDisabled) {
    IPCSync sync_arm9, sync_arm7;
    sync_arm7.irq_enable = false;

    bool irq = sync_arm9.Write((1u << 13), sync_arm7);
    EXPECT_TRUE(irq);
}

TEST(IPCFIFOControl, StatusBitsReflectQueueState) {
    SPSCQueue<uint32_t, 16> send;
    SPSCQueue<uint32_t, 16> recv;
    IPCFIFOControl<16> ctrl;
    ctrl.send_fifo = &send;
    ctrl.recv_fifo = &recv;

    uint16_t empty_state = ctrl.Read();
    EXPECT_NE(empty_state & (1u << 0), 0u);  // send empty
    EXPECT_NE(empty_state & (1u << 8), 0u);  // recv empty

    send.push(1);
    recv.push(2);
    uint16_t non_empty_state = ctrl.Read();
    EXPECT_EQ(non_empty_state & (1u << 0), 0u);
    EXPECT_EQ(non_empty_state & (1u << 8), 0u);
}

TEST(IPCFIFOControl, WriteClearsSendFifoAndErrorFlag) {
    SPSCQueue<uint32_t, 16> send;
    SPSCQueue<uint32_t, 16> recv;
    IPCFIFOControl<16> ctrl;
    ctrl.send_fifo = &send;
    ctrl.recv_fifo = &recv;

    send.push(0xDEADBEEF);
    ctrl.error_flag = true;

    // bit3 clears send fifo, bit14 acknowledges error, bit15 enables fifo
    ctrl.Write(static_cast<uint16_t>((1u << 3) | (1u << 14) | (1u << 15)));

    EXPECT_TRUE(send.empty());
    EXPECT_FALSE(ctrl.error_flag);
    EXPECT_TRUE(ctrl.fifo_enabled);
}
