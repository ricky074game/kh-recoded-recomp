#include <gtest/gtest.h>
#include "hw_math.h"

#include <limits>

extern HWMathEngine g_math_engine;

class MathTest : public ::testing::Test {
protected:
    HWMathEngine engine;
};

TEST_F(MathTest, Div32SignedProducesQuotientAndRemainder) {
    engine.div_mode = DivMode::Div32_32;
    engine.div_numer = static_cast<int32_t>(-101);
    engine.div_denom = static_cast<int32_t>(10);
    engine.ComputeDivision();

    EXPECT_EQ(engine.div_result, -10);
    EXPECT_EQ(engine.div_remainder, -1);
    EXPECT_FALSE(engine.div_by_zero);
}

TEST_F(MathTest, Div64By32Signed) {
    engine.div_mode = DivMode::Div64_32;
    engine.div_numer = -5000;
    engine.div_denom = static_cast<int32_t>(-64);
    engine.ComputeDivision();

    EXPECT_EQ(engine.div_result, 78);
    EXPECT_EQ(engine.div_remainder, -8);
}

TEST_F(MathTest, Div64By64Signed) {
    engine.div_mode = DivMode::Div64_64;
    engine.div_numer = 9223372036854775000LL;
    engine.div_denom = 4096;
    engine.ComputeDivision();

    EXPECT_EQ(engine.div_result, 2251799813685247LL);
    EXPECT_EQ(engine.div_remainder, 3288);
}

TEST_F(MathTest, DivByZeroSetsFlagAndSaturates) {
    engine.div_mode = DivMode::Div32_32;
    engine.div_numer = 123;
    engine.div_denom = 0;
    engine.ComputeDivision();

    EXPECT_TRUE(engine.div_by_zero);
    EXPECT_EQ(engine.div_result, static_cast<int64_t>(std::numeric_limits<int32_t>::max()));
    EXPECT_EQ(engine.div_remainder, 123);
    EXPECT_NE(engine.ReadDIVCNT() & (1u << 14), 0u);
}

TEST_F(MathTest, DivOverflowMinIntHandledWithoutUB) {
    engine.div_mode = DivMode::Div32_32;
    engine.div_numer = static_cast<int32_t>(std::numeric_limits<int32_t>::min());
    engine.div_denom = static_cast<int32_t>(-1);
    engine.ComputeDivision();

    EXPECT_EQ(engine.div_result, static_cast<int64_t>(std::numeric_limits<int32_t>::min()));
    EXPECT_EQ(engine.div_remainder, 0);
}

TEST_F(MathTest, Sqrt32AndSqrt64UseIntegerSqrt) {
    engine.sqrt_mode = SqrtMode::Sqrt32;
    engine.sqrt_param = 144;
    engine.ComputeSqrt();
    EXPECT_EQ(engine.sqrt_result, 12u);

    engine.sqrt_mode = SqrtMode::Sqrt64;
    engine.sqrt_param = 18446744073709551615ULL;
    engine.ComputeSqrt();
    EXPECT_EQ(engine.sqrt_result, 4294967295u);
}

TEST(MathStandaloneWrappers, WrapperFunctionsDriveGlobalEngine) {
    g_math_engine.div_numer = 81;
    g_math_engine.div_denom = 9;
    ComputeDiv32();
    EXPECT_EQ(g_math_engine.div_result, 9);

    g_math_engine.sqrt_param = 256;
    ComputeSqrt32();
    EXPECT_EQ(g_math_engine.sqrt_result, 16u);

    EXPECT_NO_THROW(HandleMathExceptions());
}
