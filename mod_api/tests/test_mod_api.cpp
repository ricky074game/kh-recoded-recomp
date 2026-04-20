#include <gtest/gtest.h>

#include <atomic>

#include "hook_registry.h"

namespace {
std::atomic<int> g_pre_hook_calls{0};
std::atomic<int> g_post_hook_calls{0};

void PreHookFn() {
    ++g_pre_hook_calls;
}

void PostHookFn() {
    ++g_post_hook_calls;
}
}

TEST(ModAPITest, HooksDispatchByAddress) {
    ModAPI::Init();
    ModAPI::ClearHooks();

    g_pre_hook_calls.store(0);
    g_post_hook_calls.store(0);

    ModAPI::RegisterHook(0x1234, reinterpret_cast<void*>(&PreHookFn));
    ModAPI::RegisterPostHook(0x1234, reinterpret_cast<void*>(&PostHookFn));

    EXPECT_TRUE(ModAPI::HasHook(0x1234));

    ModAPI::ExecutePreHook(0x1234);
    ModAPI::ExecutePostHook(0x1234);

    EXPECT_EQ(g_pre_hook_calls.load(), 1);
    EXPECT_EQ(g_post_hook_calls.load(), 1);
}

TEST(ModAPITest, MissingModPathDoesNotThrow) {
    EXPECT_NO_THROW(ModAPI::LoadModDLL("/tmp/kh_recoded_missing_mod.so"));
}
