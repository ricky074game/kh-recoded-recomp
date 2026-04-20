
#pragma once
#include "cpu_context.h"
#include <cstdio>
#include <atomic>

#ifndef ENABLE_DS_TRACING
#define ENABLE_DS_TRACING 1
#endif

extern thread_local uint32_t g_deb_last_addr;
extern thread_local uint32_t g_deb_branch_target;
extern thread_local uint32_t g_deb_loop_count;
extern std::atomic<bool> g_superdebug_enabled;

#if ENABLE_DS_TRACING
    #ifdef EXTREME_DEBUG
        #define DS_ADDR(x) \
            ctx->trace_buffer[ctx->trace_idx++] = (x); \
            { \
                if ((x) < g_deb_last_addr && g_deb_last_addr - (x) < 0x100) { \
                    if (g_deb_branch_target == 0) g_deb_branch_target = (x); \
                    if ((x) == g_deb_branch_target) g_deb_loop_count++; \
                } else if ((x) > g_deb_branch_target + 0x100 || (x) < g_deb_branch_target) { \
                    g_deb_loop_count = 0; g_deb_branch_target = 0; \
                } \
                g_deb_last_addr = (x); \
                if (g_superdebug_enabled.load(std::memory_order_relaxed)) { \
                    printf("[TRACE] EXEC 0x%08X\n", (x)); \
                } \
            }
    #else
        #define DS_ADDR(x) ctx->trace_buffer[ctx->trace_idx++] = (x); 
    #endif
#else
    #define DS_ADDR(x) 
#endif
