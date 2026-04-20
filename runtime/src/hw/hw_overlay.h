#pragma once

// ============================================================================
// hw_overlay.h — Nintendo DS ARM9 Overlay Management & Dispatch
//
// Nintendo DS games swap blocks of code (overlays) in and out of the limited
// 4MB Main RAM during runtime. The game handles this by downloading the code
// via filesystem into RAM and then flushing the CPU caches.
//
// This module provides:
// 1. OverlayTable: Parses y9.bin to map Overlay IDs to metadata (RAM addr, size).
// 2. OverlayManager: Tracks which overlays are currently resident in RAM.
// 3. DynamicDispatcher: A function pointer table that routes BX/BLX target
//                       addresses to the correct lifted C++ overlay blocks.
// ============================================================================

#include <vector>
#include <string>
#include <cstdint>
#include <unordered_map>
#include <memory>

class CPU_Context;

// ============================================================================
// Overlay Metadata
// ============================================================================
struct OverlayEntry {
    uint32_t overlay_id;
    uint32_t ram_address;
    uint32_t ram_size;
    uint32_t bss_size;
    uint32_t static_init_start;
    uint32_t static_init_end;
    uint32_t file_id;
    uint32_t compressed;
};

// ============================================================================
// Function Pointer Types
// ============================================================================
// Signatures for lifted C++ functions. They all take CPU_Context*.
typedef void (*LiftedFunc)(CPU_Context*);

// ============================================================================
// Overlay Manager
// ============================================================================
class OverlayManager {
public:
    // Global table parsed from y9.bin
    std::vector<OverlayEntry> table;

    // Active overlays mapped from loaded RAM base address -> Overlay ID
    std::unordered_map<uint32_t, uint32_t> active_overlays;

    // Lifted function tables (Target Address -> Lifted C++ Function)
    // static_funcs maps arm9.bin
    // overlay_funcs maps [OverlayID][TargetAddress]
    std::unordered_map<uint32_t, LiftedFunc> static_funcs;
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, LiftedFunc>> overlay_funcs;

    // Load y9.bin
    bool LoadY9(const std::string& y9_path);
    bool LoadY9FromBuffer(const std::vector<uint8_t>& data);

    // Register lifted functions
    void RegisterStaticFunction(uint32_t address, LiftedFunc func);
    void RegisterOverlayFunction(uint32_t overlay_id, uint32_t address, LiftedFunc func);

    // Dynamic Execution Router
    void ExecuteDynamicBranch(CPU_Context* ctx, uint32_t target_address);

    // Cache Invalidation
    // Called when the game executes 'MCR p15, 0, Rd, c7, c5, 0'.
    // Scans memory or utilizes heuristics to detect newly swapped overlays.
    void InvalidateOverlayCache(const std::vector<uint8_t>& main_ram);
    
    // Explicitly set an active overlay (useful for tests/forced loading)
    void SetActiveOverlay(uint32_t overlay_id);
    bool IsOverlayActive(uint32_t overlay_id) const;
};
