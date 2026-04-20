#pragma once


#include <cstdint>
#include "cpu_context.h"

namespace FlagDeferral {


    inline bool Calc_N(uint32_t result) {
        return (result & 0x80000000) != 0;
    }

    inline bool Calc_Z(uint32_t result) {
        return result == 0;
    }

    inline bool Calc_C_ADD(uint32_t a, uint32_t b) {
        uint64_t wide = static_cast<uint64_t>(a) + static_cast<uint64_t>(b);
        return (wide >> 32) != 0;
    }

    inline bool Calc_C_SUB(uint32_t a, uint32_t b) {
        return a >= b;
    }

    inline bool Calc_C_Shift(uint32_t value, uint32_t shift_amount, bool is_left) {
        if (shift_amount == 0) return false;
        if (is_left) {
            return (value >> (32 - shift_amount)) & 1;
        } else {
            return (value >> (shift_amount - 1)) & 1;
        }
    }

    inline bool Calc_V_ADD(uint32_t a, uint32_t b, uint32_t result) {
        return ((~(a ^ b)) & (a ^ result) & 0x80000000) != 0;
    }

    inline bool Calc_V_SUB(uint32_t a, uint32_t b, uint32_t result) {
        return ((a ^ b) & (a ^ result) & 0x80000000) != 0;
    }

    inline bool Calc_C(uint32_t /*result*/) { return false; }

} // namespace FlagDeferral


inline void CalculateFlags_ADD(CPU_Context* ctx, uint32_t a, uint32_t b) {
    uint32_t result = a + b;
    uint32_t& cpsr = ctx->cpsr;

    cpsr &= ~(0xFu << 28);

    if (FlagDeferral::Calc_N(result))         cpsr |= (1u << 31); // N
    if (FlagDeferral::Calc_Z(result))         cpsr |= (1u << 30); // Z
    if (FlagDeferral::Calc_C_ADD(a, b))       cpsr |= (1u << 29); // C
    if (FlagDeferral::Calc_V_ADD(a, b, result)) cpsr |= (1u << 28); // V
}

inline void CalculateFlags_SUB(CPU_Context* ctx, uint32_t a, uint32_t b) {
    uint32_t result = a - b;
    uint32_t& cpsr = ctx->cpsr;

    cpsr &= ~(0xFu << 28);

    if (FlagDeferral::Calc_N(result))         cpsr |= (1u << 31);
    if (FlagDeferral::Calc_Z(result))         cpsr |= (1u << 30);
    if (FlagDeferral::Calc_C_SUB(a, b))       cpsr |= (1u << 29);
    if (FlagDeferral::Calc_V_SUB(a, b, result)) cpsr |= (1u << 28);
}

inline void CalculateFlags_MOV(CPU_Context* ctx, uint32_t result) {
    uint32_t& cpsr = ctx->cpsr;

    cpsr &= ~((1u << 31) | (1u << 30));

    if (FlagDeferral::Calc_N(result)) cpsr |= (1u << 31);
    if (FlagDeferral::Calc_Z(result)) cpsr |= (1u << 30);
}

inline bool CheckConditionCode(CPU_Context* ctx, int cc) {
    uint32_t cpsr = ctx->cpsr;
    bool N = (cpsr >> 31) & 1;
    bool Z = (cpsr >> 30) & 1;
    bool C = (cpsr >> 29) & 1;
    bool V = (cpsr >> 28) & 1;

    switch (cc) {
        case 1:  return Z;          // EQ — Equal
        case 2:  return !Z;         // NE — Not equal
        case 3:  return C;          // CS/HS — Carry set / Unsigned higher or same
        case 4:  return !C;         // CC/LO — Carry clear / Unsigned lower
        case 5:  return N;          // MI — Minus / Negative
        case 6:  return !N;         // PL — Plus / Positive or zero
        case 7:  return V;          // VS — Overflow set
        case 8:  return !V;         // VC — Overflow clear
        case 9:  return C && !Z;    // HI — Unsigned higher
        case 10: return !C || Z;    // LS — Unsigned lower or same
        case 11: return N == V;     // GE — Signed greater than or equal
        case 12: return N != V;     // LT — Signed less than
        case 13: return !Z && (N == V); // GT — Signed greater than
        case 14: return Z || (N != V);  // LE — Signed less than or equal
        case 15: return true;       // AL — Always
        default: return true;       // Unconditional / reserved
    }
}
