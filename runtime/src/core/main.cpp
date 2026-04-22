#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>

#include "cpu_context.h"
#include "ds_debug.h"
#include "hw_audio.h"
#include "hw_input.h"
#include "hw_irq.h"
#include "hw_overlay.h"
#include "memory_map.h"
#include "runtime_qt_frontend.h"
#include "sw_renderer.h"
#include "vfs.h"

extern std::atomic<uint64_t> g_debug_arm9_dispatch_count;
extern std::atomic<uint32_t> g_debug_arm9_last_dispatch_addr;
extern std::atomic<uint32_t> g_debug_arm9_same_dispatch_addr_count;

extern std::atomic<uint64_t> g_debug_gx_swap_count;
extern std::atomic<uint32_t> g_debug_gx_last_vertex_count;
extern std::atomic<bool> g_superdebug_enabled;
extern std::atomic<uint32_t> g_debug_gx_last_polygon_count;

extern std::atomic<uint64_t> g_debug_2d_submit_count;
extern std::atomic<uint32_t> g_debug_2d_last_sprite_count;

static std::atomic<bool> g_running{true};
static NDSMemory g_memory;
static std::atomic<CPU_Context*> g_arm9_ctx{nullptr};
static std::atomic<uint64_t> g_vblank_count{0};
static std::atomic<uint64_t> g_ui_present_count{0};

struct DebugWatchdogConfig {
    bool enabled = false;
    int poll_ms = 500;
    int stall_ms = 3000;
    int heartbeat_ms = 0;
    int same_dispatch_warn_count = 10000;
};

static DebugWatchdogConfig g_watchdog_cfg;

enum class LogLevel { Info, Warning, Error };

static void Log(LogLevel level, const std::string& msg) {
    const char* prefix = "[INFO]";
    if (level == LogLevel::Warning) prefix = "[WARN]";
    if (level == LogLevel::Error) prefix = "[ERR ]";
    std::cout << prefix << " " << msg << std::endl;
}

static std::string Hex32(uint32_t value) {
    std::ostringstream oss;
    oss << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
    return oss.str();
}

static int ReadEnvInt(const char* name, int fallback, int min_val, int max_val) {
    const char* value = std::getenv(name);
    if (!value || *value == '\0') {
        return fallback;
    }

    char* end = nullptr;
    long parsed = std::strtol(value, &end, 10);
    if (end == value || (end && *end != '\0')) {
        return fallback;
    }

    if (parsed < min_val) parsed = min_val;
    if (parsed > max_val) parsed = max_val;
    return static_cast<int>(parsed);
}

static bool ReadEnvBool(const char* name, bool fallback) {
    const char* value = std::getenv(name);
    if (!value || *value == '\0') {
        return fallback;
    }

    std::string lower(value);
    std::transform(lower.begin(),
                   lower.end(),
                   lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (lower == "1" || lower == "true" || lower == "yes" || lower == "on") {
        return true;
    }
    if (lower == "0" || lower == "false" || lower == "no" || lower == "off") {
        return false;
    }
    return fallback;
}

static bool ContainsSnapPath(const char* value) {
    return value && std::strstr(value, "/snap/") != nullptr;
}

static void SanitizeSnapRuntimeEnv() {
    const bool running_under_snap =
        std::getenv("SNAP") != nullptr || std::getenv("SNAP_NAME") != nullptr;

    const char* ld_library_path = std::getenv("LD_LIBRARY_PATH");
    const char* ld_preload = std::getenv("LD_PRELOAD");

    bool changed = false;
    if (ContainsSnapPath(ld_library_path) || running_under_snap) {
        unsetenv("LD_LIBRARY_PATH");
        changed = true;
    }
    if (ContainsSnapPath(ld_preload)) {
        unsetenv("LD_PRELOAD");
        changed = true;
    }

    if (changed) {
        Log(LogLevel::Warning,
            "Sanitized Snap runtime linker environment to avoid host glibc conflicts.");
    }
}

static void LogRuntimeSnapshot(const std::string& reason, LogLevel level = LogLevel::Info) {
    CPU_Context* arm9_ctx = g_arm9_ctx.load(std::memory_order_acquire);
    const uint32_t last_dispatch_addr = g_debug_arm9_last_dispatch_addr.load(std::memory_order_relaxed);

    std::ostringstream oss;
    const uint32_t dispcnt_a = g_memory.GetEngine2DA().ReadRegister(0x00);
    const uint32_t dispcnt_b = g_memory.GetEngine2DB().ReadRegister(0x00);
    oss << reason
        << " | ARM9Ctx=" << (arm9_ctx ? "ready" : "null")
        << " dispatch=" << g_debug_arm9_dispatch_count.load(std::memory_order_relaxed)
        << " lastDispatch=" << Hex32(last_dispatch_addr)
        << " sameDispatch=" << g_debug_arm9_same_dispatch_addr_count.load(std::memory_order_relaxed)
        << " gxSwaps=" << g_debug_gx_swap_count.load(std::memory_order_relaxed)
        << " gxVerts=" << g_debug_gx_last_vertex_count.load(std::memory_order_relaxed)
        << " gxPolys=" << g_debug_gx_last_polygon_count.load(std::memory_order_relaxed)
        << " submit2D=" << g_debug_2d_submit_count.load(std::memory_order_relaxed)
        << " sprites2D=" << g_debug_2d_last_sprite_count.load(std::memory_order_relaxed)
        << " vblank=" << g_vblank_count.load(std::memory_order_relaxed)
        << " uiPresents=" << g_ui_present_count.load(std::memory_order_relaxed)
        << " ime9=" << Hex32(g_memory.irq_arm9.ime)
        << " ie9=" << Hex32(g_memory.irq_arm9.ie)
        << " if9=" << Hex32(g_memory.irq_arm9.if_reg)
        << " powcnt1=" << Hex32(g_memory.powcnt1)
        << " dispcntA=" << Hex32(dispcnt_a)
        << " dispcntB=" << Hex32(dispcnt_b);

    if (arm9_ctx) {
        const uint8_t trace_idx = arm9_ctx->trace_idx;
        const uint8_t trace_last = static_cast<uint8_t>((trace_idx - 1) & 0xFF);
        oss << " pc=" << Hex32(arm9_ctx->r[15])
            << " lr=" << Hex32(arm9_ctx->r[14])
            << " sp=" << Hex32(arm9_ctx->r[13])
            << " cpsr=" << Hex32(arm9_ctx->cpsr)
            << " mode=" << Hex32(arm9_ctx->GetMode())
            << " cp15c1=" << Hex32(arm9_ctx->cp15_control)
            << " cp15dtcm=" << Hex32(arm9_ctx->cp15_dtcm_base)
            << " cp15itcm=" << Hex32(arm9_ctx->cp15_itcm_base)
            << " lastTrace=" << Hex32(arm9_ctx->trace_buffer[trace_last]);
    }

    Log(level, oss.str());
}

static void RunDebugWatchdog() {
    using clock = std::chrono::steady_clock;

    uint64_t last_dispatch = g_debug_arm9_dispatch_count.load(std::memory_order_relaxed);
    auto last_progress = clock::now();
    auto last_heartbeat = clock::now();
    bool stalled_reported = false;
    bool tight_loop_reported = false;
    const uint32_t tight_loop_reset_threshold =
        static_cast<uint32_t>(std::max(100, g_watchdog_cfg.same_dispatch_warn_count / 8));

    while (g_running.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(g_watchdog_cfg.poll_ms));

        const auto now = clock::now();
        const uint64_t current_dispatch = g_debug_arm9_dispatch_count.load(std::memory_order_relaxed);

        if (current_dispatch != last_dispatch) {
            last_dispatch = current_dispatch;
            last_progress = now;
            if (stalled_reported) {
                LogRuntimeSnapshot("ARM9 dispatch resumed after stall");
                stalled_reported = false;
            }
        }

        if (g_watchdog_cfg.heartbeat_ms > 0) {
            const int heartbeat_elapsed_ms = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(now - last_heartbeat).count());
            if (heartbeat_elapsed_ms >= g_watchdog_cfg.heartbeat_ms) {
                LogRuntimeSnapshot("Debug heartbeat");
                last_heartbeat = now;
            }
        }

        const int stalled_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now - last_progress).count());
        if (!stalled_reported && stalled_ms >= g_watchdog_cfg.stall_ms) {
            std::ostringstream reason;
            reason << "ARM9 dispatch appears stalled for " << stalled_ms << "ms";
            LogRuntimeSnapshot(reason.str(), LogLevel::Warning);
            stalled_reported = true;
        }

        const uint32_t same_dispatch = g_debug_arm9_same_dispatch_addr_count.load(std::memory_order_relaxed);
        if (!tight_loop_reported &&
            same_dispatch >= static_cast<uint32_t>(g_watchdog_cfg.same_dispatch_warn_count)) {
            LogRuntimeSnapshot(
                "ARM9 is repeatedly dispatching the same address (possible tight loop before first frame)",
                LogLevel::Warning);
            tight_loop_reported = true;
        } else if (tight_loop_reported && same_dispatch <= tight_loop_reset_threshold) {
            LogRuntimeSnapshot("ARM9 tight-loop condition cleared");
            tight_loop_reported = false;
        }
    }

    Log(LogLevel::Info, "Debug watchdog thread exiting.");
}

static void PrintTrace(CPU_Context* ctx) {
    if (!ctx) return;
    std::cout << "Execution Trace (Ring Buffer, last 10):\n";
    uint8_t curr = ctx->trace_idx;
    for (int i = 0; i < 10; ++i) {
        uint8_t idx = (curr - 1 - i) % 256;
        uint32_t addr = ctx->trace_buffer[idx];
        if (addr == 0 && i > 0) break;
        if (i == 0) {
            std::cout << " -> 0x" << std::hex << std::setw(8) << std::setfill('0') << addr
                      << " (Instruction that crashed)\n";
        } else {
            std::cout << "    0x" << std::hex << std::setw(8) << std::setfill('0') << addr << "\n";
        }
    }
}

static void CrashHandler(int signum) {
    std::cout << "\n==================================================\n";
    std::cout << "  FATAL ERROR: Signal " << signum << " caught!\n";
    std::cout << "==================================================\n";
    PrintTrace(g_arm9_ctx.load(std::memory_order_acquire));
    std::cout << "Dump complete. Exiting.\n";
    std::exit(signum);
}

static void RunARM9() {
    Log(LogLevel::Info, "ARM9 thread started.");

    g_debug_arm9_dispatch_count.store(0, std::memory_order_relaxed);
    g_debug_arm9_last_dispatch_addr.store(0, std::memory_order_relaxed);
    g_debug_arm9_same_dispatch_addr_count.store(0, std::memory_order_relaxed);
    g_debug_gx_swap_count.store(0, std::memory_order_relaxed);
    g_debug_gx_last_vertex_count.store(0, std::memory_order_relaxed);
    g_debug_gx_last_polygon_count.store(0, std::memory_order_relaxed);
    g_debug_2d_submit_count.store(0, std::memory_order_relaxed);
    g_debug_2d_last_sprite_count.store(0, std::memory_order_relaxed);

    CPU_Context ctx;
    ctx.InitializeNDS9();
    ctx.mem = &g_memory;
    ctx.running_flag = &g_running;
    g_arm9_ctx.store(&ctx, std::memory_order_release);

    std::ifstream arm9_file("recoded/arm9.bin", std::ios::binary);
    if (arm9_file.is_open()) {
        arm9_file.read(reinterpret_cast<char*>(g_memory.GetMainRAM()), g_memory.GetMainRAMSize());
        Log(LogLevel::Info, "ARM9 binary loaded into Main RAM.");
    } else {
        Log(LogLevel::Error, "Failed to load recoded/arm9.bin!");
    }

    if (g_memory.GetOverlayManager().LoadY9("recoded/y9.bin")) {
        Log(LogLevel::Info, "Overlay table (y9.bin) loaded.");
    }

    {
        const auto& overlay_mgr = g_memory.GetOverlayManager();
        std::unordered_set<uintptr_t> unique_static_handlers;
        unique_static_handlers.reserve(overlay_mgr.static_funcs.size());
        for (const auto& entry : overlay_mgr.static_funcs) {
            unique_static_handlers.insert(reinterpret_cast<uintptr_t>(entry.second));
        }

        std::ostringstream reg_stats;
        reg_stats << "Lifted function registry: static=" << overlay_mgr.static_funcs.size()
                  << ", overlays=" << overlay_mgr.overlay_funcs.size()
                  << ", staticHandlers=" << unique_static_handlers.size();
        Log(LogLevel::Info, reg_stats.str());

        if (overlay_mgr.static_funcs.size() >= 32 && unique_static_handlers.size() <= 2) {
            Log(LogLevel::Warning,
                "Most static addresses resolve to very few handler functions; this usually means chunk dispatch is broken in generated output.");
        }
    }

    Log(LogLevel::Info, "Starting ARM9 execution at 0x02000800...");

    try {
        g_memory.GetOverlayManager().ExecuteDynamicBranch(&ctx, 0x02000800);

        if (g_running.load(std::memory_order_relaxed)) {
            LogRuntimeSnapshot("ARM9 dispatcher returned (execution halted)", LogLevel::Warning);
            g_running.store(false, std::memory_order_relaxed);
        }
    } catch (const std::exception& e) {
        Log(LogLevel::Error, std::string("ARM9 Crash: ") + e.what());
        LogRuntimeSnapshot("ARM9 exception snapshot", LogLevel::Error);
        PrintTrace(&ctx);
        g_running.store(false, std::memory_order_relaxed);
    }

    Log(LogLevel::Info, "ARM9 thread exiting.");
}

static void RunARM7() {
    Log(LogLevel::Info, "ARM7 thread started.");

    CPU_Context ctx;
    ctx.InitializeNDS7();
    ctx.mem = &g_memory;

    while (g_running) {
        CheckInterrupts(&ctx, g_memory.irq_arm7);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    Log(LogLevel::Info, "ARM7 thread exiting.");
}

static void RunTimingThread() {
    Log(LogLevel::Info, "Timing thread started (59.83Hz VBlank).");

    auto next_vblank = std::chrono::high_resolution_clock::now();
    auto next_hblank = next_vblank;
    const auto vblank_interval = std::chrono::nanoseconds(16715000);
    const auto hblank_interval = std::chrono::nanoseconds(63600);
    const bool force_dispcnt = ReadEnvBool("KH_DEBUG_FORCE_DISPCNT", false);
    const uint32_t force_dispcnt_value =
        static_cast<uint32_t>(ReadEnvInt("KH_DEBUG_FORCE_DISPCNT_VALUE", 0x1F00, 0, 0xFFFFFFFF));

    auto trigger_arm9_dma = [](DMAStartTiming timing_mode) {
        for (int channel = 0; channel < 4; ++channel) {
            DMAChannel& dma = g_memory.dma_arm9[channel];
            if (!dma.enabled || dma.timing != timing_mode) continue;

            dma.Execute(&g_memory);
            if (dma.irq_on_end) {
                g_memory.irq_arm9.RaiseIRQ(IRQBits::DMA0 << channel);
            }
        }
    };

    auto trigger_arm7_dma = [](DMAStartTiming timing_mode) {
        for (int channel = 0; channel < 4; ++channel) {
            DMAChannel& dma = g_memory.dma_arm7[channel];
            if (!dma.enabled || dma.timing != timing_mode) continue;
            dma.Execute(&g_memory);
        }
    };

    while (g_running) {
        auto now = std::chrono::high_resolution_clock::now();
        while (now >= next_hblank) {
            g_memory.irq_arm9.RaiseIRQ(IRQBits::HBlank);
            g_memory.irq_arm7.RaiseIRQ(IRQBits::HBlank);
            trigger_arm9_dma(DMAStartTiming::HBlank);
            trigger_arm7_dma(DMAStartTiming::HBlank);
            next_hblank += hblank_interval;
        }

        if (now >= next_vblank) {
            g_memory.irq_arm9.RaiseIRQ(IRQBits::VBlank);
            g_memory.irq_arm7.RaiseIRQ(IRQBits::VBlank);
            trigger_arm9_dma(DMAStartTiming::VBlank);
            trigger_arm7_dma(DMAStartTiming::VBlank);
            trigger_arm9_dma(DMAStartTiming::Special);
            trigger_arm7_dma(DMAStartTiming::Special);
            g_vblank_count.fetch_add(1, std::memory_order_relaxed);

            if (force_dispcnt) {
                if (g_memory.GetEngine2DA().ReadRegister(0x00) == 0) {
                    g_memory.GetEngine2DA().WriteRegister(0x00, force_dispcnt_value);
                }
                if (g_memory.GetEngine2DB().ReadRegister(0x00) == 0) {
                    g_memory.GetEngine2DB().WriteRegister(0x00, force_dispcnt_value);
                }
            }

            const uint8_t* oam_ptr = g_memory.GetOAM();
            const uint8_t* vram_ptr = g_memory.GetVRAM();
            size_t vram_size = g_memory.GetVRAMSize();
            const uint8_t* palette_ptr = g_memory.GetPaletteRAM();
            size_t palette_size = g_memory.GetPaletteRAMSize();
            
            g_memory.GetEngine2DA().SubmitFrame(oam_ptr, vram_ptr, vram_size, palette_ptr, palette_size);
            g_memory.GetEngine2DB().SubmitFrame(oam_ptr + 1024, vram_ptr, vram_size, palette_ptr, palette_size);

            next_vblank += vblank_interval;
        } else {
            auto diff = next_vblank - now;
            if (diff > std::chrono::milliseconds(2)) {
                std::this_thread::sleep_for(diff - std::chrono::milliseconds(1));
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
    }

    Log(LogLevel::Info, "Timing thread exiting.");
}

int main(int argc, char* argv[]) {
    std::signal(SIGSEGV, CrashHandler);
    std::signal(SIGABRT, CrashHandler);
    SanitizeSnapRuntimeEnv();

    // Force Qt software rendering when no GPU is available to suppress
    // libEGL/MESA/ZINK errors in headless or VM environments.
    if (!std::getenv("QT_OPENGL")) {
        setenv("QT_OPENGL", "software", 0);
    }
    if (!std::getenv("LIBGL_ALWAYS_SOFTWARE")) {
        setenv("LIBGL_ALWAYS_SOFTWARE", "1", 0);
    }

    std::string data_dir = "recoded/data";
    bool data_dir_set = false;
    bool superdebug_cli = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "superdebug" || arg == "--superdebug") {
            superdebug_cli = true;
            continue;
        }
        if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [data_dir] [superdebug|--superdebug]\n";
            std::cout << "Examples:\n";
            std::cout << "  " << argv[0] << " recoded/data\n";
            std::cout << "  " << argv[0] << " recoded/data superdebug\n";
            return 0;
        }
        if (!data_dir_set) {
            data_dir = arg;
            data_dir_set = true;
        } else {
            Log(LogLevel::Warning, "Ignoring extra argument: " + arg);
        }
    }

    const bool superdebug_env = ReadEnvBool("KH_SUPERDEBUG", false);
    g_superdebug_enabled.store(superdebug_cli || superdebug_env, std::memory_order_relaxed);
    if (g_superdebug_enabled.load(std::memory_order_relaxed)) {
        Log(LogLevel::Warning, "SUPERDEBUG enabled: verbose instruction trace spam is active.");
    }

    Log(LogLevel::Info, "Data directory: " + data_dir);
    VFS vfs(data_dir);

    Log(LogLevel::Info, "Initializing Virtual DS Motherboard...");
    Log(LogLevel::Info, "  Main RAM:    4 MB @ 0x02000000");
    Log(LogLevel::Info, "  WRAM:      256 KB @ 0x037F8000");
    Log(LogLevel::Info, "  ITCM:       32 KB @ 0x01000000");
    Log(LogLevel::Info, "  DTCM:       16 KB @ 0x027E0000");
    Log(LogLevel::Info, "  VRAM:      656 KB @ 0x06000000");
    Log(LogLevel::Info, "  OAM:         2 KB @ 0x07000000");
    Log(LogLevel::Info, "  Palette:     2 KB @ 0x05000000");

#ifdef EXTREME_DEBUG
    const bool default_watchdog_enabled = true;
#else
    const bool default_watchdog_enabled = false;
#endif

    g_watchdog_cfg.enabled = ReadEnvBool("KH_DEBUG_WATCHDOG", default_watchdog_enabled);
    g_watchdog_cfg.poll_ms = ReadEnvInt("KH_DEBUG_WATCHDOG_POLL_MS", 500, 100, 10000);
    g_watchdog_cfg.stall_ms = ReadEnvInt("KH_DEBUG_WATCHDOG_STALL_MS", 3000, 500, 120000);
    g_watchdog_cfg.heartbeat_ms = ReadEnvInt("KH_DEBUG_HEARTBEAT_MS", 0, 0, 120000);
    g_watchdog_cfg.same_dispatch_warn_count =
        ReadEnvInt("KH_DEBUG_SAME_DISPATCH_WARN", 10000, 1000, 1000000000);

    std::thread watchdog_thread;
    if (g_watchdog_cfg.enabled) {
        watchdog_thread = std::thread(RunDebugWatchdog);
    }

    Log(LogLevel::Info, "Spawning CPU threads...");
    std::thread arm9_thread(RunARM9);
    std::thread arm7_thread(RunARM7);
    std::thread timing_thread(RunTimingThread);

    g_memory.GetAudioManager().Initialize();
    g_memory.GetAudioManager().LoadMap("recoded/data/audio_map.txt");
    g_memory.GetInputManager().LoadConfig("bindings.ini");

    SoftwareRenderer ds_renderer;
    g_memory.GetGXEngine().renderer = &ds_renderer;
    g_memory.GetEngine2DA().renderer = &ds_renderer;
    g_memory.GetEngine2DB().renderer = &ds_renderer;

    const int exit_code = RunQtFrontend(
        argc,
        argv,
        &ds_renderer,
        &g_memory,
        &g_running,
        &g_ui_present_count,
        [](const std::string& reason) { LogRuntimeSnapshot(reason); });

    g_running.store(false, std::memory_order_relaxed);

    g_memory.GetAudioManager().Shutdown();

    arm9_thread.join();
    arm7_thread.join();
    timing_thread.join();
    if (watchdog_thread.joinable()) {
        watchdog_thread.join();
    }

    Log(LogLevel::Info, "Runtime engine shutdown cleanly.");
    return exit_code;
}
