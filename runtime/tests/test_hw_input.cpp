#include <gtest/gtest.h>
#include "hw_input.h"
#include "hw_mic.h"

TEST(InputTest, ActiveLowInitialization) {
    InputManager mgr;
    // KEYINPUT default unpressed is 0x03FF
    EXPECT_EQ(mgr.ReadKEYINPUT(), 0x03FF);
    // EXTKEYIN default unpressed is 0x00C3
    EXPECT_EQ(mgr.ReadEXTKEYIN(), 0x00C3);
}

TEST(InputTest, VirtualButtonPress) {
    InputManager mgr;
    
    // Press 'A' (bit 0)
    mgr.UpdateVirtualButton("A", true);
    // 0x03FF & ~1 = 0x03FE
    EXPECT_EQ(mgr.ReadKEYINPUT(), 0x03FE);
    
    // Release 'A'
    mgr.UpdateVirtualButton("A", false);
    EXPECT_EQ(mgr.ReadKEYINPUT(), 0x03FF);
}

TEST(InputTest, TouchScaling) {
    InputManager mgr;
    mgr.SetDisplayScale(2.0f, 100, 50); // Offset X: 100, Offset Y: 50, Scale: 2x
    
    SDL_MouseButtonEvent btn;
    btn.button = SDL_BUTTON_LEFT;
    btn.state = SDL_PRESSED;
    btn.x = 100; // Left edge of screen
    btn.y = 50;  // Top edge of screen
    
    mgr.HandleMouseEvent(btn);
    EXPECT_EQ(mgr.GetTouchX(), 0);
    EXPECT_EQ(mgr.GetTouchY(), 0);
    EXPECT_TRUE(mgr.IsTouchPressed());
    
    btn.x = 100 + (255 * 2) + 50; // Off screen
    mgr.HandleMouseEvent(btn);
    EXPECT_EQ(mgr.GetTouchX(), 255); // Clamped
}

TEST(InputTest, TouchCalibrationMapsToADC) {
    InputManager mgr;
    mgr.SetDisplayScale(1.0f, 0, 0);

    SDL_MouseButtonEvent btn{};
    btn.button = SDL_BUTTON_LEFT;
    btn.state = SDL_PRESSED;
    btn.x = 128;
    btn.y = 96;

    mgr.HandleMouseEvent(btn);

    EXPECT_EQ(mgr.GetTouchX(), 128);
    EXPECT_EQ(mgr.GetTouchY(), 96);
    EXPECT_EQ(mgr.GetTouchADCX(), static_cast<uint16_t>(128.0f * (4095.0f / 255.0f)));
    EXPECT_EQ(mgr.GetTouchADCY(), static_cast<uint16_t>(96.0f * (4095.0f / 191.0f)));
}

TEST(InputTest, LidFoldBitIsActiveLow) {
    InputManager mgr;

    mgr.HandleLidFold(true);
    EXPECT_EQ((mgr.ReadEXTKEYIN() >> 7) & 1, 0u);

    mgr.HandleLidFold(false);
    EXPECT_EQ((mgr.ReadEXTKEYIN() >> 7) & 1, 1u);
}

TEST(InputTest, AnalogInputMapsToDPad) {
    InputManager mgr;

    mgr.ProcessAnalogInput(12000, -12000);
    EXPECT_EQ((mgr.ReadKEYINPUT() >> 4) & 1, 0u); // Right pressed
    EXPECT_EQ((mgr.ReadKEYINPUT() >> 6) & 1, 0u); // Up pressed

    mgr.ProcessAnalogInput(0, 0);
    EXPECT_EQ((mgr.ReadKEYINPUT() >> 4) & 1, 1u);
    EXPECT_EQ((mgr.ReadKEYINPUT() >> 6) & 1, 1u);
}

TEST(InputTest, PollMicrophonePushesSample) {
    HwMic::Init();

    InputManager mgr;
    mgr.PollMicrophone();
    const uint8_t sample = HwMic::ReadData();

    EXPECT_GE(sample, 0x70);
    EXPECT_LE(sample, 0x8F);
}
