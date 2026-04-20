#include <gtest/gtest.h>
#include "threading_engine.h"
#include <thread>
#include <chrono>

// The threading engine is highly stateful, so we mock and test the API without locking up the test suite.

TEST(ThreadingEngineTest, InitShutdown) {
    EXPECT_NO_THROW(Core::Threading::InitCores());

    // Give them a brief moment to start
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_NO_THROW(Core::Threading::ShutdownCores());
}

TEST(ThreadingEngineTest, SyncThreadsNoThrow) {
    // Calling sync should not throw, even if not fully initialized or if called manually for testing
    EXPECT_NO_THROW(Core::Threading::SyncThreads());
}
