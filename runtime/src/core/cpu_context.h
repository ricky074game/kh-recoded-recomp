#pragma once


#include <cstdint>
#include <cstring>
#include <atomic>

class NDSMemory; // Forward declaration

namespace ARMMode {
    constexpr uint32_t USR = 0x10; // User
    constexpr uint32_t FIQ = 0x11; // Fast Interrupt
    constexpr uint32_t IRQ = 0x12; // Normal Interrupt
    constexpr uint32_t SVC = 0x13; // Supervisor (SWI)
    constexpr uint32_t ABT = 0x17; // Abort (prefetch/data)
    constexpr uint32_t UND = 0x1B; // Undefined instruction
    constexpr uint32_t SYS = 0x1F; // System (privileged User)
}

namespace CPSRFlags {
    constexpr uint32_t N = (1u << 31); // Negative
    constexpr uint32_t Z = (1u << 30); // Zero
    constexpr uint32_t C = (1u << 29); // Carry
    constexpr uint32_t V = (1u << 28); // Overflow
    constexpr uint32_t I = (1u << 7);  // IRQ disable
    constexpr uint32_t F = (1u << 6);  // FIQ disable
    constexpr uint32_t T = (1u << 5);  // Thumb state

    constexpr uint32_t MODE_MASK = 0x1F;
}

struct CP15State {
    uint32_t control   = 0; // c1: Control register (cache enable, TCM enable, etc.)
    uint32_t dtcm_base = 0; // c9,c1,0: DTCM Region register (base | size)
    uint32_t itcm_base = 0; // c9,c1,1: ITCM Region register (base | size)
    uint32_t cache_cmd = 0; // c7: Cache operation scratch (used for flush detection)
};

struct CPU_Context {
    uint32_t r[16] = {};
    uint32_t cpsr   = 0;

    uint32_t r13_irq = 0, r14_irq = 0;
    uint32_t r13_svc = 0, r14_svc = 0;
    uint32_t r8_fiq  = 0, r9_fiq  = 0, r10_fiq = 0, r11_fiq = 0, r12_fiq = 0;
    uint32_t r13_fiq = 0, r14_fiq = 0;
    uint32_t r13_abt = 0, r14_abt = 0;
    uint32_t r13_und = 0, r14_und = 0;

    uint32_t spsr_irq = 0;
    uint32_t spsr_fiq = 0;
    uint32_t spsr_svc = 0;
    uint32_t spsr_abt = 0;
    uint32_t spsr_und = 0;

    CP15State cp15;

    uint32_t& cp15_control   = cp15.control;
    uint32_t& cp15_dtcm_base = cp15.dtcm_base;
    uint32_t& cp15_itcm_base = cp15.itcm_base;

    NDSMemory* mem = nullptr;

    uint32_t trace_buffer[256] = {};
    uint8_t  trace_idx = 0;
    uint32_t loop_addr = 0;
    uint32_t loop_count = 0;

    uint32_t dispatch_pc = 0;

    const std::atomic<bool>* running_flag = nullptr;


    uint32_t GetMode() const { return cpsr & CPSRFlags::MODE_MASK; }

    uint32_t GetSPSR() const {
        switch (GetMode()) {
            case ARMMode::IRQ: return spsr_irq;
            case ARMMode::FIQ: return spsr_fiq;
            case ARMMode::SVC: return spsr_svc;
            case ARMMode::ABT: return spsr_abt;
            case ARMMode::UND: return spsr_und;
            default:           return cpsr; // USR/SYS have no SPSR
        }
    }

    void SetSPSR(uint32_t value) {
        switch (GetMode()) {
            case ARMMode::IRQ: spsr_irq = value; break;
            case ARMMode::FIQ: spsr_fiq = value; break;
            case ARMMode::SVC: spsr_svc = value; break;
            case ARMMode::ABT: spsr_abt = value; break;
            case ARMMode::UND: spsr_und = value; break;
            default: break; // USR/SYS — no SPSR to set
        }
    }

    void SwitchMode(uint32_t new_mode) {
        uint32_t old_mode = GetMode();
        if (old_mode == new_mode) return;

        SaveBankedRegisters(old_mode);

        cpsr = (cpsr & ~CPSRFlags::MODE_MASK) | (new_mode & CPSRFlags::MODE_MASK);

        RestoreBankedRegisters(new_mode);
    }

    void InitializeNDS9() {
        std::memset(r, 0, sizeof(r));
        cpsr = ARMMode::SVC | CPSRFlags::I | CPSRFlags::F; // Boot in SVC mode, IRQs disabled
        r[13] = 0x0380FD80; // ARM9 SVC stack pointer (per GBATEK)

        SwitchMode(ARMMode::IRQ);
        r[13] = 0x0380FF80; // ARM9 IRQ stack pointer

        SwitchMode(ARMMode::SYS);
        r[13] = 0x0380FFC0; // ARM9 System/User stack pointer

        SwitchMode(ARMMode::SVC); // Back to boot mode
        r[15] = 0x02000000; // Entry point (start of Main RAM where arm9.bin loads)
    }

    void InitializeNDS7() {
        std::memset(r, 0, sizeof(r));
        cpsr = ARMMode::SVC | CPSRFlags::I | CPSRFlags::F;
        r[13] = 0x0380FD80;

        SwitchMode(ARMMode::IRQ);
        r[13] = 0x0380FFB0;

        SwitchMode(ARMMode::SYS);
        r[13] = 0x0380FF00;

        SwitchMode(ARMMode::SVC);
        r[15] = 0x03800000; // ARM7 entry
    }

private:
    void SaveBankedRegisters(uint32_t mode) {
        switch (mode) {
            case ARMMode::IRQ:
                r13_irq = r[13]; r14_irq = r[14];
                break;
            case ARMMode::SVC:
                r13_svc = r[13]; r14_svc = r[14];
                break;
            case ARMMode::FIQ:
                r8_fiq = r[8]; r9_fiq = r[9]; r10_fiq = r[10];
                r11_fiq = r[11]; r12_fiq = r[12];
                r13_fiq = r[13]; r14_fiq = r[14];
                break;
            case ARMMode::ABT:
                r13_abt = r[13]; r14_abt = r[14];
                break;
            case ARMMode::UND:
                r13_und = r[13]; r14_und = r[14];
                break;
            default: break; // USR/SYS share registers
        }
    }

    void RestoreBankedRegisters(uint32_t new_mode) {
        switch (new_mode) {
            case ARMMode::IRQ:
                r[13] = r13_irq; r[14] = r14_irq;
                break;
            case ARMMode::SVC:
                r[13] = r13_svc; r[14] = r14_svc;
                break;
            case ARMMode::FIQ:
                r[8] = r8_fiq; r[9] = r9_fiq; r[10] = r10_fiq;
                r[11] = r11_fiq; r[12] = r12_fiq;
                r[13] = r13_fiq; r[14] = r14_fiq;
                break;
            case ARMMode::ABT:
                r[13] = r13_abt; r[14] = r14_abt;
                break;
            case ARMMode::UND:
                r[13] = r13_und; r[14] = r14_und;
                break;
            default: break;
        }
    }
};
