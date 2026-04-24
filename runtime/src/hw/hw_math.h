#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

enum class DivMode : uint8_t {
    Div32_32 = 0,
    Div64_32 = 1,
    Div64_64 = 2
};

enum class SqrtMode : uint8_t {
    Sqrt32 = 0,
    Sqrt64 = 1
};

class HWMathEngine {
public:
    DivMode div_mode = DivMode::Div32_32;
    int64_t div_numer = 0;
    int64_t div_denom = 0;
    int64_t div_result = 0;
    int64_t div_remainder = 0;
    bool div_by_zero = false;
    uint32_t div_busy_cycles = 0;

    SqrtMode sqrt_mode = SqrtMode::Sqrt32;
    uint64_t sqrt_param = 0;
    uint32_t sqrt_result = 0;
    uint32_t sqrt_busy_cycles = 0;

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

    void Step(uint32_t cycles) {
        if (div_busy_cycles > cycles) {
            div_busy_cycles -= cycles;
        } else {
            div_busy_cycles = 0;
        }

        if (sqrt_busy_cycles > cycles) {
            sqrt_busy_cycles -= cycles;
        } else {
            sqrt_busy_cycles = 0;
        }
    }

    void StartDivision() {
        ComputeDivision();
        div_busy_cycles = (div_mode == DivMode::Div32_32) ? 18u : 34u;
    }

    void StartSqrt() {
        ComputeSqrt();
        sqrt_busy_cycles = 13u;
    }

    void ComputeDivision() {
        const bool div0_flag = (div_denom == 0);
        div_by_zero = div0_flag;

        switch (div_mode) {
            case DivMode::Div32_32: {
                const int32_t numer = static_cast<int32_t>(div_numer);
                const int32_t denom = static_cast<int32_t>(div_denom);

                if (denom == 0) {
                    div_result = (numer < 0) ? 1 : -1;
                    div_remainder = numer;
                    break;
                }
                if (numer == std::numeric_limits<int32_t>::min() && denom == -1) {
                    div_result = std::numeric_limits<int32_t>::min();
                    div_remainder = 0;
                    break;
                }

                div_result = static_cast<int64_t>(numer / denom);
                div_remainder = static_cast<int64_t>(numer % denom);
                break;
            }

            case DivMode::Div64_32: {
                const int64_t numer = div_numer;
                const int32_t denom32 = static_cast<int32_t>(div_denom);

                if (denom32 == 0) {
                    div_result = (numer < 0) ? 1 : -1;
                    div_remainder = numer;
                    break;
                }
                if (numer == std::numeric_limits<int64_t>::min() && denom32 == -1) {
                    div_result = std::numeric_limits<int64_t>::min();
                    div_remainder = 0;
                    break;
                }

                const int64_t denom = static_cast<int64_t>(denom32);
                div_result = numer / denom;
                div_remainder = numer % denom;
                break;
            }

            case DivMode::Div64_64: {
                const int64_t numer = div_numer;
                const int64_t denom = div_denom;

                if (denom == 0) {
                    div_result = (numer < 0) ? 1 : -1;
                    div_remainder = numer;
                    break;
                }
                if (numer == std::numeric_limits<int64_t>::min() && denom == -1) {
                    div_result = std::numeric_limits<int64_t>::min();
                    div_remainder = 0;
                    break;
                }

                div_result = numer / denom;
                div_remainder = numer % denom;
                break;
            }
        }
    }

    void ComputeSqrt() {
        const uint64_t input =
            (sqrt_mode == SqrtMode::Sqrt32) ? (sqrt_param & 0xFFFFFFFFull) : sqrt_param;
        sqrt_result = static_cast<uint32_t>(IntegerSqrt(input));
    }

    uint64_t div(uint64_t n, uint32_t d) {
        return (d == 0) ? 0 : (n / d);
    }

    uint32_t sq(uint64_t v) {
        return static_cast<uint32_t>(std::sqrt(static_cast<double>(v)));
    }

    uint32_t ReadDIVCNT() const {
        uint32_t value = static_cast<uint32_t>(div_mode) & 0x3u;
        if (div_by_zero) {
            value |= (1u << 14);
        }
        if (div_busy_cycles != 0) {
            value |= (1u << 15);
        }
        return value;
    }

    void WriteDIVCNT(uint32_t value) {
        uint32_t mode = value & 0x3u;
        if (mode == 3u) {
            mode = 1u;
        }
        div_mode = static_cast<DivMode>(mode);
        StartDivision();
    }

    uint32_t ReadSQRTCNT() const {
        uint32_t value = static_cast<uint32_t>(sqrt_mode) & 0x1u;
        if (sqrt_busy_cycles != 0) {
            value |= (1u << 15);
        }
        return value;
    }

    void WriteSQRTCNT(uint32_t value) {
        sqrt_mode = static_cast<SqrtMode>(value & 0x1u);
        StartSqrt();
    }
};

void ComputeDiv64();
void ComputeDiv32();
void ComputeSqrt64();
void ComputeSqrt32();
void HandleMathExceptions();
