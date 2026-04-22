#pragma once

#include <cstdint>

struct CPU_Context;

namespace HwBios {
    void SetActiveContext(CPU_Context* ctx);
    CPU_Context* GetActiveContext();

    void HandleARM9_SWI(uint32_t swi_number);
    void HandleARM7_SWI(uint32_t swi_number);
}
