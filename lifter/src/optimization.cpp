#include "optimization.h"

// ============================================================================
// optimization.cpp — Deferred Flag Calculation Implementation
//
// The core flag logic lives in the header as inline functions for maximum
// performance in emitted code. This compilation unit ensures the header
// is compiled and linked correctly, and can house any future non-inline
// flag optimization utilities (e.g., flag dependency analysis for the lifter).
// ============================================================================

// Currently, all implementations are inline in optimization.h.
// This file exists to ensure proper linkage in the build system.
