#include <gtest/gtest.h>
#include "hw_bios.h"
#include <stdexcept>

TEST(HWBiosTest, SWIDispatch) {
    EXPECT_THROW(HwBios::HandleARM9_SWI(0x11), std::runtime_error);
    EXPECT_NO_THROW(HwBios::HandleARM9_SWI(0x09));
    EXPECT_NO_THROW(HwBios::HandleARM9_SWI(0x0D));
    EXPECT_NO_THROW(HwBios::HandleARM7_SWI(0x06)); // Halt test
}
