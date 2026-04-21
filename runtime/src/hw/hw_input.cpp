#include "hw_input.h"
#include "hw_mic.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <QKeyEvent>
#include <QMouseEvent>

namespace {
uint16_t ClampTouchCoordinate(int value, int max_value) {
    if (value < 0) return 0;
    if (value > max_value) return static_cast<uint16_t>(max_value);
    return static_cast<uint16_t>(value);
}

Qt::Key StringToQtKey(const std::string& val) {
    std::string upper = val;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    if (upper == "SPACE") return Qt::Key_Space;
    if (upper == "Z") return Qt::Key_Z;
    if (upper == "X") return Qt::Key_X;
    if (upper == "C") return Qt::Key_C;
    if (upper == "A") return Qt::Key_A;
    if (upper == "S") return Qt::Key_S;
    if (upper == "RETURN") return Qt::Key_Return;
    if (upper == "ENTER") return Qt::Key_Return;
    if (upper == "RIGHT") return Qt::Key_Right;
    if (upper == "LEFT") return Qt::Key_Left;
    if (upper == "UP") return Qt::Key_Up;
    if (upper == "DOWN") return Qt::Key_Down;
    if (upper == "RSHIFT") return Qt::Key_Shift;
    return Qt::Key_unknown;
}
}

InputManager::InputManager() {
    // Default Qt bindings
    bindings["A"] = Qt::Key_Space;
    bindings["B"] = Qt::Key_Z;
    bindings["Select"] = Qt::Key_Shift;
    bindings["Start"] = Qt::Key_Return;
    bindings["Right"] = Qt::Key_Right;
    bindings["Left"] = Qt::Key_Left;
    bindings["Up"] = Qt::Key_Up;
    bindings["Down"] = Qt::Key_Down;
    bindings["R"] = Qt::Key_S;
    bindings["L"] = Qt::Key_A;
    bindings["X"] = Qt::Key_X;
    bindings["Y"] = Qt::Key_C;
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
            
            Qt::Key key_code = StringToQtKey(val);
            if (key_code != Qt::Key_unknown) {
                bindings[key] = key_code;
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

void InputManager::HandleKeyEvent(QKeyEvent* event, bool pressed) {
    int code = event->key();
    
    for (const auto& [btn, key_code] : bindings) {
        if (code == static_cast<int>(key_code)) {
            UpdateVirtualButton(btn, pressed);
        }
    }
}

void InputManager::HandleMouseEvent(QMouseEvent* event, bool pressed) {
    if (event->button() == Qt::LeftButton) {
        SetBit(extkeyin, 6, pressed); // Pen bit
        
        if (pressed) {
            // Update touch X/Y
            int raw_x = event->position().x() - display_offset_x;
            int raw_y = event->position().y() - display_offset_y;
            
            float unscaled_x = raw_x / scale_factor;
            float unscaled_y = raw_y / scale_factor;
            
            // Clamp to DS screen
            touch_x = ClampTouchCoordinate(static_cast<int>(unscaled_x), 255);
            touch_y = ClampTouchCoordinate(static_cast<int>(unscaled_y), 191);
            CalibrateTouchScreen();
        }
    }
}

void InputManager::HandleMouseMotion(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) {
        int raw_x = event->position().x() - display_offset_x;
        int raw_y = event->position().y() - display_offset_y;
        
        float unscaled_x = raw_x / scale_factor;
        float unscaled_y = raw_y / scale_factor;
        
        touch_x = ClampTouchCoordinate(static_cast<int>(unscaled_x), 255);
        touch_y = ClampTouchCoordinate(static_cast<int>(unscaled_y), 191);
        CalibrateTouchScreen();
    }
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
