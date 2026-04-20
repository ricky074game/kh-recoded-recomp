#pragma once

// ============================================================================
// hw_math.h — Nintendo DS Hardware Math Engine Emulation
//
// The DS has a dedicated division and square root unit mapped to IO registers.
// Games write operands, then read results. On real hardware this takes a few
// cycles; we compute instantly and return. The "busy" flag is always 0.
//
// Reference: GBATEK §DS Math
//   Division: 0x04000280 (DIVCNT) through 0x040002A8
//   Sqrt:     0x040002B0 (SQRTCNT) through 0x040002B8
// ============================================================================

#include <cstdint>
#include <cmath>
#include <limits>

// ---- Division Modes (DIVCNT bits 0-1) ----
enum class DivMode : uint8_t {
    Div32_32 = 0, // 32-bit ÷ 32-bit = 32-bit quotient, 32-bit remainder
    Div64_32 = 1, // 64-bit ÷ 32-bit = 64-bit quotient, 32-bit remainder
    Div64_64 = 2  // 64-bit ÷ 64-bit = 64-bit quotient, 64-bit remainder
};

// ---- Square Root Modes (SQRTCNT bit 0) ----
enum class SqrtMode : uint8_t {
    Sqrt32 = 0, // sqrt of 32-bit input → 32-bit result
    Sqrt64 = 1  // sqrt of 64-bit input → 32-bit result
};

class HWMathEngine {
public:
    // ---- Division Registers ----
    DivMode  div_mode      = DivMode::Div32_32;
    int64_t  div_numer     = 0;  // DIVNUMER (64-bit, signed)
    int64_t  div_denom     = 0;  // DIVDENOM (64-bit, signed)
    int64_t  div_result    = 0;  // DIVRESULT (quotient)
    int64_t  div_remainder = 0;  // DIVREMRESULT (remainder)
    bool     div_by_zero   = false;

    // ---- Sqrt Registers ----
    SqrtMode sqrt_mode     = SqrtMode::Sqrt32;
    uint64_t sqrt_param    = 0;  // SQRTPARAM
    uint32_t sqrt_result   = 0;  // SQRTRESULT

    HWMathEngine() = default;

    static uint64_t IntegerSqrt(uint64_t value) {
        uint64_t bit = 1ull << 62;
        uint64_t result = 0;

        while (bit > value) {
            bit >>= 2;
        }

        while (bit != 0) {
            if (value >= result + bit) {
                value -= result + bit;
                result = (result >> 1) + bit;
            } else {
                result >>= 1;
            }
            bit >>= 2;
        }

        return result;
    }

private:
    static int64_t SaturatedResult(bool negative, DivMode mode) {
        if (mode == DivMode::Div32_32) {
            return negative ? static_cast<int64_t>(std::numeric_limits<int32_t>::min())
                            : static_cast<int64_t>(std::numeric_limits<int32_t>::max());
        }
        return negative ? std::numeric_limits<int64_t>::min()
                        : std::numeric_limits<int64_t>::max();
    }

public:

    // Performs the division using the currently configured mode and operands.
    // Per GBATEK: division by zero produces ±MAX result, remainder = numerator.
    void ComputeDivision() {
        div_by_zero = false;

        switch (div_mode) {
            case DivMode::Div32_32: {
                const int32_t n = static_cast<int32_t>(div_numer & 0xFFFFFFFFu);
                const int32_t d = static_cast<int32_t>(div_denom & 0xFFFFFFFFu);
                if (d == 0) {
                    div_by_zero = true;
                    div_result = SaturatedResult(n < 0, DivMode::Div32_32);
                    div_remainder = static_cast<int64_t>(n);
                    break;
                }

                if (n == std::numeric_limits<int32_t>::min() && d == -1) {
                    div_result = static_cast<int64_t>(std::numeric_limits<int32_t>::min());
                    div_remainder = 0;
                    break;
                }

                div_result = static_cast<int64_t>(n / d);
                div_remainder = static_cast<int64_t>(n % d);
                break;
            }
            case DivMode::Div64_32: {
                const int64_t n = div_numer;
                const int32_t d32 = static_cast<int32_t>(div_denom & 0xFFFFFFFFu);
                if (d32 == 0) {
                    div_by_zero = true;
                    div_result = SaturatedResult(n < 0, DivMode::Div64_32);
                    div_remainder = n;
                    break;
                }

                const int64_t d = static_cast<int64_t>(d32);
                if (n == std::numeric_limits<int64_t>::min() && d == -1) {
                    div_result = std::numeric_limits<int64_t>::min();
                    div_remainder = 0;
                    break;
                }

                div_result = n / d;
                div_remainder = n % d;
                break;
            }
            case DivMode::Div64_64: {
                const int64_t n = div_numer;
                const int64_t d = div_denom;
                if (d == 0) {
                    div_by_zero = true;
                    div_result = SaturatedResult(n < 0, DivMode::Div64_64);
                    div_remainder = n;
                    break;
                }

                if (n == std::numeric_limits<int64_t>::min() && d == -1) {
                    div_result = std::numeric_limits<int64_t>::min();
                    div_remainder = 0;
                    break;
                }

                div_result = n / d;
                div_remainder = n % d;
                break;
            }
        }
    }

    // Performs the square root using the currently configured mode.
    void ComputeSqrt() {
        uint64_t input = 0;
        switch (sqrt_mode) {
            case SqrtMode::Sqrt32:
                input = (sqrt_param & 0xFFFFFFFFull);
                break;
            case SqrtMode::Sqrt64:
                input = sqrt_param;
                break;
        }
        sqrt_result = static_cast<uint32_t>(IntegerSqrt(input));
    }

    // ---- Legacy interface for backward compatibility ----
    uint64_t div(uint64_t n, uint32_t d) {
        if (d == 0) return 0;
        return n / d;
    }

    uint32_t sq(uint64_t v) {
        return static_cast<uint32_t>(std::sqrt(static_cast<double>(v)));
    }

    // ---- DIVCNT register read (0x04000280) ----
    uint32_t ReadDIVCNT() const {
        uint32_t val = static_cast<uint32_t>(div_mode);
        if (div_by_zero) val |= (1u << 14);
        // Busy flag (bit 15) = 0 since we compute instantly
        return val;
    }

    void WriteDIVCNT(uint32_t value) {
        uint32_t mode = value & 0x3;
        if (mode > 2) mode = 2;
        div_mode = static_cast<DivMode>(mode);
        ComputeDivision(); // Re-compute on mode change
    }

    // ---- SQRTCNT register read (0x040002B0) ----
    uint32_t ReadSQRTCNT() const {
        return static_cast<uint32_t>(sqrt_mode);
        // Busy flag (bit 15) = 0
    }

    void WriteSQRTCNT(uint32_t value) {
        sqrt_mode = static_cast<SqrtMode>(value & 0x1);
        ComputeSqrt();
    }
};

void ComputeDiv64();
void ComputeDiv32();
void ComputeSqrt64();
void ComputeSqrt32();
void HandleMathExceptions();
