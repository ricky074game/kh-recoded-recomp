#pragma once

// ============================================================================
// hw_input.h — Nintendo DS Keypad & Touch Screen Emulation
//
// DS Input registers are active-low (1 = released, 0 = pressed).
// Provides mapping from SDL2 Keyboard/Mouse and a Virtual Controller UI
// directly into the NDS hardware memory mapping.
// ============================================================================

#include <cstdint>
#include <string>
#include <unordered_map>
#include <SDL2/SDL.h>

class InputManager {
private:
    // NDS Hardware Register State (Active Low)
    uint16_t keyinput = 0x03FF;   // 10 bits: A, B, Sel, Start, R, L, U, D, R-btn, L-btn
    uint16_t extkeyin = 0x00C3;   // X, Y, Pen, Fold (Bit 0, 1, 6, 7)
    
    // Virtual touch state
    uint16_t touch_x = 0;
    uint16_t touch_y = 0;
    uint16_t touch_adc_x = 0;
    uint16_t touch_adc_y = 0;
    
    // Configurable Key Bindings (Action Name -> SDL_Scancode)
    std::unordered_map<std::string, SDL_Scancode> bindings;
    
    // Display scaling for touch translation
    float scale_factor = 1.0f;
    int display_offset_x = 0;
    int display_offset_y = 0;

    // Touch ADC calibration (12-bit)
    float adc_scale_x = 4095.0f / 255.0f;
    float adc_scale_y = 4095.0f / 191.0f;

    // Synthetic microphone stream state for hardware polling
    bool mic_capture_enabled = false;
    uint16_t mic_lfsr = 0xACE1;

    void SetBit(uint16_t& reg, int bit, bool pressed);

public:
    InputManager();
    
    // Config & Setup
    void LoadConfig(const std::string& path);
    void SetDisplayScale(float scale, int offset_x, int offset_y);
    
    // SDL2 Event Handlers
    // Call these from your main loop SDL_PollEvent
    void HandleKeyEvent(const SDL_KeyboardEvent& key);
    void HandleMouseEvent(const SDL_MouseButtonEvent& mouse);
    void HandleMouseMotion(const SDL_MouseMotionEvent& motion);
    
    // Virtual Controller UI Check
    // Checks if the mouse coordinates fall into a drawn bounding box
    void UpdateVirtualButton(const std::string& button, bool pressed);
    
    // Hardware Memory IO
    uint16_t ReadKEYINPUT() const { return keyinput; }
    uint16_t ReadEXTKEYIN() const { return extkeyin; }
    
    // SPI bypass for touch
    uint16_t GetTouchX() const { return touch_x; }
    uint16_t GetTouchY() const { return touch_y; }
    uint16_t GetTouchADCX() const { return touch_adc_x; }
    uint16_t GetTouchADCY() const { return touch_adc_y; }
    bool     IsTouchPressed() const { return !(extkeyin & (1 << 6)); }

    // --- Phase 4 Implementation ---
    void PollMicrophone();
    void CalibrateTouchScreen();
    void HandleLidFold(bool closed);
    void ProcessAnalogInput(int16_t axis_x, int16_t axis_y);
};
