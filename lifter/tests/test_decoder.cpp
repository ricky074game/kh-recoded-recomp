#include <gtest/gtest.h>
#include "arm_decoder.h"

class ARMDecoderTest : public ::testing::Test {
protected:
    ARMDecoder decoder;

    void SetUp() override {
        ASSERT_TRUE(decoder.Initialize());
    }
};

TEST_F(ARMDecoderTest, DecodesARMInstruction) {
    // e0810002 = ADD R0, R1, R2
    std::vector<uint8_t> data = {0x02, 0x00, 0x81, 0xE0};
    decoder.LoadROM(data, 0x02000000);

    cs_insn* insn = decoder.DecodeInstruction(0x02000000, CS_MODE_ARM);
    ASSERT_NE(insn, nullptr);
    EXPECT_EQ(insn->id, ARM_INS_ADD);

    cs_free(insn, 1);
}

TEST_F(ARMDecoderTest, AnalyzesControlFlowAndAvoidsLiteralPool) {
    // 0x02000000: B 0x02000008 (EA000000)
    // 0x02000004: DEADBEEF (Literal Pool / Data)
    // 0x02000008: MOV R0, #1 (E3A00001)
    std::vector<uint8_t> data = {
        0x00, 0x00, 0x00, 0xEA, // B pc+8 (offset is +8 from PC which is at 0x02000008, so target is 0x02000000 + 8 = 0x02000008)
        0xEF, 0xBE, 0xAD, 0xDE, // DEADBEEF
        0x01, 0x00, 0xA0, 0xE3  // MOV R0, #1
    };

    decoder.LoadROM(data, 0x02000000);
    decoder.AnalyzeControlFlow(0x02000000);

    const auto& instructions = decoder.GetDecodedInstructions();

    // We should have analyzed 0x02000000 and 0x02000008, skipping 0x02000004
    EXPECT_TRUE(instructions.find(0x02000000) != instructions.end());
    EXPECT_TRUE(instructions.find(0x02000004) == instructions.end());
    EXPECT_TRUE(instructions.find(0x02000008) != instructions.end());
}
