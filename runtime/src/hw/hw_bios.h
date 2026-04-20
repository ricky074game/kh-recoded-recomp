#pragma once

#include <cstdint>

namespace HwBios {
    void HandleARM9_SWI(uint32_t swi_number);
    void HandleARM7_SWI(uint32_t swi_number);
}
