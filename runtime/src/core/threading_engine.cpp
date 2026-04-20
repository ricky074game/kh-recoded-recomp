#include "threading_engine.h"
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <condition_variable>

namespace Core::Threading {
    std::thread arm9_t;
    std::thread arm7_t;
    std::thread master_t;

    std::atomic<bool> is_running{false};
    std::atomic<uint64_t> sync_epoch{0};

    std::mutex sync_mutex;
    std::condition_variable sync_cv;

    void MasterTimingLoop() {
        while (is_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));

            {
                std::lock_guard<std::mutex> lock(sync_mutex);
                ++sync_epoch;
            }
            sync_cv.notify_all();
        }
    }

    void ARM9_Thread() {
        uint64_t local_epoch = 0;
        while (is_running) {
            std::unique_lock<std::mutex> lock(sync_mutex);
            sync_cv.wait(lock, [&]() {
                return !is_running || sync_epoch.load(std::memory_order_acquire) != local_epoch;
            });
            if (!is_running) {
                break;
            }
            local_epoch = sync_epoch.load(std::memory_order_acquire);
        }
    }

    void ARM7_Thread() {
        uint64_t local_epoch = 0;
        while (is_running) {
            std::unique_lock<std::mutex> lock(sync_mutex);
            sync_cv.wait(lock, [&]() {
                return !is_running || sync_epoch.load(std::memory_order_acquire) != local_epoch;
            });
            if (!is_running) {
                break;
            }
            local_epoch = sync_epoch.load(std::memory_order_acquire);
        }
    }

    void InitCores() {
        if (is_running) return;
        is_running = true;
        sync_epoch.store(0, std::memory_order_release);

        arm9_t = std::thread(ARM9_Thread);
        arm7_t = std::thread(ARM7_Thread);
        master_t = std::thread(MasterTimingLoop);
    }

    void ShutdownCores() {
        is_running = false;

        sync_cv.notify_all();

        if (arm9_t.joinable()) arm9_t.join();
        if (arm7_t.joinable()) arm7_t.join();
        if (master_t.joinable()) master_t.join();
    }

    void SyncThreads() {
        {
            std::lock_guard<std::mutex> lock(sync_mutex);
            ++sync_epoch;
        }
        sync_cv.notify_all();
    }
}
