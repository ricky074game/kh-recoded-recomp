#include <gtest/gtest.h>
#include <atomic>
#include "hw_overlay.h"
#include "cpu_context.h"

// Stubs for lifter functions
static bool func1_called = false;
static bool func2_called = false;
static bool func3_called = false;
static uint32_t last_dispatch_pc = 0;
static uint32_t last_entry_r15 = 0;

static void DummyStaticBlock(CPU_Context* ctx) { func1_called = true; }
static void DummyOverlayBlock(CPU_Context* ctx) { func2_called = true; }
static void DummyOverlayBlock2(CPU_Context* ctx) { func3_called = true; }
static void DummyDispatchCapture(CPU_Context* ctx) {
    func1_called = true;
    last_dispatch_pc = ctx->dispatch_pc;
    last_entry_r15 = ctx->r[15];
}

class OverlayTest : public ::testing::Test {
protected:
    void SetUp() override {
        func1_called = false;
        func2_called = false;
        func3_called = false;
        last_dispatch_pc = 0;
        last_entry_r15 = 0;
    }
};

// ============================================================================
// Parsing
// ============================================================================
TEST_F(OverlayTest, ParseY9Buffer) {
    OverlayManager mgr;
    
    // Create a dummy y9.bin buffer with 2 entries (64 bytes total)
    std::vector<uint8_t> buffer(64, 0);
    
    // Entry 0: ID=0, RAM=0x020613E0, RAM_Size=0x2620, FileID=0
    uint32_t overlay0_ram = 0x020613E0;
    uint32_t overlay0_size = 0x2620;
    std::memcpy(&buffer[4], &overlay0_ram, 4);
    std::memcpy(&buffer[8], &overlay0_size, 4);
    
    // Entry 1: ID=1, RAM=0x0209E604, RAM_Size=0x1000, FileID=1
    uint32_t overlay1_id = 1;
    uint32_t overlay1_ram = 0x0209E604;
    uint32_t overlay1_size = 0x1000;
    uint32_t overlay1_file = 1;
    std::memcpy(&buffer[32], &overlay1_id, 4);
    std::memcpy(&buffer[36], &overlay1_ram, 4);
    std::memcpy(&buffer[40], &overlay1_size, 4);
    std::memcpy(&buffer[56], &overlay1_file, 4);

    EXPECT_TRUE(mgr.LoadY9FromBuffer(buffer));
    EXPECT_EQ(mgr.table.size(), 2u);
    
    EXPECT_EQ(mgr.table[0].overlay_id, 0u);
    EXPECT_EQ(mgr.table[0].ram_address, 0x020613E0u);
    EXPECT_EQ(mgr.table[0].ram_size, 0x2620u);

    EXPECT_EQ(mgr.table[1].overlay_id, 1u);
    EXPECT_EQ(mgr.table[1].ram_address, 0x0209E604u);
    EXPECT_EQ(mgr.table[1].ram_size, 0x1000u);
    EXPECT_EQ(mgr.table[1].file_id, 1u);
}

// ============================================================================
// Dynamic Dispatch
// ============================================================================
TEST_F(OverlayTest, DispatchStaticFunction) {
    OverlayManager mgr;
    mgr.RegisterStaticFunction(0x02001000, DummyStaticBlock);
    
    CPU_Context ctx;
    mgr.ExecuteDynamicBranch(&ctx, 0x02001000);
    EXPECT_TRUE(func1_called);
}

TEST_F(OverlayTest, DispatchClearsThumbBit) {
    OverlayManager mgr;
    mgr.RegisterStaticFunction(0x02001000, DummyStaticBlock);
    
    CPU_Context ctx;
    mgr.ExecuteDynamicBranch(&ctx, 0x02001001); // Target with THUMB bit set
    EXPECT_TRUE(func1_called);                  // Should strip the 1 and route correctly
}

TEST_F(OverlayTest, DispatchRoutesViaDispatchPC) {
    OverlayManager mgr;
    mgr.RegisterStaticFunction(0x02001000, DummyDispatchCapture);

    CPU_Context ctx;
    mgr.ExecuteDynamicBranch(&ctx, 0x02001001); // Target with THUMB bit set

    EXPECT_TRUE(func1_called);
    EXPECT_EQ(last_dispatch_pc, 0x02001000u);
    EXPECT_EQ(last_entry_r15, 0u);
}

TEST_F(OverlayTest, DispatchStopsWhenRuntimeStopped) {
    OverlayManager mgr;

    CPU_Context ctx;
    std::atomic<bool> running{false};
    ctx.running_flag = &running;

    EXPECT_NO_THROW(mgr.ExecuteDynamicBranch(&ctx, 0x02001000));
    EXPECT_FALSE(func1_called);
    EXPECT_FALSE(func2_called);
    EXPECT_FALSE(func3_called);
}

TEST_F(OverlayTest, DispatchOverlayFunction_Active) {
    OverlayManager mgr;
    
    // Mock table: Overlay 5 loads at 0x02060000, size 0x1000
    OverlayEntry entry = {};
    entry.overlay_id = 5;
    entry.ram_address = 0x02060000;
    entry.ram_size = 0x1000;
    
    // Size must be exactly 6 so ID=5 is within bounds
    mgr.table.resize(6);
    mgr.table[5] = entry;

    mgr.SetActiveOverlay(5);
    EXPECT_TRUE(mgr.IsOverlayActive(5));

    // Register an overlay func
    mgr.RegisterOverlayFunction(5, 0x02060500, DummyOverlayBlock);

    CPU_Context ctx;
    mgr.ExecuteDynamicBranch(&ctx, 0x02060500);
    EXPECT_TRUE(func2_called);
}

TEST_F(OverlayTest, DispatchOverlayFunction_InactiveThrows) {
    OverlayManager mgr;
    
    OverlayEntry entry = {};
    entry.overlay_id = 5;
    entry.ram_address = 0x02060000;
    entry.ram_size = 0x1000;
    
    mgr.table.resize(6);
    mgr.table[5] = entry;

    // We do NOT set it as active
    EXPECT_FALSE(mgr.IsOverlayActive(5));
    mgr.RegisterOverlayFunction(5, 0x02060500, DummyOverlayBlock);

    CPU_Context ctx;
    EXPECT_THROW(mgr.ExecuteDynamicBranch(&ctx, 0x02060500), std::runtime_error);
    EXPECT_FALSE(func2_called);
}

TEST_F(OverlayTest, DispatchOverlayOverlap) {
    OverlayManager mgr;
    
    // Overlay 1 and Overlay 2 load to the exact same RAM region (0x02060000)
    OverlayEntry e1 = {1, 0x02060000, 0x1000, 0, 0, 0, 1, 0};
    OverlayEntry e2 = {2, 0x02060000, 0x1000, 0, 0, 0, 2, 0};
    mgr.table.push_back({0, 0, 0, 0, 0, 0, 0, 0}); // Dummy 0
    mgr.table.push_back(e1);
    mgr.table.push_back(e2);

    // Both have a function at 0x02060100
    mgr.RegisterOverlayFunction(1, 0x02060100, DummyOverlayBlock);
    mgr.RegisterOverlayFunction(2, 0x02060100, DummyOverlayBlock2);

    // Make Overlay 1 active, call should route to func1
    mgr.SetActiveOverlay(1);
    CPU_Context ctx;
    mgr.ExecuteDynamicBranch(&ctx, 0x02060100);
    EXPECT_TRUE(func2_called);
    EXPECT_FALSE(func3_called);

    func2_called = false;

    // Load Overlay 2 (overwrites Overlay 1 since they map to same ram_address)
    mgr.SetActiveOverlay(2);
    EXPECT_TRUE(mgr.IsOverlayActive(2));
    EXPECT_FALSE(mgr.IsOverlayActive(1));

    mgr.ExecuteDynamicBranch(&ctx, 0x02060100);
    EXPECT_FALSE(func2_called);
    EXPECT_TRUE(func3_called); // Routed to func2 instead
}
