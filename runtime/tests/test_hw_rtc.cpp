#include <gtest/gtest.h>
#include "hw_rtc.h"

TEST(HWRtcTest, SPIReadWrite) {
    HwRtc::Init();
    HwRtc::WriteSPI(0x01); // Read Command
    uint8_t data = HwRtc::ReadSPI();
    EXPECT_EQ(data, 0); // Stubbed implementation check
}
