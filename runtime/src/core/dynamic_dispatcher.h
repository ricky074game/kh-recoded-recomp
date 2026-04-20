#pragma once
#include <cstdint>

class CPU_Context;

namespace Core::Dispatcher {
    void Init();
    void ExecuteDynamicBranch(uint32_t target_address, CPU_Context* ctx);
    void InvalidateOverlayCache();
    void LoadOverlay(int overlay_id);
    void UnloadOverlay(int overlay_id);
}
