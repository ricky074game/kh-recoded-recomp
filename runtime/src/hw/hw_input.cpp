#include "hw_input.h"
#include "hw_mic.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>

namespace {
uint16_t ClampTouchCoordinate(int value, int max_value) {
    if (value < 0) return 0;
    if (value > max_value) return static_cast<uint16_t>(max_value);
    return static_cast<uint16_t>(value);
}
}

InputManager::InputManager() {
    // Default bindings
    bindings["A"] = SDL_SCANCODE_SPACE;
    bindings["B"] = SDL_SCANCODE_Z;
    bindings["Select"] = SDL_SCANCODE_RSHIFT;
    bindings["Start"] = SDL_SCANCODE_RETURN;
    bindings["Right"] = SDL_SCANCODE_RIGHT;
    bindings["Left"] = SDL_SCANCODE_LEFT;
    bindings["Up"] = SDL_SCANCODE_UP;
    bindings["Down"] = SDL_SCANCODE_DOWN;
    bindings["R"] = SDL_SCANCODE_S;
    bindings["L"] = SDL_SCANCODE_A;
    bindings["X"] = SDL_SCANCODE_X;
    bindings["Y"] = SDL_SCANCODE_C;
}

void InputManager::LoadConfig(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        auto delim = line.find('=');
        if (delim != std::string::npos) {
            std::string key = line.substr(0, delim);
            std::string val = line.substr(delim + 1);
            
            // Remove whitespace
            key.erase(std::remove_if(key.begin(), key.end(), ::isspace), key.end());
            val.erase(std::remove_if(val.begin(), val.end(), ::isspace), val.end());
            
            SDL_Scancode scancode = SDL_GetScancodeFromName(val.c_str());
            if (scancode != SDL_SCANCODE_UNKNOWN) {
                bindings[key] = scancode;
            }
        }
    }
}

void InputManager::SetDisplayScale(float scale, int offset_x, int offset_y) {
    scale_factor = scale;
    display_offset_x = offset_x;
    display_offset_y = offset_y;
}

void InputManager::SetBit(uint16_t& reg, int bit, bool pressed) {
    if (pressed) {
        reg &= ~(1 << bit); // Active low: clear bit to 0
    } else {
        reg |= (1 << bit);  // Released: set bit to 1
    }
}

void InputManager::UpdateVirtualButton(const std::string& button, bool pressed) {
    if (button == "A")      SetBit(keyinput, 0, pressed);
    else if (button == "B") SetBit(keyinput, 1, pressed);
    else if (button == "Select") SetBit(keyinput, 2, pressed);
    else if (button == "Start") SetBit(keyinput, 3, pressed);
    else if (button == "Right") SetBit(keyinput, 4, pressed);
    else if (button == "Left") SetBit(keyinput, 5, pressed);
    else if (button == "Up") SetBit(keyinput, 6, pressed);
    else if (button == "Down") SetBit(keyinput, 7, pressed);
    else if (button == "R") SetBit(keyinput, 8, pressed);
    else if (button == "L") SetBit(keyinput, 9, pressed);
    else if (button == "X") SetBit(extkeyin, 0, pressed);
    else if (button == "Y") SetBit(extkeyin, 1, pressed);
}

void InputManager::HandleKeyEvent(const SDL_KeyboardEvent& key) {
    bool pressed = (key.state == SDL_PRESSED);
    SDL_Scancode code = key.keysym.scancode;
    
    for (const auto& [btn, scancode] : bindings) {
        if (code == scancode) {
            UpdateVirtualButton(btn, pressed);
        }
    }
}

void InputManager::HandleMouseEvent(const SDL_MouseButtonEvent& mouse) {
    if (mouse.button == SDL_BUTTON_LEFT) {
        HandleTouchPoint(mouse.x, mouse.y, mouse.state == SDL_PRESSED);
    }
}

void InputManager::HandleMouseMotion(const SDL_MouseMotionEvent& motion) {
    if (motion.state & SDL_BUTTON_LMASK) {
        HandleTouchPoint(motion.x, motion.y, true);
    }
}

void InputManager::HandleTouchPoint(int x, int y, bool pressed) {
    SetBit(extkeyin, 6, pressed); // Pen bit

    if (!pressed) {
        return;
    }

    const int raw_x = x - display_offset_x;
    const int raw_y = y - display_offset_y;

    const float unscaled_x = raw_x / scale_factor;
    const float unscaled_y = raw_y / scale_factor;

    // Clamp to DS screen.
    touch_x = ClampTouchCoordinate(static_cast<int>(unscaled_x), 255);
    touch_y = ClampTouchCoordinate(static_cast<int>(unscaled_y), 191);
    CalibrateTouchScreen();
}


// ============================================================================
// Phase 4: Graphics & Input
// ============================================================================

void InputManager::PollMicrophone() {
    if (!mic_capture_enabled) {
        HwMic::SetCaptureEnabled(true);
        mic_capture_enabled = true;
    }

    // 16-bit LFSR noise source centered around quiet line (0x80).
    const uint16_t feedback = static_cast<uint16_t>((mic_lfsr ^ (mic_lfsr >> 1)) & 1u);
    mic_lfsr = static_cast<uint16_t>((mic_lfsr >> 1) | (feedback << 15));
    const uint8_t sample = static_cast<uint8_t>(0x70u + (mic_lfsr & 0x1Fu));

    HwMic::PushSample(sample);
}

void InputManager::CalibrateTouchScreen() {
    // Convert screen-space touch into 12-bit ADC coordinates without mutating
    // the original DS screen-space (0..255, 0..191) state.
    const float scaled_x = static_cast<float>(touch_x) * adc_scale_x;
    const float scaled_y = static_cast<float>(touch_y) * adc_scale_y;

    touch_adc_x = ClampTouchCoordinate(static_cast<int>(scaled_x), 4095);
    touch_adc_y = ClampTouchCoordinate(static_cast<int>(scaled_y), 4095);
}

void InputManager::HandleLidFold(bool closed) {
    // Bit 7 of EXTKEYIN is the Fold (Lid) bit. Active low.
    // 0 = folded (closed), 1 = open.
    if (closed) {
        extkeyin &= ~(1 << 7);
    } else {
        extkeyin |= (1 << 7);
    }
}

void InputManager::ProcessAnalogInput(int16_t axis_x, int16_t axis_y) {
    // PC Gamepad Analog to DS D-Pad mapper.
    // DS Keypad is mapped to KEYINPUT (Bits 4,5,6,7 -> Right, Left, Up, Down).
    // Active low (0 = pressed, 1 = released).
    const int deadzone = 8000;

    // Reset D-Pad bits (set them to 1 / released first)
    keyinput |= (1 << 4); // Right
    keyinput |= (1 << 5); // Left
    keyinput |= (1 << 6); // Up
    keyinput |= (1 << 7); // Down

    // Apply exact deadzone integer logic
    if (axis_x > deadzone) {
        keyinput &= ~(1 << 4); // Press Right
    } else if (axis_x < -deadzone) {
        keyinput &= ~(1 << 5); // Press Left
    }

    if (axis_y > deadzone) {
        keyinput &= ~(1 << 7); // Press Down
    } else if (axis_y < -deadzone) {
        keyinput &= ~(1 << 6); // Press Up
    }
}
