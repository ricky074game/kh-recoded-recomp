// ============================================================================
// hook_dispatcher.cpp — Runtime Hook Dispatch Logic
//
// This module provides the function that the generated code calls before
// and after each basic block, allowing mods to intercept execution.
// ============================================================================

#include "recomp_api.h"

// Dispatches hooks for a given ROM address block.
// Returns true if the original block should execute, false to skip it.
bool DispatchHooks(uint32_t rom_address, CPU_Context* ctx) {
    auto& registry = HookRegistry::Get();

    // Run pre-hooks — if any returns false, skip the original block.
    if (!registry.RunPreHooks(rom_address, ctx)) {
        return false;
    }

    // The caller executes the original block here...

    // Run post-hooks after the block completes.
    // (This is called separately by the generated code after the block.)
    return true;
}

void DispatchPostHooks(uint32_t rom_address, CPU_Context* ctx) {
    HookRegistry::Get().RunPostHooks(rom_address, ctx);
}
