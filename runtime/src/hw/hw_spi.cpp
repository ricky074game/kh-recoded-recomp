#include "hw_spi.h"

namespace HwSpi {
    uint16_t spicnt = 0;
    uint16_t spidata = 0;

    // Example states for chip selection
    enum class SPIChip { None, Power, Firmware, Touch, RTC };
    SPIChip active_chip = SPIChip::None;

    void Init() {
        spicnt = 0;
        spidata = 0;
        active_chip = SPIChip::None;
    }

    uint16_t ReadRegister(uint32_t address) {
        if (address == 0x040001C0) return spicnt;
        if (address == 0x040001C2) return spidata;
        return 0;
    }

    void WriteRegister(uint32_t address, uint16_t value) {
        if (address == 0x040001C0) {
            spicnt = value;
            // Chip select logic
            int device = (spicnt >> 8) & 3;
            bool chip_select_hold = (spicnt >> 11) & 1;

            if (chip_select_hold) {
                switch(device) {
                    case 0: active_chip = SPIChip::Power; break;
                    case 1: active_chip = SPIChip::Firmware; break;
                    case 2: active_chip = SPIChip::Touch; break;
                    case 3: active_chip = SPIChip::None; break; // Slot-2 / reserved
                }
            } else {
                active_chip = SPIChip::None;
            }
        }
        else if (address == 0x040001C2) {
            // Send byte to active chip and get response
            uint8_t to_send = value & 0xFF;
            uint8_t response = 0;

            if (active_chip == SPIChip::Power) {
                // Power management response
                response = 0x00;
            } else if (active_chip == SPIChip::Firmware) {
                // Firmware response
                response = 0x00;
            } else if (active_chip == SPIChip::Touch) {
                // Touchscreen response (simulated ADC)
                response = 0x00;
            }

            spidata = response; // Emulate 8-bit response mapping
        }
    }
}
