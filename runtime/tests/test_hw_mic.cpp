#include <gtest/gtest.h>
#include "hw_mic.h"

TEST(HwMicTest, DisabledCaptureReturnsIdleLevel) {
    HwMic::Init();
    HwMic::SetCaptureEnabled(false);
    EXPECT_EQ(HwMic::ReadData(), 0x80);
}

TEST(HwMicTest, QueuedSamplesAreReadInOrder) {
    HwMic::Init();
    HwMic::SetCaptureEnabled(true);

    HwMic::PushSample(0x12);
    HwMic::PushSample(0x34);

    EXPECT_EQ(HwMic::ReadData(), 0x12);
    EXPECT_EQ(HwMic::ReadData(), 0x34);
}

TEST(HwMicTest, EmptyQueueGeneratesNoiseSamples) {
    HwMic::Init();
    HwMic::SetCaptureEnabled(true);

    const uint8_t sample = HwMic::ReadData();
    EXPECT_GE(sample, 0x70);
    EXPECT_LE(sample, 0x8F);
}
