#pragma once

#include <cstdint>

namespace HwRtc {
    void Init();
    uint8_t ReadSPI();
    void WriteSPI(uint8_t value);
}
