#include "hw_math.h"

// Expose a global engine if needed by dynamic dispatchers calling standalone functions
HWMathEngine g_math_engine;

void ComputeDiv64() {
    g_math_engine.div_mode = DivMode::Div64_64;
    g_math_engine.ComputeDivision();
}

void ComputeDiv32() {
    g_math_engine.div_mode = DivMode::Div32_32;
    g_math_engine.ComputeDivision();
}

void ComputeSqrt64() {
    g_math_engine.sqrt_mode = SqrtMode::Sqrt64;
    g_math_engine.ComputeSqrt();
}

void ComputeSqrt32() {
    g_math_engine.sqrt_mode = SqrtMode::Sqrt32;
    g_math_engine.ComputeSqrt();
}

void HandleMathExceptions() {
    // The DS sets status flags in DIVCNT instead of throwing software exceptions.
    // Keep this compatibility hook as a no-op.
}
