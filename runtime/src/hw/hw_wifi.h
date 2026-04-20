#pragma once

#include <cstdint>

namespace HwWifi {
    void Init();
    void Update();
    uint16_t ReadRegister(uint32_t address);
    void WriteRegister(uint32_t address, uint16_t value);
    bool IsIRQPending();
    void ClearIRQ();
}
