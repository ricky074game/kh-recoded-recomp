#include <gtest/gtest.h>
#include "dynamic_dispatcher.h"
#include "hw_overlay.h"
#include "cpu_context.h"

// Note: Core::Dispatcher operates globally, but we'll mock interactions to test bounds and behavior
// Since it's a global namespace, we verify it doesn't crash and modifies state appropriately.

namespace Core::Dispatcher {
    extern OverlayManager* g_overlay_manager;
}

class DynamicDispatcherTest : public ::testing::Test {
protected:
    void SetUp() override {
        Core::Dispatcher::Init();
    }
};

TEST_F(DynamicDispatcherTest, InitCreatesManager) {
    EXPECT_NE(Core::Dispatcher::g_overlay_manager, nullptr);
}

TEST_F(DynamicDispatcherTest, LoadUnloadOverlay) {
    // Should not crash, and should set internal active overlays
    EXPECT_NO_THROW(Core::Dispatcher::LoadOverlay(5));
    EXPECT_TRUE(Core::Dispatcher::g_overlay_manager->IsOverlayActive(5));

    EXPECT_NO_THROW(Core::Dispatcher::UnloadOverlay(5));
    EXPECT_FALSE(Core::Dispatcher::g_overlay_manager->IsOverlayActive(5));
}

TEST_F(DynamicDispatcherTest, InvalidateOverlayCache) {
    // Calling invalidate doesn't throw
    EXPECT_NO_THROW(Core::Dispatcher::InvalidateOverlayCache());
}

TEST_F(DynamicDispatcherTest, ExecuteDynamicBranch_Unmapped) {
    // Route execution to an invalid location
    CPU_Context ctx;
    ctx.r[15] = 0x02FFFFFF; // Random address
    // The test in hw_overlay shows it throws a std::runtime_error on unmapped dynamic dispatch
    EXPECT_THROW(Core::Dispatcher::ExecuteDynamicBranch(0x02FFFFFF, &ctx), std::runtime_error);
}
