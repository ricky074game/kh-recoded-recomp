#pragma once

#include <cstdint>

namespace HwMic {
    void Init();
    void SetCaptureEnabled(bool enabled);
    void PushSample(uint8_t sample);
    uint8_t ReadData();
}
