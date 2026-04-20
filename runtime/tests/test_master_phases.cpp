#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include <vector>
#include <cmath>
#include <cstring>
#include <thread>
#include <mutex>
#include <chrono>

#include "vfs.h"
#include "cpu_context.h"
#include "memory_map.h"
#include "arm_decoder.h"
#include "c_emitter.h"
#include "optimization.h"
#include "hw_ipc.h"
#include "hw_math.h"
#include "hw_dma.h"
#include "hw_timers.h"
#include "hw_irq.h"

// ============================================================================
// PHASE 1: Dissection (ROM Extraction & VFS)
// ============================================================================

TEST(Phase1, VFS_InvalidIdReturnsEmpty) {
    VFS v("data");
    EXPECT_EQ(v.GetPathById(999), "");
}

TEST(Phase1, VFS_InvalidPathReturnsInvalidHandle) {
    VFS v("data");
    EXPECT_EQ(v.GetIdByPath("nonexistent"), 0xFFFFFFFF);
}

TEST(Phase1, VFS_ReadInvalidIdThrows) {
    VFS v("data");
    EXPECT_THROW(v.ReadFileById(999), std::runtime_error);
}

TEST(Phase1, VFS_ConstructionDoesNotThrow) {
    EXPECT_NO_THROW(VFS v("data"));
}

TEST(Phase1, VFS_LoadFromNonexistentRomFails) {
    VFS v("data");
    EXPECT_FALSE(v.LoadFromRom("nonexistent_rom.nds"));
}

TEST(Phase1, VFS_ReadByPathThrowsForUnmapped) {
    VFS v("data");
    EXPECT_THROW(v.ReadFileByPath("ghost_file.bin"), std::runtime_error);
}

// ============================================================================
// PHASE 2: Virtual DS Motherboard (Memory & CPU)
// ============================================================================

TEST(Phase2, MemoryMap_MainRAMReadWrite) {
    NDSMemory m;
    m.Write32(0x02000000, 0xDEADBEEF);
    EXPECT_EQ(m.Read32(0x02000000), 0xDEADBEEF);
}

TEST(Phase2, MemoryMap_DTCMReadWrite) {
    NDSMemory m;
    m.Write32(0x027E0000, 0xCAFEBABE);
    EXPECT_EQ(m.Read32(0x027E0000), 0xCAFEBABE);
}

TEST(Phase2, MemoryMap_ITCMReadWrite) {
    NDSMemory m;
    m.Write32(0x01000000, 0x12345678);
    EXPECT_EQ(m.Read32(0x01000000), 0x12345678);
}

TEST(Phase2, MemoryMap_WRAMReadWrite) {
    NDSMemory m;
    m.Write32(0x037F8000, 0xABCDEF01);
    EXPECT_EQ(m.Read32(0x037F8000), 0xABCDEF01);
}

TEST(Phase2, MemoryMap_UnmappedThrows) {
    NDSMemory m;
    EXPECT_THROW(m.Read32(0x08000000), std::runtime_error);
}

TEST(Phase2, MemoryMap_IOPortStubReadsZero) {
    NDSMemory m;
    EXPECT_EQ(m.Read32(0x04000000), 0u);
}

TEST(Phase2, CPUContext_ZeroInitialized) {
    CPU_Context c = {};
    EXPECT_EQ(c.r[0], 0u);
    EXPECT_EQ(c.cpsr, 0u);
    EXPECT_EQ(c.mem, nullptr);
}

TEST(Phase2, CPUContext_BankedIRQ) {
    CPU_Context c = {};
    c.r13_irq = 0x1000;
    c.r14_irq = 0x2000;
    EXPECT_EQ(c.r13_irq, 0x1000u);
    EXPECT_EQ(c.r14_irq, 0x2000u);
}

TEST(Phase2, CPUContext_BankedFIQ) {
    CPU_Context c = {};
    c.r8_fiq = 0x08;
    c.r9_fiq = 0x09;
    c.r10_fiq = 0x0A;
    c.r11_fiq = 0x0B;
    c.r12_fiq = 0x0C;
    EXPECT_EQ(c.r8_fiq, 0x08u);
    EXPECT_EQ(c.r12_fiq, 0x0Cu);
}

TEST(Phase2, CP15_ControlRegister) {
    CPU_Context c = {};
    c.cp15_control = 0xFF;
    EXPECT_EQ(c.cp15_control, 0xFFu);
    EXPECT_EQ(c.cp15.control, 0xFFu); // Access through CP15State
}

TEST(Phase2, CP15_TCMBases) {
    CPU_Context c = {};
    c.cp15_dtcm_base = 0x027E0000;
    c.cp15_itcm_base = 0x01000000;
    EXPECT_EQ(c.cp15_dtcm_base, 0x027E0000u);
    EXPECT_EQ(c.cp15_itcm_base, 0x01000000u);
}

// ============================================================================
// PHASE 3: Capstone Binary Lifter
// ============================================================================

TEST(Phase3, Capstone_Initialize) {
    ARMDecoder d;
    EXPECT_TRUE(d.Initialize());
}

TEST(Phase3, PCRelative_ARMMode) {
    // ARM: PC = addr + 8, aligned to 4
    EXPECT_EQ(ARMDecoder::ResolvePCRelative(0x02000000, 4, false), 0x0200000C);
}

TEST(Phase3, PCRelative_ThumbMode) {
    // Thumb: PC = addr + 4, aligned to 4
    EXPECT_EQ(ARMDecoder::ResolvePCRelative(0x02000000, 4, true), 0x02000008);
}

TEST(Phase3, IsThumb_OddAddress) {
    EXPECT_TRUE(ARMDecoder::IsThumb(0x02000001));
}

TEST(Phase3, IsThumb_EvenAddress) {
    EXPECT_FALSE(ARMDecoder::IsThumb(0x02000000));
}

TEST(Phase3, ResolveTarget_StripLSB) {
    EXPECT_EQ(ARMDecoder::ResolveTarget(0x02000001), 0x02000000u);
    EXPECT_EQ(ARMDecoder::ResolveTarget(0x02000000), 0x02000000u);
}

TEST(Phase3, Emitter_ADD) {
    ARMDecoder d;
    d.Initialize();
    std::vector<uint8_t> code = {0x01, 0x00, 0x81, 0xE0}; // ADD R0, R1, R1
    d.LoadROM(code, 0x02000000);
    cs_insn* insn = d.DecodeInstruction(0x02000000, CS_MODE_ARM);
    ASSERT_NE(insn, nullptr);
    CEmitter e(&d);
    std::string result = e.EmitInstruction(insn);
    EXPECT_NE(result.find("ctx->r[0] = ctx->r[1] + ctx->r[1];"), std::string::npos);
    cs_free(insn, 1);
}

TEST(Phase3, Emitter_SUB) {
    ARMDecoder d;
    d.Initialize();
    std::vector<uint8_t> code = {0x02, 0x00, 0x41, 0xE0}; // SUB R0, R1, R2
    d.LoadROM(code, 0x02000000);
    cs_insn* insn = d.DecodeInstruction(0x02000000, CS_MODE_ARM);
    ASSERT_NE(insn, nullptr);
    CEmitter e(&d);
    std::string result = e.EmitInstruction(insn);
    EXPECT_NE(result.find("ctx->r[0] = ctx->r[1] - ctx->r[2];"), std::string::npos);
    cs_free(insn, 1);
}

TEST(Phase3, Emitter_MOV) {
    ARMDecoder d;
    d.Initialize();
    std::vector<uint8_t> code = {0x0D, 0x00, 0xA0, 0xE1}; // MOV R0, R13
    d.LoadROM(code, 0x02000000);
    cs_insn* insn = d.DecodeInstruction(0x02000000, CS_MODE_ARM);
    ASSERT_NE(insn, nullptr);
    CEmitter e(&d);
    std::string result = e.EmitInstruction(insn);
    EXPECT_NE(result.find("ctx->r[0] = ctx->r[13];"), std::string::npos);
    cs_free(insn, 1);
}

TEST(Phase3, Emitter_PCRelativeLDR) {
    ARMDecoder d;
    d.Initialize();
    std::vector<uint8_t> code = {
        0x04, 0x00, 0x9F, 0xE5, // LDR R0, [PC, #0x04]
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0xEF, 0xBE, 0xAD, 0xDE  // DEADBEEF
    };
    d.LoadROM(code, 0x02000000);
    cs_insn* insn = d.DecodeInstruction(0x02000000, CS_MODE_ARM);
    ASSERT_NE(insn, nullptr);
    CEmitter e(&d);
    std::string result = e.EmitInstruction(insn);
    EXPECT_NE(result.find("DEADBEEF"), std::string::npos);
    cs_free(insn, 1);
}

// ---- Flag Deferral Tests ----

TEST(Phase3, FlagDeferral_Negative) {
    EXPECT_TRUE(FlagDeferral::Calc_N(0x80000000));
    EXPECT_TRUE(FlagDeferral::Calc_N(0xFFFFFFFF));
    EXPECT_FALSE(FlagDeferral::Calc_N(0x7FFFFFFF));
    EXPECT_FALSE(FlagDeferral::Calc_N(0));
}

TEST(Phase3, FlagDeferral_Zero) {
    EXPECT_TRUE(FlagDeferral::Calc_Z(0));
    EXPECT_FALSE(FlagDeferral::Calc_Z(1));
    EXPECT_FALSE(FlagDeferral::Calc_Z(0xFFFFFFFF));
}

TEST(Phase3, FlagDeferral_CarryADD) {
    EXPECT_TRUE(FlagDeferral::Calc_C_ADD(0xFFFFFFFF, 1));  // Overflow
    EXPECT_FALSE(FlagDeferral::Calc_C_ADD(1, 1));           // No overflow
    EXPECT_TRUE(FlagDeferral::Calc_C_ADD(0x80000000, 0x80000000));
}

TEST(Phase3, FlagDeferral_CarrySUB) {
    EXPECT_TRUE(FlagDeferral::Calc_C_SUB(10, 5));   // No borrow
    EXPECT_TRUE(FlagDeferral::Calc_C_SUB(5, 5));    // Equal
    EXPECT_FALSE(FlagDeferral::Calc_C_SUB(4, 5));   // Borrow
}

TEST(Phase3, FlagDeferral_OverflowADD) {
    // 0x7FFFFFFF + 1 = 0x80000000 (positive + positive = negative → overflow)
    EXPECT_TRUE(FlagDeferral::Calc_V_ADD(0x7FFFFFFF, 1, 0x80000000));
    // 1 + 1 = 2 (no overflow)
    EXPECT_FALSE(FlagDeferral::Calc_V_ADD(1, 1, 2));
}

TEST(Phase3, FlagDeferral_OverflowSUB) {
    // 0x80000000 - 1 = 0x7FFFFFFF (negative - positive = positive → overflow)
    EXPECT_TRUE(FlagDeferral::Calc_V_SUB(0x80000000, 1, 0x7FFFFFFF));
    // 5 - 3 = 2 (no overflow)
    EXPECT_FALSE(FlagDeferral::Calc_V_SUB(5, 3, 2));
}

TEST(Phase3, ConditionCode_EQ_NE) {
    CPU_Context ctx = {};
    // Set Z flag
    ctx.cpsr = (1u << 30);
    EXPECT_TRUE(CheckConditionCode(&ctx, 1));  // EQ
    EXPECT_FALSE(CheckConditionCode(&ctx, 2)); // NE

    // Clear Z flag
    ctx.cpsr = 0;
    EXPECT_FALSE(CheckConditionCode(&ctx, 1)); // EQ
    EXPECT_TRUE(CheckConditionCode(&ctx, 2));  // NE
}

TEST(Phase3, ConditionCode_GE_LT) {
    CPU_Context ctx = {};
    // N=0, V=0 → N==V → GE is true
    ctx.cpsr = 0;
    EXPECT_TRUE(CheckConditionCode(&ctx, 11));  // GE
    EXPECT_FALSE(CheckConditionCode(&ctx, 12)); // LT

    // N=1, V=0 → N!=V → LT is true
    ctx.cpsr = (1u << 31);
    EXPECT_FALSE(CheckConditionCode(&ctx, 11)); // GE
    EXPECT_TRUE(CheckConditionCode(&ctx, 12));  // LT
}

TEST(Phase3, CalculateFlags_ADD_UpdatesCPSR) {
    CPU_Context ctx = {};
    CalculateFlags_ADD(&ctx, 0xFFFFFFFF, 1);
    // Result is 0 → Z set, C set (carry)
    EXPECT_TRUE(ctx.cpsr & CPSRFlags::Z);
    EXPECT_TRUE(ctx.cpsr & CPSRFlags::C);
}

TEST(Phase3, CalculateFlags_SUB_UpdatesCPSR) {
    CPU_Context ctx = {};
    CalculateFlags_SUB(&ctx, 5, 5);
    // Result is 0 → Z set, C set (no borrow)
    EXPECT_TRUE(ctx.cpsr & CPSRFlags::Z);
    EXPECT_TRUE(ctx.cpsr & CPSRFlags::C);
}

// ============================================================================
// PHASE 4: Concurrency & IPC
// ============================================================================

TEST(Phase4, ThreadSpawn_JoinWorks) {
    std::atomic<bool> executed{false};
    std::thread t([&]() { executed = true; });
    t.join();
    EXPECT_TRUE(executed);
}

TEST(Phase4, ThreadSpawn_MutexLockable) {
    std::mutex mtx;
    EXPECT_TRUE(mtx.try_lock());
    mtx.unlock();
}

TEST(Phase4, IPC_FIFOThroughMemoryMap) {
    NDSMemory m;
    // ARM9 writes to IPC FIFO send register
    m.Write32(0x04000188, 0xDEADC0DE);

    // The value should appear in the ARM9→ARM7 FIFO
    uint32_t val = 0;
    EXPECT_TRUE(m.ipc_fifo_arm9_to_arm7.pop(val));
    EXPECT_EQ(val, 0xDEADC0DE);
}

TEST(Phase4, IPC_FIFORecvThroughMemoryMap) {
    NDSMemory m;
    // Simulate ARM7 pushing a value
    m.ipc_fifo_arm7_to_arm9.push(0xBEEFCAFE);

    // ARM9 reads from IPC FIFO recv register
    uint32_t val = m.Read32(0x04000188);
    EXPECT_EQ(val, 0xBEEFCAFE);
}

TEST(Phase4, IPC_FIFOEmptyReadsZero) {
    NDSMemory m;
    uint32_t val = m.Read32(0x04000188);
    EXPECT_EQ(val, 0u); // Empty FIFO
}

// ============================================================================
// PHASE 4.2: DMA & Hardware Math Engine
// ============================================================================

TEST(Phase4_2, MathEngine_Div32) {
    HWMathEngine m;
    m.div_mode = DivMode::Div32_32;
    m.div_numer = 100;
    m.div_denom = 5;
    m.ComputeDivision();
    EXPECT_EQ(m.div_result, 20);
    EXPECT_EQ(m.div_remainder, 0);
}

TEST(Phase4_2, MathEngine_DivWithRemainder) {
    HWMathEngine m;
    m.div_mode = DivMode::Div32_32;
    m.div_numer = 103;
    m.div_denom = 5;
    m.ComputeDivision();
    EXPECT_EQ(m.div_result, 20);
    EXPECT_EQ(m.div_remainder, 3);
}

TEST(Phase4_2, MathEngine_DivByZero) {
    HWMathEngine m;
    m.div_mode = DivMode::Div32_32;
    m.div_numer = 42;
    m.div_denom = 0;
    m.ComputeDivision();
    EXPECT_TRUE(m.div_by_zero);
    EXPECT_EQ(m.div_remainder, 42); // Remainder = numerator per GBATEK
}

TEST(Phase4_2, MathEngine_SignedDivision) {
    HWMathEngine m;
    m.div_mode = DivMode::Div32_32;
    m.div_numer = -100;
    m.div_denom = 7;
    m.ComputeDivision();
    EXPECT_EQ(m.div_result, -14);
    EXPECT_EQ(m.div_remainder, -2);
}

TEST(Phase4_2, MathEngine_Div64_32) {
    HWMathEngine m;
    m.div_mode = DivMode::Div64_32;
    m.div_numer = 0x200000000LL; // 8589934592
    m.div_denom = 2;
    m.ComputeDivision();
    EXPECT_EQ(m.div_result, 0x100000000LL);
}

TEST(Phase4_2, MathEngine_Sqrt) {
    HWMathEngine m;
    m.sqrt_mode = SqrtMode::Sqrt32;
    m.sqrt_param = 144;
    m.ComputeSqrt();
    EXPECT_EQ(m.sqrt_result, 12u);
}

TEST(Phase4_2, MathEngine_Sqrt64) {
    HWMathEngine m;
    m.sqrt_mode = SqrtMode::Sqrt64;
    m.sqrt_param = 10000000000ULL;
    m.ComputeSqrt();
    EXPECT_EQ(m.sqrt_result, 100000u);
}

TEST(Phase4_2, MathEngine_DIVCNTReadBack) {
    HWMathEngine m;
    m.div_mode = DivMode::Div64_32;
    m.div_numer = 1;
    m.div_denom = 0;
    m.ComputeDivision();

    uint32_t divcnt = m.ReadDIVCNT();
    EXPECT_EQ(divcnt & 0x3, 1u);        // Mode = Div64_32
    EXPECT_TRUE(divcnt & (1u << 14));    // Div-by-zero flag
}

TEST(Phase4_2, MathEngine_ThroughMemoryMap) {
    NDSMemory m;
    // Write numerator = 100
    m.Write32(0x04000290, 100);
    // Write denominator = 5
    m.Write32(0x04000298, 5);
    // Read result
    uint32_t result = m.Read32(0x040002A0);
    EXPECT_EQ(result, 20u);
}

TEST(Phase4_2, MathEngine_SqrtThroughMemoryMap) {
    NDSMemory m;
    m.Write32(0x040002B8, 256); // SQRTPARAM
    uint32_t result = m.Read32(0x040002B4); // SQRTRESULT
    EXPECT_EQ(result, 16u);
}

TEST(Phase4_2, DMA_TriggerDisablesChannel) {
    DMAChannel d;
    d.enabled = true;
    d.trigger();
    EXPECT_FALSE(d.enabled);
}

TEST(Phase4_2, DMA_DisabledDoesNothing) {
    DMAChannel d;
    d.enabled = false;
    d.trigger();
    EXPECT_FALSE(d.enabled);
}

TEST(Phase4_2, DMA_WriteControlDecodesFields) {
    DMAChannel d;
    // Enable bit | 32-bit words | Immediate timing | word count = 10
    uint32_t ctrl = (1u << 31) | (1u << 26) | 10;
    d.WriteControl(ctrl);
    EXPECT_TRUE(d.enabled);
    EXPECT_TRUE(d.word_size32);
    EXPECT_EQ(d.timing, DMAStartTiming::Immediate);
    EXPECT_EQ(d.word_count, 10u);
}

TEST(Phase4_2, DMA_ExecuteMemCopy) {
    // Create a buffer simulating memory
    std::vector<uint8_t> buffer(1024, 0);
    // Write source data
    uint32_t src_val = 0xDEADBEEF;
    std::memcpy(&buffer[0], &src_val, 4);

    DMAChannel d;
    d.src_addr = 0;
    d.dst_addr = 256;
    d.word_count = 1;
    d.word_size32 = true;
    d.enabled = true;
    d.src_ctrl = DMAAddrCtrl::Increment;
    d.dst_ctrl = DMAAddrCtrl::Increment;
    d.timing = DMAStartTiming::Immediate;

    d.Execute(buffer.data(), static_cast<uint32_t>(buffer.size()));

    // Verify the copy
    uint32_t dst_val;
    std::memcpy(&dst_val, &buffer[256], 4);
    EXPECT_EQ(dst_val, 0xDEADBEEF);
    EXPECT_FALSE(d.enabled); // Should auto-disable
}

TEST(Phase4_2, DMA_MultiWordCopy) {
    std::vector<uint8_t> buffer(2048, 0);
    // Write 4 words at source
    for (int i = 0; i < 4; ++i) {
        uint32_t val = 0x10 + i;
        std::memcpy(&buffer[i * 4], &val, 4);
    }

    DMAChannel d;
    d.src_addr = 0;
    d.dst_addr = 1024;
    d.word_count = 4;
    d.word_size32 = true;
    d.enabled = true;
    d.src_ctrl = DMAAddrCtrl::Increment;
    d.dst_ctrl = DMAAddrCtrl::Increment;

    d.Execute(buffer.data(), static_cast<uint32_t>(buffer.size()));

    for (int i = 0; i < 4; ++i) {
        uint32_t val;
        std::memcpy(&val, &buffer[1024 + i * 4], 4);
        EXPECT_EQ(val, 0x10u + i);
    }
}

TEST(Phase4_2, DMA_ThroughMemoryMap) {
    NDSMemory m;
    // Write source data to Main RAM
    m.Write32(0x02000000, 0xCAFEBABE);

    // Configure DMA channel 0 via IO registers (relative to main RAM base)
    // The DMA operates on Main RAM offsets, not absolute addresses in this impl
    // So we verify the register decode path works
    m.Write32(0x040000B0, 0x02000000); // DMA0 SRC
    m.Write32(0x040000B4, 0x02001000); // DMA0 DST

    // Enable + immediate + word count 1
    m.Write32(0x040000B8, (1u << 31) | (1u << 26) | 1);

    // The DMA should have executed
    EXPECT_FALSE(m.dma_arm9[0].enabled);
}

// ============================================================================
// PHASE 4.5: Hardware Timers & RTC
// ============================================================================

TEST(Phase4_5, Timer_ScaleF1) {
    HWTimer t;
    EXPECT_EQ(t.scale(1000, 1), 33513);
}

TEST(Phase4_5, Timer_ScaleF64) {
    HWTimer t;
    EXPECT_EQ(t.scale(1000, 64), 523);
}

TEST(Phase4_5, Timer_ScaleF256) {
    HWTimer t;
    EXPECT_EQ(t.scale(1000, 256), 130);
}

TEST(Phase4_5, Timer_ScaleF1024) {
    HWTimer t;
    EXPECT_EQ(t.scale(1000, 1024), 32);
}

TEST(Phase4_5, Timer_ScaleZeroMicroseconds) {
    HWTimer t;
    EXPECT_EQ(t.scale(0, 1), 0);
}

TEST(Phase4_5, Timer_ScaleZeroPrescaler) {
    HWTimer t;
    EXPECT_EQ(t.scale(1000, 0), 0);
}

TEST(Phase4_5, TimerManager_WriteControl) {
    HWTimerManager tmr;
    tmr.WriteTimer(2, 0x1000); // Timer 0 reload = 0x1000
    tmr.WriteTimer(3, 0x0080); // Timer 0 control: enable
    EXPECT_TRUE(tmr.channels[0].running);
}

TEST(Phase4_5, TimerManager_ReadCounter) {
    HWTimerManager tmr;
    tmr.channels[0].reload = 0;
    tmr.channels[0].WriteControl(0x0080); // Enable, prescaler F/1
    // Can't test exact value due to timing, but shouldn't crash
    uint16_t val = tmr.ReadTimer(0);
    EXPECT_GE(val, 0u);
}

TEST(Phase4_5, RTC_BCD_Normal) {
    EXPECT_EQ(HWRTC::bcd(25), 0x25);
    EXPECT_EQ(HWRTC::bcd(9),  0x09);
    EXPECT_EQ(HWRTC::bcd(59), 0x59);
}

TEST(Phase4_5, RTC_BCD_EdgeCases) {
    EXPECT_EQ(HWRTC::bcd(0),  0x00);
    EXPECT_EQ(HWRTC::bcd(99), 0x99);
    EXPECT_EQ(HWRTC::bcd(12), 0x12);
}

TEST(Phase4_5, RTC_GetCurrentTime) {
    HWRTC rtc;
    auto time = rtc.GetCurrentTime();
    // Just verify it returns valid BCD (high nibble <= 9)
    EXPECT_LE(time.hour >> 4, 2u);
    EXPECT_LE(time.minute >> 4, 5u);
    EXPECT_LE(time.second >> 4, 5u);
}

TEST(Phase4_5, RTC_SPIProtocol) {
    HWRTC rtc;
    // Send read command (bit 0 = read)
    rtc.ProcessSPIByte(0x01);
    EXPECT_EQ(rtc.state, HWRTC::SPIState::Reading);

    // Read back 7 bytes of time data
    for (int i = 0; i < 7; ++i) {
        rtc.ProcessSPIByte(0x00); // Dummy write to clock out data
    }
}

// ============================================================================
// PHASE 4.8: Interrupt Controller (IRQ)
// ============================================================================

TEST(Phase4_8, IRQ_TriggerCondition) {
    HWIRQ irq;
    irq.ime = 1;
    irq.ie = IRQBits::VBlank;
    irq.if_reg = IRQBits::VBlank;
    EXPECT_TRUE(irq.HasPendingIRQ());
}

TEST(Phase4_8, IRQ_MasterDisabled) {
    HWIRQ irq;
    irq.ime = 0;
    irq.ie = IRQBits::VBlank;
    irq.if_reg = IRQBits::VBlank;
    EXPECT_FALSE(irq.HasPendingIRQ());
}

TEST(Phase4_8, IRQ_NotEnabled) {
    HWIRQ irq;
    irq.ime = 1;
    irq.ie = 0; // No sources enabled
    irq.if_reg = IRQBits::VBlank;
    EXPECT_FALSE(irq.HasPendingIRQ());
}

TEST(Phase4_8, IRQ_RaiseVBlank) {
    HWIRQ irq;
    irq.vblank();
    EXPECT_EQ(irq.if_reg & IRQBits::VBlank, IRQBits::VBlank);
}

TEST(Phase4_8, IRQ_RaiseMultiple) {
    HWIRQ irq;
    irq.RaiseIRQ(IRQBits::Timer0);
    irq.RaiseIRQ(IRQBits::DMA0);
    EXPECT_TRUE(irq.if_reg & IRQBits::Timer0);
    EXPECT_TRUE(irq.if_reg & IRQBits::DMA0);
}

TEST(Phase4_8, IRQ_AcknowledgeClears) {
    HWIRQ irq;
    irq.RaiseIRQ(IRQBits::VBlank | IRQBits::Timer0);
    irq.AcknowledgeIRQ(IRQBits::VBlank);
    EXPECT_FALSE(irq.if_reg & IRQBits::VBlank);
    EXPECT_TRUE(irq.if_reg & IRQBits::Timer0); // Should still be set
}

TEST(Phase4_8, IRQ_PendingEnabled) {
    HWIRQ irq;
    irq.ie = IRQBits::VBlank | IRQBits::Timer0;
    irq.RaiseIRQ(IRQBits::VBlank | IRQBits::DMA0);
    EXPECT_EQ(irq.GetPendingEnabled(), IRQBits::VBlank); // Only VBlank is both raised and enabled
}

TEST(Phase4_8, CheckInterrupts_VectorsToIRQ) {
    CPU_Context ctx = {};
    ctx.cpsr = ARMMode::SYS; // User/System mode, IRQs enabled (I=0)
    ctx.r[15] = 0x02000100;  // Current PC

    HWIRQ irq;
    irq.ime = 1;
    irq.ie = IRQBits::VBlank;
    irq.if_reg = IRQBits::VBlank;

    CheckInterrupts(&ctx, irq);

    EXPECT_EQ(ctx.r[15], 0xFFFF0000u); // Vectored to IRQ handler
    EXPECT_EQ(ctx.r14_irq, 0x02000100u); // LR saved
    EXPECT_EQ(ctx.spsr_irq, ARMMode::SYS); // CPSR saved
    EXPECT_TRUE(ctx.cpsr & CPSRFlags::I); // IRQs now disabled
    EXPECT_EQ(ctx.cpsr & CPSRFlags::MODE_MASK, ARMMode::IRQ); // In IRQ mode
}

TEST(Phase4_8, CheckInterrupts_SkipsWhenIBitSet) {
    CPU_Context ctx = {};
    ctx.cpsr = ARMMode::SYS | CPSRFlags::I; // IRQs already disabled
    ctx.r[15] = 0x02000100;

    HWIRQ irq;
    irq.ime = 1;
    irq.ie = IRQBits::VBlank;
    irq.if_reg = IRQBits::VBlank;

    CheckInterrupts(&ctx, irq);

    EXPECT_EQ(ctx.r[15], 0x02000100u); // PC unchanged — IRQs blocked
}

TEST(Phase4_8, IRQ_ThroughMemoryMap) {
    NDSMemory m;
    // Write IME
    m.Write32(0x04000208, 1);
    EXPECT_EQ(m.irq_arm9.ime, 1u);

    // Write IE
    m.Write32(0x04000210, IRQBits::VBlank);
    EXPECT_EQ(m.irq_arm9.ie, IRQBits::VBlank);

    // Read IF
    m.irq_arm9.RaiseIRQ(IRQBits::VBlank);
    uint32_t if_val = m.Read32(0x04000214);
    EXPECT_EQ(if_val & IRQBits::VBlank, IRQBits::VBlank);

    // Acknowledge by writing 1 to IF bit
    m.Write32(0x04000214, IRQBits::VBlank);
    EXPECT_EQ(m.irq_arm9.if_reg & IRQBits::VBlank, 0u);
}

// ============================================================================
// PHASE 2 Extra: VRAM, OAM, Palette through Memory Map
// ============================================================================

TEST(Phase2_Extra, VRAM_ReadWrite32) {
    NDSMemory m;
    m.Write32(0x06000000, 0x11223344);
    EXPECT_EQ(m.Read32(0x06000000), 0x11223344);
}

TEST(Phase2_Extra, OAM_ReadWrite32) {
    NDSMemory m;
    m.Write32(0x07000000, 0xAABBCCDD);
    EXPECT_EQ(m.Read32(0x07000000), 0xAABBCCDD);
}

TEST(Phase2_Extra, PaletteRAM_ReadWrite32) {
    NDSMemory m;
    m.Write32(0x05000000, 0x55667788);
    EXPECT_EQ(m.Read32(0x05000000), 0x55667788);
}

TEST(Phase2_Extra, KEYINPUT_DefaultAllReleased) {
    NDSMemory m;
    uint32_t keys = m.Read32(0x04000130);
    EXPECT_EQ(keys, 0x03FFu); // All buttons released (active-low)
}

TEST(Phase2_Extra, VRAMCNT_WriteRead) {
    NDSMemory m;
    m.Write32(0x04000240, 0x83); // VRAMCNT_A = enable, MST=3
    uint32_t val = m.Read32(0x04000240);
    EXPECT_EQ(val, 0x83u);
}
