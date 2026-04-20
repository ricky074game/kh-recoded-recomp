#include <gtest/gtest.h>
#include "hw_wifi.h"

TEST(HwWifiTest, Init) {
    EXPECT_NO_THROW(HwWifi::Init());
}

TEST(HwWifiTest, ReadWriteReg) {
    HwWifi::Init();
    HwWifi::WriteRegister(0x04804000, 0x1234);
    EXPECT_EQ(HwWifi::ReadRegister(0x04804000), 0x1234);
}

TEST(HwWifiTest, UpdateRaisesAndClearsPeriodicIRQ) {
    HwWifi::Init();
    HwWifi::WriteRegister(0x04804000, 0x0001);  // Enable MAC update path.

    for (int i = 0; i < 1024; ++i) {
        HwWifi::Update();
    }

    EXPECT_TRUE(HwWifi::IsIRQPending());
    EXPECT_NE(HwWifi::ReadRegister(0x04804012) & 0x0001, 0);

    HwWifi::ClearIRQ();
    EXPECT_FALSE(HwWifi::IsIRQPending());
}
