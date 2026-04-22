#include "memory_map.h"
#include <atomic>
#include <cstdlib>

extern void RegisterAllLiftedFunctions(NDSMemory* mem);
extern void RegisterITCMHelperFunctions(NDSMemory* mem);
extern void RegisterMainRAMHelperFunctions(NDSMemory* mem);
extern std::atomic<uint32_t> g_debug_arm9_last_dispatch_addr;


uint8_t* g_memory_base = nullptr;
uint32_t g_memory_size = 0;

namespace {
constexpr uint32_t kITCMMirrorMask = 0x7FFF; // 32KB mirrored window
constexpr uint32_t kITCMMirrorEndExclusive = 0x02000000;
constexpr uint32_t kMainRAMStart = 0x02000000;
constexpr uint32_t kMainRAMMirrorEndExclusive = 0x03000000;
constexpr uint32_t kMainRAMMirrorMask = 0x003FFFFF; // 4MB mirrored window
constexpr uint32_t kWRAMMirrorStart = 0x03000000;
constexpr uint32_t kWRAMMirrorEndExclusive = 0x04000000;
constexpr uint32_t kWRAMBase = 0x037F8000;
constexpr uint32_t kWRAMMirrorMask = 0x0003FFFF; // 256KB mirrored window
constexpr uint32_t kVRAMMirrorStart = 0x06000000;
constexpr uint32_t kVRAMMirrorEndExclusive = 0x07000000;
constexpr uint32_t kTransientOpenBusStart = 0x07000800;

bool IsHWProbeDebugEnabled() {
    static const bool enabled = []() {
        const char* env = std::getenv("KH_DEBUG_HW_PROBES");
        if (env == nullptr) return false;
        return std::strcmp(env, "0") != 0;
    }();
    return enabled;
}
}

NDSMemory::NDSMemory() {
    main_ram.resize(4 * 1024 * 1024, 0);    // 4MB Main RAM
    wram.resize(256 * 1024, 0);              // 256KB WRAM
    dtcm.resize(16 * 1024, 0);              // 16KB DTCM
    itcm.resize(32 * 1024, 0);              // 32KB ITCM
    palette_ram.resize(2 * 1024, 0);        // 2KB Palette RAM
    vram.resize(656 * 1024, 0);             // 656KB VRAM
    oam.resize(2 * 1024, 0);               // 2KB OAM

    engine2d_b.SetSubEngine(true);

    InitIPC(&irq_arm9, &irq_arm7);

    RegisterAllLiftedFunctions(this);
    RegisterITCMHelperFunctions(this);
    RegisterMainRAMHelperFunctions(this);
}

void NDSMemory::InvalidateOverlayCache() {
    overlay_manager.InvalidateOverlayCache(main_ram);
}

uint32_t NDSMemory::HandleHardwareRead(uint32_t address) {

    if (address >= 0x04000000 && address <= 0x0400006F && address != 0x04000060) {
        return engine2d_a.ReadRegister(address - 0x04000000);
    }
    if (address >= 0x04001000 && address <= 0x0400106F) {
        return engine2d_b.ReadRegister(address - 0x04001000);
    }

    if (address == 0x04000180) return ipc_sync_arm9.Read();

    if (address == 0x040001A0) return save_chip.ReadSPICNT();
    if (address == 0x040001A2) return save_chip.ReadSPIData();

    if (address == 0x04000188) {
        uint32_t val = 0;
        ipc_fifo_arm7_to_arm9.pop(val);
        return val;
    }

    if (address >= 0x04000100 && address <= 0x0400010F) {
        uint32_t offset = address - 0x04000100;
        return timers_arm9.ReadTimer(offset);
    }

    if (address == 0x04000208) return irq_arm9.ime;
    if (address == 0x04000210) return irq_arm9.ie;
    if (address == 0x04000214) return irq_arm9.if_reg;
    
    if (address == 0x04000130) return input_manager.ReadKEYINPUT();
    if (address == 0x04000136) return input_manager.ReadEXTKEYIN();

    if (address >= 0x04000240 && address <= 0x04000248) {
        return vramcnt[address - 0x04000240];
    }

    if (address == 0x04000304) return powcnt1;

    if (address == 0x04000280) return math_engine.ReadDIVCNT();

    if (address == 0x04000290) return static_cast<uint32_t>(math_engine.div_numer & 0xFFFFFFFF);
    if (address == 0x04000294) return static_cast<uint32_t>((math_engine.div_numer >> 32) & 0xFFFFFFFF);

    if (address == 0x04000298) return static_cast<uint32_t>(math_engine.div_denom & 0xFFFFFFFF);
    if (address == 0x0400029C) return static_cast<uint32_t>((math_engine.div_denom >> 32) & 0xFFFFFFFF);

    if (address == 0x040002A0) return static_cast<uint32_t>(math_engine.div_result & 0xFFFFFFFF);
    if (address == 0x040002A4) return static_cast<uint32_t>((math_engine.div_result >> 32) & 0xFFFFFFFF);

    if (address == 0x040002A8) return static_cast<uint32_t>(math_engine.div_remainder & 0xFFFFFFFF);
    if (address == 0x040002AC) return static_cast<uint32_t>((math_engine.div_remainder >> 32) & 0xFFFFFFFF);

    if (address == 0x040002B0) return math_engine.ReadSQRTCNT();

    if (address == 0x040002B4) return math_engine.sqrt_result;

    if (address == 0x040002B8) return static_cast<uint32_t>(math_engine.sqrt_param & 0xFFFFFFFF);
    if (address == 0x040002BC) return static_cast<uint32_t>((math_engine.sqrt_param >> 32) & 0xFFFFFFFF);

    if (address >= 0x040000B0 && address <= 0x040000DF) {
        int channel = (address - 0x040000B0) / 12;
        int reg     = (address - 0x040000B0) % 12;
        if (channel < 4) {
            if (reg == 0) return dma_arm9[channel].src_addr;
            if (reg == 4) return dma_arm9[channel].dst_addr;
            if (reg == 8) return dma_arm9[channel].enabled ? 0x80000000 : 0;
        }
    }

    if (address == 0x04000060) return gx_engine.ReadRegister(address);
    if (address >= 0x04000600 && address <= 0x04000604) return gx_engine.ReadRegister(address);
    if (address >= 0x04000350 && address <= 0x04000354) return gx_engine.ReadRegister(address);
    if (address >= 0x04000640 && address <= 0x0400067F) return gx_engine.ReadRegister(address);
    if (address >= 0x04000680 && address <= 0x040006A3) return gx_engine.ReadRegister(address);

    return 0; // Unhandled read returns 0
}

void NDSMemory::HandleHardwareWrite(uint32_t address, uint32_t value) {
    const bool debug_hw_probes = IsHWProbeDebugEnabled();

    static bool seen_dispcnt_a_write = false;
    static bool seen_dispcnt_b_write = false;
    if (debug_hw_probes && address == 0x04000000 && !seen_dispcnt_a_write) {
        seen_dispcnt_a_write = true;
        std::fprintf(stderr,
                     "NDSMemory: first DISPCNT-A write addr=0x%08X value=0x%08X lastDispatch=0x%08X\n",
                     address,
                     value,
                     g_debug_arm9_last_dispatch_addr.load(std::memory_order_relaxed));
    }
    if (debug_hw_probes && address == 0x04001000 && !seen_dispcnt_b_write) {
        seen_dispcnt_b_write = true;
        std::fprintf(stderr,
                     "NDSMemory: first DISPCNT-B write addr=0x%08X value=0x%08X lastDispatch=0x%08X\n",
                     address,
                     value,
                     g_debug_arm9_last_dispatch_addr.load(std::memory_order_relaxed));
    }

    if (debug_hw_probes && address >= 0x04000000 && address <= 0x0400106F) {
        static bool seen_hwio_write[0x1070] = {};
        const uint32_t idx = address - 0x04000000;
        if (idx < 0x1070 && !seen_hwio_write[idx]) {
            seen_hwio_write[idx] = true;
            std::fprintf(stderr,
                         "NDSMemory: first IO write addr=0x%08X value=0x%08X lastDispatch=0x%08X\n",
                         address,
                         value,
                         g_debug_arm9_last_dispatch_addr.load(std::memory_order_relaxed));
        }
    }

    if (address >= 0x04000000 && address <= 0x0400006F && address != 0x04000060) {
        engine2d_a.WriteRegister(address - 0x04000000, value); return;
    }
    if (address >= 0x04001000 && address <= 0x0400106F) {
        engine2d_b.WriteRegister(address - 0x04001000, value); return;
    }

    if (address == 0x04000180) {
        bool send_irq = ipc_sync_arm9.Write(value, ipc_sync_arm7);
        if (send_irq && ipc_sync_arm7.irq_enable) {
            irq_arm7.RaiseIRQ(IRQBits::IPCSync);
        }
        return;
    }

    if (address == 0x040001A0) { save_chip.WriteSPICNT(static_cast<uint16_t>(value)); return; }
    if (address == 0x040001A2) { save_chip.WriteSPIData(static_cast<uint8_t>(value)); return; }

    if (address == 0x04000188) {
        ipc_fifo_arm9_to_arm7.push(value);
        return;
    }

    if (address >= 0x04000100 && address <= 0x0400010F) {
        uint32_t offset = address - 0x04000100;
        timers_arm9.WriteTimer(offset, static_cast<uint16_t>(value));
        return;
    }

    if (address >= 0x040000B0 && address <= 0x040000DF) {
        int channel = (address - 0x040000B0) / 12;
        int reg     = (address - 0x040000B0) % 12;
        if (channel < 4) {
            if (reg == 0) {
                dma_arm9[channel].src_addr = value;
            } else if (reg == 4) {
                dma_arm9[channel].dst_addr = value;
            } else if (reg == 8) {
                dma_arm9[channel].WriteControl(value);
                if (dma_arm9[channel].enabled &&
                    dma_arm9[channel].timing == DMAStartTiming::Immediate) {
                    dma_arm9[channel].Execute(this);
                    if (dma_arm9[channel].irq_on_end) {
                        irq_arm9.RaiseIRQ(IRQBits::DMA0 << channel);
                    }
                }
            }
        }
        return;
    }

    if (address >= 0x040000D0 && address <= 0x040000FF) {
        int channel = (address - 0x040000D0) / 12;
        int reg     = (address - 0x040000D0) % 12;
        if (channel < 4) {
            if (reg == 0) {
                dma_arm7[channel].src_addr = value;
            } else if (reg == 4) {
                dma_arm7[channel].dst_addr = value;
            } else if (reg == 8) {
                dma_arm7[channel].WriteControl(value);
                if (dma_arm7[channel].enabled &&
                    dma_arm7[channel].timing == DMAStartTiming::Immediate) {
                    dma_arm7[channel].Execute(this);
                }
            }
        }
        return;
    }

    if (address == 0x04000280) {
        math_engine.WriteDIVCNT(value);
        return;
    }

    if (address == 0x04000290) {
        math_engine.div_numer = (math_engine.div_numer & 0xFFFFFFFF00000000LL) |
                                 (value & 0xFFFFFFFF);
        math_div_numer = static_cast<uint64_t>(math_engine.div_numer);
        math_engine.ComputeDivision();
        return;
    }
    if (address == 0x04000294) {
        math_engine.div_numer = (math_engine.div_numer & 0x00000000FFFFFFFFLL) |
                                 (static_cast<int64_t>(value) << 32);
        math_div_numer = static_cast<uint64_t>(math_engine.div_numer);
        math_engine.ComputeDivision();
        return;
    }

    if (address == 0x04000298) {
        math_engine.div_denom = (math_engine.div_denom & 0xFFFFFFFF00000000LL) |
                                 (value & 0xFFFFFFFF);
        math_div_denom = static_cast<uint32_t>(value);
        math_engine.ComputeDivision();
        return;
    }
    if (address == 0x0400029C) {
        math_engine.div_denom = (math_engine.div_denom & 0x00000000FFFFFFFFLL) |
                                 (static_cast<int64_t>(value) << 32);
        math_engine.ComputeDivision();
        return;
    }

    if (address == 0x040002B0) {
        math_engine.WriteSQRTCNT(value);
        return;
    }

    if (address == 0x040002B8) {
        math_engine.sqrt_param = (math_engine.sqrt_param & 0xFFFFFFFF00000000ULL) | value;
        math_sqrt_val = math_engine.sqrt_param;
        math_engine.ComputeSqrt();
        return;
    }
    if (address == 0x040002BC) {
        math_engine.sqrt_param = (math_engine.sqrt_param & 0x00000000FFFFFFFFULL) |
                                  (static_cast<uint64_t>(value) << 32);
        math_sqrt_val = math_engine.sqrt_param;
        math_engine.ComputeSqrt();
        return;
    }

    if (address == 0x04000208) { irq_arm9.ime = value; return; }
    if (address == 0x04000210) { irq_arm9.ie  = value; return; }
    if (address == 0x04000214) { irq_arm9.AcknowledgeIRQ(value); return; }

    if (address >= 0x04000240 && address <= 0x04000248) {
        int idx = address - 0x04000240;
        vramcnt[idx] = static_cast<uint8_t>(value);
        vram_ctrl.WriteControl(idx, static_cast<uint8_t>(value));
        return;
    }

    if (address == 0x04000304) { powcnt1 = static_cast<uint16_t>(value); return; }

    if (address == 0x04000060) { gx_engine.WriteCommandPort(address, value); return; }
    if (address >= 0x04000350 && address <= 0x04000354) { gx_engine.WriteCommandPort(address, value); return; }
    if (address == 0x04000400) { gx_engine.WriteGXFIFO(value); return; }
    if (address >= 0x04000440 && address <= 0x040005CC) { gx_engine.WriteCommandPort(address, value); return; }

}

void NDSMemory::StepHardware(int arm9_cycles) {
    if (arm9_cycles <= 0) {
        return;
    }

    TimerCore timer_core;
    timer_core.tmr = &timers_arm9;
    timer_core.irq = &irq_arm9;
    timer_core.StepTimers(arm9_cycles);

    const bool vblank_pending = (irq_arm9.if_reg & IRQBits::VBlank) != 0;
    if (!vblank_pending) {
        arm9_vblank_dma_serviced = false;
        return;
    }

    if (arm9_vblank_dma_serviced) {
        return;
    }

    for (int channel = 0; channel < 4; ++channel) {
        auto& dma = dma_arm9[channel];
        if (!dma.enabled || dma.timing != DMAStartTiming::VBlank) {
            continue;
        }

        dma.Execute(this);
        if (dma.irq_on_end) {
            irq_arm9.RaiseIRQ(IRQBits::DMA0 << channel);
        }
    }
    arm9_vblank_dma_serviced = true;
}

uint8_t NDSMemory::Read8(uint32_t address) const {
    if (address < kITCMMirrorEndExclusive) {
        return itcm[address & kITCMMirrorMask];
    }

    if (address >= kMainRAMStart && address < kMainRAMMirrorEndExclusive) {
        return main_ram[(address - kMainRAMStart) & kMainRAMMirrorMask];
    }
    if (address >= 0x027E0000 && address <= 0x027E3FFF) {
        return dtcm[address - 0x027E0000];
    }
    if (address >= kWRAMMirrorStart && address < kWRAMMirrorEndExclusive) {
        return wram[(address - kWRAMBase) & kWRAMMirrorMask];
    }
    if (address >= 0x01000000 && address <= 0x01007FFF) {
        return itcm[address - 0x01000000];
    }
    if (address >= 0x05000000 && address < 0x06000000) {
        const uint32_t palette_size = static_cast<uint32_t>(palette_ram.size());
        const uint32_t off = (address - 0x05000000) % palette_size;
        return palette_ram[off];
    }
    if (address >= kVRAMMirrorStart && address < kVRAMMirrorEndExclusive) {
        const uint32_t off = (address - kVRAMMirrorStart) % static_cast<uint32_t>(vram.size());
        return vram[off];
    }
    if (address >= 0x07000000 && address <= 0x070007FF) {
        return oam[address - 0x07000000];
    }
    if (address >= 0x04000000 && address <= 0x04FFFFFF) {
        return const_cast<NDSMemory*>(this)->HandleHardwareRead(address) & 0xFF;
    }

    if (address >= kTransientOpenBusStart) {
        return 0;
    }

    throw std::runtime_error("Segfault: Unmapped 8-bit read at 0x" + ToHex(address));
}

void NDSMemory::Write8(uint32_t address, uint8_t value) {
    if (address < kITCMMirrorEndExclusive) {
        itcm[address & kITCMMirrorMask] = value;
        return;
    }

    if (address >= kMainRAMStart && address < kMainRAMMirrorEndExclusive) {
        main_ram[(address - kMainRAMStart) & kMainRAMMirrorMask] = value;
        return;
    }
    if (address >= 0x027E0000 && address <= 0x027E3FFF) {
        dtcm[address - 0x027E0000] = value; return;
    }
    if (address >= kWRAMMirrorStart && address < kWRAMMirrorEndExclusive) {
        wram[(address - kWRAMBase) & kWRAMMirrorMask] = value; return;
    }
    if (address >= 0x01000000 && address <= 0x01007FFF) {
        itcm[address - 0x01000000] = value; return;
    }
    if (address >= 0x05000000 && address < 0x06000000) {
        const uint32_t palette_size = static_cast<uint32_t>(palette_ram.size());
        const uint32_t off = (address - 0x05000000) % palette_size;
        palette_ram[off] = value; return;
    }
    if (address >= kVRAMMirrorStart && address < kVRAMMirrorEndExclusive) {
        const uint32_t off = (address - kVRAMMirrorStart) % static_cast<uint32_t>(vram.size());
        vram[off] = value; return;
    }
    if (address >= 0x07000000 && address <= 0x070007FF) {
        oam[address - 0x07000000] = value; return;
    }
    if (address >= 0x04000000 && address <= 0x04FFFFFF) {
        HandleHardwareWrite(address, value); return;
    }

    if (address >= kTransientOpenBusStart) {
        return;
    }

    throw std::runtime_error("Segfault: Unmapped 8-bit write at 0x" + ToHex(address));
}

uint16_t NDSMemory::Read16(uint32_t address) const {
    if (address < kITCMMirrorEndExclusive) {
        uint32_t off0 = address & kITCMMirrorMask;
        uint32_t off1 = (address + 1) & kITCMMirrorMask;
        return static_cast<uint16_t>(itcm[off0]) |
               (static_cast<uint16_t>(itcm[off1]) << 8);
    }

    if (address >= kMainRAMStart && address < kMainRAMMirrorEndExclusive) {
        uint32_t off0 = (address - kMainRAMStart) & kMainRAMMirrorMask;
        uint32_t off1 = ((address + 1) - kMainRAMStart) & kMainRAMMirrorMask;
        return static_cast<uint16_t>(main_ram[off0]) |
               (static_cast<uint16_t>(main_ram[off1]) << 8);
    }
    if (address >= 0x027E0000 && address <= 0x027E3FFE) {
        uint32_t off = address - 0x027E0000;
        return static_cast<uint16_t>(dtcm[off]) | (static_cast<uint16_t>(dtcm[off + 1]) << 8);
    }
    if (address >= kWRAMMirrorStart && address < kWRAMMirrorEndExclusive) {
        uint32_t off0 = (address - kWRAMBase) & kWRAMMirrorMask;
        uint32_t off1 = ((address + 1) - kWRAMBase) & kWRAMMirrorMask;
        return static_cast<uint16_t>(wram[off0]) | (static_cast<uint16_t>(wram[off1]) << 8);
    }
    if (address >= 0x01000000 && address <= 0x01007FFE) {
        uint32_t off = address - 0x01000000;
        return static_cast<uint16_t>(itcm[off]) | (static_cast<uint16_t>(itcm[off + 1]) << 8);
    }
    if (address >= 0x05000000 && address < 0x06000000) {
        const uint32_t palette_size = static_cast<uint32_t>(palette_ram.size());
        const uint32_t base = address - 0x05000000;
        const uint32_t off0 = base % palette_size;
        const uint32_t off1 = (base + 1u) % palette_size;
        return static_cast<uint16_t>(palette_ram[off0]) | (static_cast<uint16_t>(palette_ram[off1]) << 8);
    }
    if (address >= kVRAMMirrorStart && address < kVRAMMirrorEndExclusive) {
        const uint32_t base = address - kVRAMMirrorStart;
        const uint32_t vram_size = static_cast<uint32_t>(vram.size());
        const uint32_t off0 = base % vram_size;
        const uint32_t off1 = (base + 1u) % vram_size;
        return static_cast<uint16_t>(vram[off0]) | (static_cast<uint16_t>(vram[off1]) << 8);
    }
    if (address >= 0x07000000 && address <= 0x070007FE) {
        uint32_t off = address - 0x07000000;
        return static_cast<uint16_t>(oam[off]) | (static_cast<uint16_t>(oam[off + 1]) << 8);
    }
    if (address >= 0x04000000 && address <= 0x04FFFFFF) {
        return const_cast<NDSMemory*>(this)->HandleHardwareRead(address) & 0xFFFF;
    }

    if (address >= kTransientOpenBusStart) {
        return 0;
    }

    throw std::runtime_error("Segfault: Unmapped 16-bit read at 0x" + ToHex(address));
}

void NDSMemory::Write16(uint32_t address, uint16_t value) {
    if (address < kITCMMirrorEndExclusive) {
        uint32_t off0 = address & kITCMMirrorMask;
        uint32_t off1 = (address + 1) & kITCMMirrorMask;
        itcm[off0] = static_cast<uint8_t>(value & 0xFF);
        itcm[off1] = static_cast<uint8_t>((value >> 8) & 0xFF);
        return;
    }

    if (address >= kMainRAMStart && address < kMainRAMMirrorEndExclusive) {
        uint32_t off0 = (address - kMainRAMStart) & kMainRAMMirrorMask;
        uint32_t off1 = ((address + 1) - kMainRAMStart) & kMainRAMMirrorMask;
        main_ram[off0] = static_cast<uint8_t>(value & 0xFF);
        main_ram[off1] = static_cast<uint8_t>((value >> 8) & 0xFF);
        return;
    }
    if (address >= 0x027E0000 && address <= 0x027E3FFE) {
        uint32_t off = address - 0x027E0000;
        dtcm[off] = value & 0xFF; dtcm[off + 1] = (value >> 8) & 0xFF; return;
    }
    if (address >= kWRAMMirrorStart && address < kWRAMMirrorEndExclusive) {
        uint32_t off0 = (address - kWRAMBase) & kWRAMMirrorMask;
        uint32_t off1 = ((address + 1) - kWRAMBase) & kWRAMMirrorMask;
        wram[off0] = value & 0xFF; wram[off1] = (value >> 8) & 0xFF; return;
    }
    if (address >= 0x01000000 && address <= 0x01007FFE) {
        uint32_t off = address - 0x01000000;
        itcm[off] = value & 0xFF; itcm[off + 1] = (value >> 8) & 0xFF; return;
    }
    if (address >= 0x05000000 && address < 0x06000000) {
        const uint32_t palette_size = static_cast<uint32_t>(palette_ram.size());
        const uint32_t base = address - 0x05000000;
        const uint32_t off0 = base % palette_size;
        const uint32_t off1 = (base + 1u) % palette_size;
        palette_ram[off0] = value & 0xFF;
        palette_ram[off1] = (value >> 8) & 0xFF;
        return;
    }
    if (address >= kVRAMMirrorStart && address < kVRAMMirrorEndExclusive) {
        const uint32_t base = address - kVRAMMirrorStart;
        const uint32_t vram_size = static_cast<uint32_t>(vram.size());
        const uint32_t off0 = base % vram_size;
        const uint32_t off1 = (base + 1u) % vram_size;
        vram[off0] = value & 0xFF;
        vram[off1] = (value >> 8) & 0xFF;
        return;
    }
    if (address >= 0x07000000 && address <= 0x070007FE) {
        uint32_t off = address - 0x07000000;
        oam[off] = value & 0xFF; oam[off + 1] = (value >> 8) & 0xFF; return;
    }
    if (address >= 0x04000000 && address <= 0x04FFFFFF) {
        HandleHardwareWrite(address, value); return;
    }

    if (address >= kTransientOpenBusStart) {
        return;
    }

    throw std::runtime_error("Segfault: Unmapped 16-bit write at 0x" + ToHex(address));
}

uint32_t NDSMemory::Read32(uint32_t address) const {
    if (address < kITCMMirrorEndExclusive) {
        uint32_t off0 = address & kITCMMirrorMask;
        uint32_t off1 = (address + 1) & kITCMMirrorMask;
        uint32_t off2 = (address + 2) & kITCMMirrorMask;
        uint32_t off3 = (address + 3) & kITCMMirrorMask;
        return static_cast<uint32_t>(itcm[off0]) |
               (static_cast<uint32_t>(itcm[off1]) << 8) |
               (static_cast<uint32_t>(itcm[off2]) << 16) |
               (static_cast<uint32_t>(itcm[off3]) << 24);
    }

    if (address >= kMainRAMStart && address < kMainRAMMirrorEndExclusive) {
        uint32_t off0 = (address - kMainRAMStart) & kMainRAMMirrorMask;
        uint32_t off1 = ((address + 1) - kMainRAMStart) & kMainRAMMirrorMask;
        uint32_t off2 = ((address + 2) - kMainRAMStart) & kMainRAMMirrorMask;
        uint32_t off3 = ((address + 3) - kMainRAMStart) & kMainRAMMirrorMask;
        return static_cast<uint32_t>(main_ram[off0]) |
               (static_cast<uint32_t>(main_ram[off1]) << 8) |
               (static_cast<uint32_t>(main_ram[off2]) << 16) |
               (static_cast<uint32_t>(main_ram[off3]) << 24);
    }
    if (address >= 0x027E0000 && address <= 0x027E3FFC) {
        uint32_t off = address - 0x027E0000;
        return static_cast<uint32_t>(dtcm[off]) |
               (static_cast<uint32_t>(dtcm[off + 1]) << 8) |
               (static_cast<uint32_t>(dtcm[off + 2]) << 16) |
               (static_cast<uint32_t>(dtcm[off + 3]) << 24);
    }
    if (address >= kWRAMMirrorStart && address < kWRAMMirrorEndExclusive) {
        uint32_t off0 = (address - kWRAMBase) & kWRAMMirrorMask;
        uint32_t off1 = ((address + 1) - kWRAMBase) & kWRAMMirrorMask;
        uint32_t off2 = ((address + 2) - kWRAMBase) & kWRAMMirrorMask;
        uint32_t off3 = ((address + 3) - kWRAMBase) & kWRAMMirrorMask;
        return static_cast<uint32_t>(wram[off0]) |
               (static_cast<uint32_t>(wram[off1]) << 8) |
               (static_cast<uint32_t>(wram[off2]) << 16) |
               (static_cast<uint32_t>(wram[off3]) << 24);
    }
    if (address >= 0x01000000 && address <= 0x01007FFC) {
        uint32_t off = address - 0x01000000;
        return static_cast<uint32_t>(itcm[off]) |
               (static_cast<uint32_t>(itcm[off + 1]) << 8) |
               (static_cast<uint32_t>(itcm[off + 2]) << 16) |
               (static_cast<uint32_t>(itcm[off + 3]) << 24);
    }
    if (address >= 0x05000000 && address < 0x06000000) {
        const uint32_t palette_size = static_cast<uint32_t>(palette_ram.size());
        const uint32_t base = address - 0x05000000;
        const uint32_t off0 = base % palette_size;
        const uint32_t off1 = (base + 1u) % palette_size;
        const uint32_t off2 = (base + 2u) % palette_size;
        const uint32_t off3 = (base + 3u) % palette_size;
        return static_cast<uint32_t>(palette_ram[off0]) |
               (static_cast<uint32_t>(palette_ram[off1]) << 8) |
               (static_cast<uint32_t>(palette_ram[off2]) << 16) |
               (static_cast<uint32_t>(palette_ram[off3]) << 24);
    }
    if (address >= kVRAMMirrorStart && address < kVRAMMirrorEndExclusive) {
        const uint32_t base = address - kVRAMMirrorStart;
        const uint32_t vram_size = static_cast<uint32_t>(vram.size());
        const uint32_t off0 = base % vram_size;
        const uint32_t off1 = (base + 1u) % vram_size;
        const uint32_t off2 = (base + 2u) % vram_size;
        const uint32_t off3 = (base + 3u) % vram_size;
        return static_cast<uint32_t>(vram[off0]) |
               (static_cast<uint32_t>(vram[off1]) << 8) |
               (static_cast<uint32_t>(vram[off2]) << 16) |
               (static_cast<uint32_t>(vram[off3]) << 24);
    }
    if (address >= 0x07000000 && address <= 0x070007FC) {
        uint32_t off = address - 0x07000000;
        return static_cast<uint32_t>(oam[off]) |
               (static_cast<uint32_t>(oam[off + 1]) << 8) |
               (static_cast<uint32_t>(oam[off + 2]) << 16) |
               (static_cast<uint32_t>(oam[off + 3]) << 24);
    }
    if (address >= 0x04000000 && address <= 0x04FFFFFF) {
        return const_cast<NDSMemory*>(this)->HandleHardwareRead(address);
    }

    if (address >= kTransientOpenBusStart) {
        return 0;
    }

    throw std::runtime_error("Segfault: Unmapped 32-bit read at 0x" + ToHex(address));
}

void NDSMemory::Write32(uint32_t address, uint32_t value) {
    if (address < kITCMMirrorEndExclusive) {
        uint32_t off0 = address & kITCMMirrorMask;
        uint32_t off1 = (address + 1) & kITCMMirrorMask;
        uint32_t off2 = (address + 2) & kITCMMirrorMask;
        uint32_t off3 = (address + 3) & kITCMMirrorMask;
        itcm[off0] = static_cast<uint8_t>(value & 0xFF);
        itcm[off1] = static_cast<uint8_t>((value >> 8) & 0xFF);
        itcm[off2] = static_cast<uint8_t>((value >> 16) & 0xFF);
        itcm[off3] = static_cast<uint8_t>((value >> 24) & 0xFF);
        return;
    }

    if (address >= kMainRAMStart && address < kMainRAMMirrorEndExclusive) {
        uint32_t off0 = (address - kMainRAMStart) & kMainRAMMirrorMask;
        uint32_t off1 = ((address + 1) - kMainRAMStart) & kMainRAMMirrorMask;
        uint32_t off2 = ((address + 2) - kMainRAMStart) & kMainRAMMirrorMask;
        uint32_t off3 = ((address + 3) - kMainRAMStart) & kMainRAMMirrorMask;
        main_ram[off0] = static_cast<uint8_t>(value & 0xFF);
        main_ram[off1] = static_cast<uint8_t>((value >> 8) & 0xFF);
        main_ram[off2] = static_cast<uint8_t>((value >> 16) & 0xFF);
        main_ram[off3] = static_cast<uint8_t>((value >> 24) & 0xFF);
        return;
    }
    if (address >= 0x027E0000 && address <= 0x027E3FFC) {
        uint32_t off = address - 0x027E0000;
        dtcm[off] = value & 0xFF; dtcm[off+1] = (value>>8)&0xFF;
        dtcm[off+2] = (value>>16)&0xFF; dtcm[off+3] = (value>>24)&0xFF;
        return;
    }
    if (address >= kWRAMMirrorStart && address < kWRAMMirrorEndExclusive) {
        uint32_t off0 = (address - kWRAMBase) & kWRAMMirrorMask;
        uint32_t off1 = ((address + 1) - kWRAMBase) & kWRAMMirrorMask;
        uint32_t off2 = ((address + 2) - kWRAMBase) & kWRAMMirrorMask;
        uint32_t off3 = ((address + 3) - kWRAMBase) & kWRAMMirrorMask;
        wram[off0] = value & 0xFF; wram[off1] = (value>>8)&0xFF;
        wram[off2] = (value>>16)&0xFF; wram[off3] = (value>>24)&0xFF;
        return;
    }
    if (address >= 0x01000000 && address <= 0x01007FFC) {
        uint32_t off = address - 0x01000000;
        itcm[off] = value & 0xFF; itcm[off+1] = (value>>8)&0xFF;
        itcm[off+2] = (value>>16)&0xFF; itcm[off+3] = (value>>24)&0xFF;
        return;
    }
    if (address >= 0x05000000 && address < 0x06000000) {
        const uint32_t palette_size = static_cast<uint32_t>(palette_ram.size());
        const uint32_t base = address - 0x05000000;
        const uint32_t off0 = base % palette_size;
        const uint32_t off1 = (base + 1u) % palette_size;
        const uint32_t off2 = (base + 2u) % palette_size;
        const uint32_t off3 = (base + 3u) % palette_size;
        palette_ram[off0] = value & 0xFF;
        palette_ram[off1] = (value>>8)&0xFF;
        palette_ram[off2] = (value>>16)&0xFF;
        palette_ram[off3] = (value>>24)&0xFF;
        return;
    }
    if (address >= kVRAMMirrorStart && address < kVRAMMirrorEndExclusive) {
        const uint32_t base = address - kVRAMMirrorStart;
        const uint32_t vram_size = static_cast<uint32_t>(vram.size());
        const uint32_t off0 = base % vram_size;
        const uint32_t off1 = (base + 1u) % vram_size;
        const uint32_t off2 = (base + 2u) % vram_size;
        const uint32_t off3 = (base + 3u) % vram_size;
        vram[off0] = value & 0xFF;
        vram[off1] = (value>>8)&0xFF;
        vram[off2] = (value>>16)&0xFF;
        vram[off3] = (value>>24)&0xFF;
        return;
    }
    if (address >= 0x07000000 && address <= 0x070007FC) {
        uint32_t off = address - 0x07000000;
        oam[off] = value & 0xFF; oam[off+1] = (value>>8)&0xFF;
        oam[off+2] = (value>>16)&0xFF; oam[off+3] = (value>>24)&0xFF;
        return;
    }
    if (address >= 0x04000000 && address <= 0x04FFFFFF) {
        HandleHardwareWrite(address, value);
        return;
    }

    if (address >= kTransientOpenBusStart) {
        return;
    }

    if (address >= 0x1FFF0000 && address < 0x20000000) {
        return;
    }

    throw std::runtime_error("Segfault: Unmapped 32-bit write at 0x" + ToHex(address));
}

thread_local uint32_t g_deb_last_addr = 0;
thread_local uint32_t g_deb_branch_target = 0;
thread_local uint32_t g_deb_loop_count = 0;
std::atomic<bool> g_superdebug_enabled{false};
