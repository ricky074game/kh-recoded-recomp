#include <gtest/gtest.h>
#include <capstone/capstone.h>
#include "c_emitter.h"

class CEmitterTest : public ::testing::Test {
protected:
    csh handle = 0;
    cs_insn* insn = nullptr;
    CEmitter emitter;

    void Reopen(cs_mode mode) {
        if (insn != nullptr) {
            cs_free(insn, 1);
            insn = nullptr;
        }
        if (handle != 0) {
            cs_close(&handle);
            handle = 0;
        }

        cs_open(CS_ARCH_ARM, mode, &handle);
        cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON);
        insn = cs_malloc(handle);
    }

    void SetUp() override {
        Reopen(CS_MODE_ARM);
    }

    void TearDown() override {
        if (insn != nullptr) {
            cs_free(insn, 1);
        }
        if (handle != 0) {
            cs_close(&handle);
        }
    }

    void Decode(const uint8_t* code, size_t size, uint64_t address = 0x1000) {
        Reopen(CS_MODE_ARM);
        size_t code_size = size;
        const uint8_t* code_ptr = code;
        cs_disasm_iter(handle, &code_ptr, &code_size, &address, insn);
    }

    void DecodeThumb(const uint8_t* code, size_t size, uint64_t address = 0x1000) {
        Reopen(CS_MODE_THUMB);
        size_t code_size = size;
        const uint8_t* code_ptr = code;
        cs_disasm_iter(handle, &code_ptr, &code_size, &address, insn);
    }
};

TEST_F(CEmitterTest, EmitBX_Thumb) {
    // BX R0
    uint8_t code[] = { 0x10, 0xFF, 0x2F, 0xE1 };
    Decode(code, sizeof(code));

    std::stringstream ss;
    emitter.EmitARMv4T_Thumb(insn, ss);
    std::string out = ss.str();
    EXPECT_TRUE(out.find("Core::Dispatcher::ExecuteDynamicBranch(ctx->r[0], ctx);") != std::string::npos);
}

TEST_F(CEmitterTest, EmitBLX_Thumb) {
    // BLX R1
    uint8_t code[] = { 0x31, 0xFF, 0x2F, 0xE1 };
    Decode(code, sizeof(code));

    std::stringstream ss;
    emitter.EmitARMv4T_Thumb(insn, ss);
    std::string out = ss.str();
    EXPECT_TRUE(out.find("ctx->r[14] = 4100;") != std::string::npos); // 0x1000 + 4
    EXPECT_TRUE(out.find("Core::Dispatcher::ExecuteDynamicBranch(ctx->r[1], ctx);") != std::string::npos);
}

TEST_F(CEmitterTest, EmitMCR_CacheFlush) {
    // MCR p15, 0, R0, c7, c5, 0
    uint8_t code[] = { 0x15, 0x0F, 0x07, 0xEE };
    Decode(code, sizeof(code));

    std::stringstream ss;
    emitter.EmitCoproc_CP15(insn, ss);
    std::string out = ss.str();
    EXPECT_TRUE(out.find("ctx->mem->InvalidateOverlayCache();") != std::string::npos);
}

TEST_F(CEmitterTest, EmitLDM) {
    // LDM R0, {R1, R2}
    uint8_t code[] = { 0x06, 0x00, 0x90, 0xE8 };
    Decode(code, sizeof(code));

    std::stringstream ss;
    emitter.EmitBlockDataTransfer(insn, ss);
    std::string out = ss.str();
    EXPECT_TRUE(out.find("ctx->mem->Read32(_addr);") != std::string::npos);
    EXPECT_TRUE(out.find("ctx->r[1]") != std::string::npos);
    EXPECT_TRUE(out.find("ctx->r[2]") != std::string::npos);
}

TEST_F(CEmitterTest, HandleSBitFlags) {
    // ADDS R0, R1, R2
    uint8_t code[] = { 0x02, 0x00, 0x91, 0xE0 };
    Decode(code, sizeof(code));

    std::stringstream ss;
    emitter.HandleSBitFlags(insn, ss);
    std::string out = ss.str();
    EXPECT_TRUE(out.find("ctx->cpsr = (ctx->cpsr & ~(0xF0000000))") != std::string::npos);
}

TEST_F(CEmitterTest, EmitLSR_ThumbImmediateUsesExplicitShiftOperand) {
    // lsrs r0, r4, #9
    uint8_t code[] = { 0x60, 0x0A };
    DecodeThumb(code, sizeof(code));

    std::string out = emitter.EmitInstruction(insn);
    EXPECT_TRUE(out.find("ctx->r[0] = ((uint32_t)(ctx->r[4]) >> 9);") != std::string::npos);
    EXPECT_TRUE(out.find("CalculateFlags_MOV(ctx, ctx->r[0]);") != std::string::npos);
}

TEST_F(CEmitterTest, EmitASR_ThumbImmediateUsesExplicitShiftOperand) {
    // asrs r1, r0, #4
    uint8_t code[] = { 0x01, 0x11 };
    DecodeThumb(code, sizeof(code));

    std::string out = emitter.EmitInstruction(insn);
    EXPECT_TRUE(out.find("ctx->r[1] = ((int32_t)(ctx->r[0]) >> 4);") != std::string::npos);
    EXPECT_TRUE(out.find("CalculateFlags_MOV(ctx, ctx->r[1]);") != std::string::npos);
}

TEST_F(CEmitterTest, EmitASR_ThumbRegisterShiftUsesDestinationAsSource) {
    // asrs r0, r0
    uint8_t code[] = { 0x00, 0x41 };
    DecodeThumb(code, sizeof(code));

    std::string out = emitter.EmitInstruction(insn);
    EXPECT_TRUE(out.find("ctx->r[0] = ((((ctx->r[0]) & 0xFFu) >= 32u) ? ((ctx->r[0] & 0x80000000u) ? 0xFFFFFFFFu : 0u) : ((int32_t)(ctx->r[0]) >> ((ctx->r[0]) & 0xFFu)));") != std::string::npos);
    EXPECT_TRUE(out.find("CalculateFlags_MOV(ctx, ctx->r[0]);") != std::string::npos);
}
