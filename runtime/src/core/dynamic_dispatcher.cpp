#include "dynamic_dispatcher.h"
#include "hw_overlay.h"
#include "cpu_context.h"
#include "memory_map.h"

namespace Core::Dispatcher {

    OverlayManager* g_overlay_manager = nullptr;

    void Init() {
        if (!g_overlay_manager) {
            g_overlay_manager = new OverlayManager();
        }
    }

    void ExecuteDynamicBranch(uint32_t target_address, CPU_Context* ctx) {
        if (!g_overlay_manager) return;
        g_overlay_manager->ExecuteDynamicBranch(ctx, target_address);
    }

    void InvalidateOverlayCache() {
        if (!g_overlay_manager) return;
        std::vector<uint8_t> dummy_ram;
        g_overlay_manager->InvalidateOverlayCache(dummy_ram);
    }

    void LoadOverlay(int overlay_id) {
        if (!g_overlay_manager) return;

        if (overlay_id >= g_overlay_manager->table.size()) {
            g_overlay_manager->table.resize(overlay_id + 1);
        }

        g_overlay_manager->SetActiveOverlay(overlay_id);
    }

    void UnloadOverlay(int overlay_id) {
        if (!g_overlay_manager) return;

        if (overlay_id < g_overlay_manager->table.size()) {
            uint32_t load_addr = g_overlay_manager->table[overlay_id].ram_address;
            g_overlay_manager->active_overlays.erase(load_addr);
        }
    }
}
