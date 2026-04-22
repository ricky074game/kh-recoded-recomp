#pragma once


#include <cstdint>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <string>

#include "hw_ipc.h"
#include "hw_math.h"
#include "hw_dma.h"
#include "hw_timers.h"
#include "hw_irq.h"
#include "hw_gxengine.h"
#include "hw_2d_engine.h"
#include "hw_save.h"
#include "hw_overlay.h"
#include "hw_input.h"
#include "hw_audio.h"

class NDSMemory {
private:
    std::vector<uint8_t> main_ram;     // 4MB   @ 0x02000000
    std::vector<uint8_t> wram;         // 256KB  @ 0x037F8000 (simplified combined WRAM)
    std::vector<uint8_t> dtcm;         // 16KB   @ 0x027E0000 (base configurable)
    std::vector<uint8_t> itcm;         // 32KB   @ 0x01000000
    std::vector<uint8_t> palette_ram;  // 2KB    @ 0x05000000
    std::vector<uint8_t> vram;         // 656KB  @ 0x06000000
    std::vector<uint8_t> oam;          // 2KB    @ 0x07000000

public:
    SPSCQueue<uint32_t, 16> ipc_fifo_arm9_to_arm7;
    SPSCQueue<uint32_t, 16> ipc_fifo_arm7_to_arm9;
    IPCSync ipc_sync_arm9;
    IPCSync ipc_sync_arm7;

    HWMathEngine math_engine;

    DMAChannel dma_arm9[4];
    DMAChannel dma_arm7[4];

    HWTimerManager timers_arm9;
    HWTimerManager timers_arm7;
    HWTimer        timer;  // Legacy compatibility
    HWRTC          rtc;

    HWIRQ irq_arm9;
    HWIRQ irq_arm7;

    GXEngine gx_engine;

    NDS2DEngine engine2d_a;
    NDS2DEngine engine2d_b;
    VRAMController vram_ctrl;

    SaveChip save_chip;

    OverlayManager overlay_manager;

    InputManager input_manager;
    AudioManager audio_manager;

    uint64_t math_div_numer = 0;
    uint32_t math_div_denom = 0;
    uint64_t math_sqrt_val  = 0;

    uint8_t vramcnt[9] = {}; // VRAMCNT_A through VRAMCNT_I

    uint16_t keyinput = 0x03FF; // All buttons released (active-low)
    uint16_t extkeyin = 0x007F; // X, Y, fold, etc.

    uint16_t powcnt1 = 0;

private:
    void CheckAlignment16(uint32_t address) const {
        if (address & 1) {
            throw std::runtime_error(
                "Data Abort: Unaligned 16-bit access at 0x" + ToHex(address));
        }
    }

    void CheckAlignment32(uint32_t address) const {
        if (address & 3) {
            throw std::runtime_error(
                "Data Abort: Unaligned 32-bit access at 0x" + ToHex(address));
        }
    }

    static std::string ToHex(uint32_t val) {
        char buf[12];
        std::snprintf(buf, sizeof(buf), "%08X", val);
        return std::string(buf);
    }

    uint32_t HandleHardwareRead(uint32_t address);
    void     HandleHardwareWrite(uint32_t address, uint32_t value);

    uint8_t* ResolveAddress(uint32_t address, uint32_t& offset, uint32_t& max_size);
    const uint8_t* ResolveAddressConst(uint32_t address, uint32_t& offset, uint32_t& max_size) const;
    bool arm9_vblank_dma_serviced = false;

public:
    NDSMemory();

    uint8_t Read8(uint32_t address) const;
    void    Write8(uint32_t address, uint8_t value);

    uint16_t Read16(uint32_t address) const;
    void     Write16(uint32_t address, uint16_t value);

    uint32_t Read32(uint32_t address) const;
    void     Write32(uint32_t address, uint32_t value);

    uint8_t* GetMainRAM() { return main_ram.data(); }
    size_t   GetMainRAMSize() const { return main_ram.size(); }
    uint8_t* GetVRAM() { return vram.data(); }
    size_t   GetVRAMSize() const { return vram.size(); }
    uint8_t* GetOAM()  { return oam.data(); }
    uint8_t* GetPaletteRAM() { return palette_ram.data(); }
    size_t   GetPaletteRAMSize() const { return palette_ram.size(); }
    GXEngine& GetGXEngine() { return gx_engine; }
    NDS2DEngine& GetEngine2DA() { return engine2d_a; }
    NDS2DEngine& GetEngine2DB() { return engine2d_b; }
    VRAMController& GetVRAMCtrl() { return vram_ctrl; }
    SaveChip& GetSaveChip() { return save_chip; }
    OverlayManager& GetOverlayManager() { return overlay_manager; }
    InputManager& GetInputManager() { return input_manager; }
    AudioManager& GetAudioManager() { return audio_manager; }
    
    void InvalidateOverlayCache();
    void StepHardware(int arm9_cycles);
};
