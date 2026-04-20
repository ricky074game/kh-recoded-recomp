#include <gtest/gtest.h>
#include <capstone/capstone.h>
#include "c_emitter.h"

class CEmitterTest : public ::testing::Test {
protected:
    csh handle;
    cs_insn* insn;
    CEmitter emitter;

    void SetUp() override {
        cs_open(CS_ARCH_ARM, CS_MODE_ARM, &handle);
        cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON);
        insn = cs_malloc(handle);
    }

    void TearDown() override {
        cs_free(insn, 1);
        cs_close(&handle);
    }

    void Decode(const uint8_t* code, size_t size, uint64_t address = 0x1000) {
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
