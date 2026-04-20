#include <gtest/gtest.h>
#include "hw_spi.h"

TEST(HWSpiTest, BasicOperations) {
    HwSpi::Init();
    HwSpi::WriteRegister(0x040001C0, 0x1234);
    uint16_t val = HwSpi::ReadRegister(0x040001C0);
    EXPECT_EQ(val, 0x1234); // Should read back spicnt
}
