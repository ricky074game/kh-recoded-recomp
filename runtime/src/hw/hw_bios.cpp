#include "hw_bios.h"
#include "cpu_context.h"
#include "hw_math.h"
#include <iostream>
#include <stdexcept>

namespace HwBios {
    void HandleARM9_SWI(uint32_t swi_number) {
        switch (swi_number) {
            case 0x11: // LZ77UnCompWrite8
            case 0x12: // LZ77UnCompWrite16
                // In a real environment, read r0 as source pointer, r1 as dest, and emulate the unpacking
                // We'll throw an unimplemented error to signal that the host OS must actually hook this
                throw std::runtime_error("LZ77 Decompression SWI 0x11/0x12 not fully implemented natively.");
            case 0x09: // Div
                ComputeDiv32();
                break;
            case 0x0D: // Sqrt
                ComputeSqrt32();
                break;
            default:
                break;
        }
    }

    void HandleARM7_SWI(uint32_t swi_number) {
        switch (swi_number) {
            case 0x06: // Halt
                // CPU halts execution until an IRQ fires.
                // Depending on the threading model, we might yield the CPU context
                break;
            case 0x07: // Stop / Sleep
                break;
            default:
                break;
        }
    }
}
