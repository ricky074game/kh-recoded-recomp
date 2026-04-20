#include <gtest/gtest.h>
#include "cpu_context.h"
#include "ds_debug.h"

TEST(DSDebugTest, RingBufferCycle) {
    CPU_Context ctx;
    ctx.trace_idx = 0;
    
    // Fill buffer up to 255
    for (int i = 0; i < 256; ++i) {
        // Just manually set the state as the macro normally does
        ctx.trace_buffer[ctx.trace_idx++] = i;
    }
    
    EXPECT_EQ(ctx.trace_idx, 0); // 8-bit uint must wrap nicely
    EXPECT_EQ(ctx.trace_buffer[0], 0);
    EXPECT_EQ(ctx.trace_buffer[255], 255);
    
    // Next write overwrites index 0
    ctx.trace_buffer[ctx.trace_idx++] = 999;
    
    EXPECT_EQ(ctx.trace_idx, 1);
    EXPECT_EQ(ctx.trace_buffer[0], 999);
}
