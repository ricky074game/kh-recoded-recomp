#pragma once

#include <cstdint>

namespace HwSpi {
    void Init();
    uint16_t ReadRegister(uint32_t address);
    void WriteRegister(uint32_t address, uint16_t value);
}
