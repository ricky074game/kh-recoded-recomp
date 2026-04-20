#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <csignal>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <array>
#include <vector>
#include <unordered_set>
#include <SDL2/SDL.h>

#include "memory_map.h"
#include "cpu_context.h"
#include "hw_irq.h"
#include "vfs.h"
#include "ds_debug.h"
#include "hw_overlay.h"
#include "sdl_renderer.h"


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
static NDSMemory         g_memory;
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

#include <SDL2/SDL.h>
#include "hw_input.h"
#include "hw_audio.h"

enum class LogLevel { Info, Warning, Error };

static void Log(LogLevel level, const std::string& msg) {
    const char* prefix = "[INFO]";
    if (level == LogLevel::Warning) prefix = "[WARN]";
    if (level == LogLevel::Error)   prefix = "[ERR ]";
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
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (lower == "1" || lower == "true" || lower == "yes" || lower == "on") {
        return true;
    }
    if (lower == "0" || lower == "false" || lower == "no" || lower == "off") {
        return false;
    }
    return fallback;
}

static void LogRuntimeSnapshot(const std::string& reason, LogLevel level = LogLevel::Info) {
    static bool code_dump_done = false;
    static const bool dump_helper_words = ReadEnvBool("KH_DEBUG_DUMP_HELPER_WORDS", false);

    if (dump_helper_words && !code_dump_done) {
        code_dump_done = true;

        std::ostringstream dump4d050;
        dump4d050 << "HelperCodeDump 0x0204D050:";
        for (uint32_t i = 0; i < 96; ++i) {
            const uint32_t addr = 0x0204D050 + (i * 4u);
            dump4d050 << " " << Hex32(addr) << "=" << Hex32(g_memory.Read32(addr));
        }
        Log(LogLevel::Info, dump4d050.str());

        std::ostringstream dump4d150;
        dump4d150 << "HelperCodeDump 0x0204D150:";
        for (uint32_t i = 0; i < 96; ++i) {
            const uint32_t addr = 0x0204D150 + (i * 4u);
            dump4d150 << " " << Hex32(addr) << "=" << Hex32(g_memory.Read32(addr));
        }
        Log(LogLevel::Info, dump4d150.str());
    }

    CPU_Context* arm9_ctx = g_arm9_ctx.load(std::memory_order_acquire);
    const uint32_t last_dispatch_addr = g_debug_arm9_last_dispatch_addr.load(std::memory_order_relaxed);

    std::ostringstream oss;
    const uint32_t dispcnt_a = g_memory.GetEngine2DA().ReadRegister(0x00);
    const uint32_t dispcnt_b = g_memory.GetEngine2DB().ReadRegister(0x00);
    const uint32_t boot_flag_5fde4 = g_memory.Read32(0x0205FDE4);
    const uint32_t boot_flag_5fde0 = g_memory.Read32(0x0205FDE0);
    const uint8_t boot_state_603c8 = g_memory.Read8(0x020603C8);
    const uint8_t boot_state_561c0 = g_memory.Read8(0x020561C0);
    const uint32_t helper_ptr_6084c = g_memory.Read32(0x0206084C);
    const uint32_t helper_state_60394 = g_memory.Read32(0x02060394);
    const uint32_t code_4d050 = g_memory.Read32(0x0204D050);
    const uint32_t code_4d054 = g_memory.Read32(0x0204D054);
    const uint32_t code_4d058 = g_memory.Read32(0x0204D058);
    const uint32_t code_4d05c = g_memory.Read32(0x0204D05C);
    const uint32_t code_4d060 = g_memory.Read32(0x0204D060);
    const uint32_t code_4d064 = g_memory.Read32(0x0204D064);
    const uint32_t code_4d068 = g_memory.Read32(0x0204D068);
    const uint32_t code_4d06c = g_memory.Read32(0x0204D06C);
    const uint32_t code_4d150 = g_memory.Read32(0x0204D150);
    const uint32_t code_4d154 = g_memory.Read32(0x0204D154);
    const uint32_t code_4d158 = g_memory.Read32(0x0204D158);
    const uint32_t code_4d15c = g_memory.Read32(0x0204D15C);
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
        << " dispcntB=" << Hex32(dispcnt_b)
        << " boot5fde4=" << Hex32(boot_flag_5fde4)
        << " boot5fde0=" << Hex32(boot_flag_5fde0)
        << " state603c8=" << Hex32(static_cast<uint32_t>(boot_state_603c8))
        << " state561c0=" << Hex32(static_cast<uint32_t>(boot_state_561c0))
        << " ptr6084c=" << Hex32(helper_ptr_6084c)
        << " state60394=" << Hex32(helper_state_60394)
        << " mem4d050=" << Hex32(code_4d050)
        << " mem4d054=" << Hex32(code_4d054)
        << " mem4d058=" << Hex32(code_4d058)
        << " mem4d05c=" << Hex32(code_4d05c)
        << " mem4d060=" << Hex32(code_4d060)
        << " mem4d064=" << Hex32(code_4d064)
        << " mem4d068=" << Hex32(code_4d068)
        << " mem4d06c=" << Hex32(code_4d06c)
        << " mem4d150=" << Hex32(code_4d150)
        << " mem4d154=" << Hex32(code_4d154)
        << " mem4d158=" << Hex32(code_4d158)
        << " mem4d15c=" << Hex32(code_4d15c);

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

        if (last_dispatch_addr >= 0x02029B54 && last_dispatch_addr <= 0x02029EA0) {
            oss << " r0=" << Hex32(arm9_ctx->r[0])
                << " r1=" << Hex32(arm9_ctx->r[1])
                << " r2=" << Hex32(arm9_ctx->r[2])
                << " r3=" << Hex32(arm9_ctx->r[3])
                << " r4=" << Hex32(arm9_ctx->r[4])
                << " r5=" << Hex32(arm9_ctx->r[5])
                << " r6=" << Hex32(arm9_ctx->r[6])
                << " r7=" << Hex32(arm9_ctx->r[7])
                << " r8=" << Hex32(arm9_ctx->r[8]);
        }

        if (last_dispatch_addr >= 0x02000C98 && last_dispatch_addr <= 0x02000EDC) {
            oss << " loop_r0=" << Hex32(arm9_ctx->r[0])
                << " loop_r1=" << Hex32(arm9_ctx->r[1])
                << " loop_r2=" << Hex32(arm9_ctx->r[2])
                << " loop_r3=" << Hex32(arm9_ctx->r[3])
                << " loop_r4=" << Hex32(arm9_ctx->r[4])
                << " loop_r5=" << Hex32(arm9_ctx->r[5])
                << " loop_r6=" << Hex32(arm9_ctx->r[6])
                << " loop_r7=" << Hex32(arm9_ctx->r[7])
                << " loop_r8=" << Hex32(arm9_ctx->r[8]);
        }
    }

    Log(level, oss.str());
}

static void RunDebugWatchdog() {
    using clock = std::chrono::steady_clock;

    std::ostringstream start;
    start << "Debug watchdog enabled"
          << " (poll=" << g_watchdog_cfg.poll_ms
          << "ms, stall=" << g_watchdog_cfg.stall_ms
          << "ms, heartbeat=";
    if (g_watchdog_cfg.heartbeat_ms > 0) {
        start << g_watchdog_cfg.heartbeat_ms << "ms";
    } else {
        start << "off";
    }
    start << ")";
    Log(LogLevel::Info, start.str());

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
        if (!tight_loop_reported && same_dispatch >= static_cast<uint32_t>(g_watchdog_cfg.same_dispatch_warn_count)) {
            LogRuntimeSnapshot("ARM9 is repeatedly dispatching the same address (possible tight loop before first frame)",
                               LogLevel::Warning);
            tight_loop_reported = true;
        } else if (tight_loop_reported && same_dispatch <= tight_loop_reset_threshold) {
            LogRuntimeSnapshot("ARM9 tight-loop condition cleared");
            tight_loop_reported = false;
        }

    }

    Log(LogLevel::Info, "Debug watchdog thread exiting.");
}

#include <csignal>
#include <cstdlib>
#include <iomanip>

void PrintTrace(CPU_Context* ctx) {
    if (!ctx) return;
    std::cout << "Execution Trace (Ring Buffer, last 10):\n";
    uint8_t curr = ctx->trace_idx;
    for (int i = 0; i < 10; ++i) {
        uint8_t idx = (curr - 1 - i) % 256;
        uint32_t addr = ctx->trace_buffer[idx];
        if (addr == 0 && i > 0) break; 
        if (i == 0) {
            std::cout << " -> 0x" << std::hex << std::setw(8) << std::setfill('0') << addr << " (Instruction that crashed)\n";
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
    const auto vblank_interval = std::chrono::nanoseconds(16715000);
    const bool force_dispcnt = ReadEnvBool("KH_DEBUG_FORCE_DISPCNT", false);
    const uint32_t force_dispcnt_value =
        static_cast<uint32_t>(ReadEnvInt("KH_DEBUG_FORCE_DISPCNT_VALUE", 0x1F00, 0, 0xFFFFFFFF));

    while (g_running) {
        auto now = std::chrono::high_resolution_clock::now();
        if (now >= next_vblank) {
            g_memory.irq_arm9.RaiseIRQ(IRQBits::VBlank);
            g_memory.irq_arm7.RaiseIRQ(IRQBits::VBlank);
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
            g_memory.GetEngine2DA().SubmitFrame(oam_ptr);
            g_memory.GetEngine2DB().SubmitFrame(oam_ptr);

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

namespace {
constexpr int kDSWidth = 256;
constexpr int kDSHeight = 192;
constexpr int kTopWindowDefaultWidth = 960;
constexpr int kTopWindowDefaultHeight = 720;
constexpr int kBottomWindowDefaultWidth = 1280;
constexpr int kBottomWindowDefaultHeight = 760;
constexpr int kMenuBarHeight = 44;

struct ModOptionUi {
    std::string name;
    bool enabled = false;
};

struct ModEntryUi {
    std::string name;
    std::string author;
    std::string summary;
    std::vector<std::string> dependencies;
    std::vector<ModOptionUi> options;
    bool enabled = true;
    bool expanded = false;
};

struct TopWindowLayout {
    SDL_Rect menu_bar{};
    SDL_Rect mods_button{};
    SDL_Rect options_button{};
    SDL_Rect exit_button{};

    SDL_Rect options_dropdown{};
    SDL_Rect options_keyboard_item{};
    SDL_Rect options_mod_settings_item{};

    SDL_Rect top_screen{};
};

struct BottomWindowLayout {
    SDL_Rect bottom_screen{};
    SDL_Rect status_bar{};
};

struct ModWindowLayout {
    SDL_Rect header_bar{};
    SDL_Rect mod_list{};
    SDL_Rect mod_details{};
    SDL_Rect details_gear_button{};
};

struct ModListLayoutRow {
    SDL_Rect row{};
    SDL_Rect arrow{};
    std::vector<SDL_Rect> dependency_rows;
};

bool PointInRect(int x, int y, const SDL_Rect& rect) {
    return x >= rect.x && y >= rect.y && x < (rect.x + rect.w) && y < (rect.y + rect.h);
}

SDL_Color MakeColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return SDL_Color{r, g, b, a};
}

void FillRect(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, &rect);
}

void StrokeRect(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderDrawRect(renderer, &rect);
}

void DrawArrowDown(SDL_Renderer* renderer, int x, int y, int size, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int row = 0; row < size; ++row) {
        const int y_row = y + row;
        SDL_RenderDrawLine(renderer, x - row, y_row, x + row, y_row);
    }
}

void DrawArrowRight(SDL_Renderer* renderer, int x, int y, int size, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int col = 0; col < size; ++col) {
        const int x_col = x + col;
        SDL_RenderDrawLine(renderer, x_col, y - col, x_col, y + col);
    }
}

const std::array<uint8_t, 7>& LookupGlyph(char c) {
    static const std::array<uint8_t, 7> kBlank{{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
    static const std::array<uint8_t, 7> kDash{{0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}};
    static const std::array<uint8_t, 7> kColon{{0x00, 0x04, 0x00, 0x00, 0x04, 0x00, 0x00}};
    static const std::array<uint8_t, 7> kPeriod{{0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}};
    static const std::array<uint8_t, 7> kSlash{{0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10}};
    static const std::array<uint8_t, 7> kA{{0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}};
    static const std::array<uint8_t, 7> kB{{0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}};
    static const std::array<uint8_t, 7> kC{{0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}};
    static const std::array<uint8_t, 7> kD{{0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}};
    static const std::array<uint8_t, 7> kE{{0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}};
    static const std::array<uint8_t, 7> kF{{0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}};
    static const std::array<uint8_t, 7> kG{{0x0F, 0x10, 0x10, 0x17, 0x11, 0x11, 0x0E}};
    static const std::array<uint8_t, 7> kH{{0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}};
    static const std::array<uint8_t, 7> kI{{0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F}};
    static const std::array<uint8_t, 7> kJ{{0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E}};
    static const std::array<uint8_t, 7> kK{{0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}};
    static const std::array<uint8_t, 7> kL{{0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}};
    static const std::array<uint8_t, 7> kM{{0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}};
    static const std::array<uint8_t, 7> kN{{0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}};
    static const std::array<uint8_t, 7> kO{{0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}};
    static const std::array<uint8_t, 7> kP{{0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}};
    static const std::array<uint8_t, 7> kQ{{0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}};
    static const std::array<uint8_t, 7> kR{{0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}};
    static const std::array<uint8_t, 7> kS{{0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}};
    static const std::array<uint8_t, 7> kT{{0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}};
    static const std::array<uint8_t, 7> kU{{0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}};
    static const std::array<uint8_t, 7> kV{{0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}};
    static const std::array<uint8_t, 7> kW{{0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}};
    static const std::array<uint8_t, 7> kX{{0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}};
    static const std::array<uint8_t, 7> kY{{0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}};
    static const std::array<uint8_t, 7> kZ{{0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}};
    static const std::array<uint8_t, 7> k0{{0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}};
    static const std::array<uint8_t, 7> k1{{0x04, 0x0C, 0x14, 0x04, 0x04, 0x04, 0x1F}};
    static const std::array<uint8_t, 7> k2{{0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}};
    static const std::array<uint8_t, 7> k3{{0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}};
    static const std::array<uint8_t, 7> k4{{0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}};
    static const std::array<uint8_t, 7> k5{{0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E}};
    static const std::array<uint8_t, 7> k6{{0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}};
    static const std::array<uint8_t, 7> k7{{0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}};
    static const std::array<uint8_t, 7> k8{{0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}};
    static const std::array<uint8_t, 7> k9{{0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}};

    const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    switch (upper) {
        case 'A': return kA;
        case 'B': return kB;
        case 'C': return kC;
        case 'D': return kD;
        case 'E': return kE;
        case 'F': return kF;
        case 'G': return kG;
        case 'H': return kH;
        case 'I': return kI;
        case 'J': return kJ;
        case 'K': return kK;
        case 'L': return kL;
        case 'M': return kM;
        case 'N': return kN;
        case 'O': return kO;
        case 'P': return kP;
        case 'Q': return kQ;
        case 'R': return kR;
        case 'S': return kS;
        case 'T': return kT;
        case 'U': return kU;
        case 'V': return kV;
        case 'W': return kW;
        case 'X': return kX;
        case 'Y': return kY;
        case 'Z': return kZ;
        case '0': return k0;
        case '1': return k1;
        case '2': return k2;
        case '3': return k3;
        case '4': return k4;
        case '5': return k5;
        case '6': return k6;
        case '7': return k7;
        case '8': return k8;
        case '9': return k9;
        case '-': return kDash;
        case ':': return kColon;
        case '.': return kPeriod;
        case '/': return kSlash;
        case ' ': return kBlank;
        default: return kBlank;
    }
}

void DrawText(SDL_Renderer* renderer, int x, int y, const std::string& text, SDL_Color color, int scale = 2) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

    int cursor_x = x;
    int cursor_y = y;
    for (char c : text) {
        if (c == '\n') {
            cursor_x = x;
            cursor_y += 8 * scale;
            continue;
        }

        const auto& glyph = LookupGlyph(c);
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if (((glyph[row] >> (4 - col)) & 1u) == 0u) continue;
                SDL_Rect px{cursor_x + (col * scale), cursor_y + (row * scale), scale, scale};
                SDL_RenderFillRect(renderer, &px);
            }
        }
        cursor_x += 6 * scale;
    }
}

int MeasureTextWidth(const std::string& text, int scale = 2) {
    return static_cast<int>(text.size()) * 6 * scale;
}

SDL_Rect CenterRect(const SDL_Rect& outer, int width, int height) {
    SDL_Rect rect{};
    rect.w = width;
    rect.h = height;
    rect.x = outer.x + (outer.w - width) / 2;
    rect.y = outer.y + (outer.h - height) / 2;
    return rect;
}

TopWindowLayout ComputeTopWindowLayout(int window_w, int window_h) {
    TopWindowLayout layout{};

    layout.menu_bar = SDL_Rect{0, 0, window_w, kMenuBarHeight};
    layout.mods_button = SDL_Rect{14, 6, 100, 32};
    layout.options_button = SDL_Rect{122, 6, 152, 32};
    layout.exit_button = SDL_Rect{window_w - 86, 6, 72, 32};

    layout.options_dropdown = SDL_Rect{layout.options_button.x, kMenuBarHeight, 280, 84};
    layout.options_keyboard_item = SDL_Rect{layout.options_dropdown.x + 2, layout.options_dropdown.y + 2,
                                            layout.options_dropdown.w - 4, 38};
    layout.options_mod_settings_item = SDL_Rect{layout.options_dropdown.x + 2,
                                                layout.options_dropdown.y + 42,
                                                layout.options_dropdown.w - 4, 38};

    const int margin = 22;
    const int content_y = kMenuBarHeight + margin;
    const int avail_w = std::max(128, window_w - (margin * 2));
    const int avail_h = std::max(96, window_h - content_y - margin);

    int draw_w = avail_w;
    int draw_h = (draw_w * kDSHeight) / kDSWidth;
    if (draw_h > avail_h) {
        draw_h = avail_h;
        draw_w = (draw_h * kDSWidth) / kDSHeight;
    }

    layout.top_screen = SDL_Rect{
        (window_w - draw_w) / 2,
        content_y + (avail_h - draw_h) / 2,
        draw_w,
        draw_h,
    };
    return layout;
}

BottomWindowLayout ComputeBottomWindowLayout(int window_w, int window_h) {
    BottomWindowLayout layout{};
    const int margin = 26;
    const int status_h = 46;

    const int avail_w = std::max(128, window_w - (margin * 2));
    const int avail_h = std::max(96, window_h - (margin * 3) - status_h);

    int draw_w = avail_w;
    int draw_h = (draw_w * kDSHeight) / kDSWidth;
    if (draw_h > avail_h) {
        draw_h = avail_h;
        draw_w = (draw_h * kDSWidth) / kDSHeight;
    }

    layout.bottom_screen = SDL_Rect{
        (window_w - draw_w) / 2,
        margin + (avail_h - draw_h) / 2,
        draw_w,
        draw_h,
    };

    layout.status_bar = SDL_Rect{margin, layout.bottom_screen.y + layout.bottom_screen.h + margin,
                                 std::max(120, window_w - (margin * 2)), status_h};
    return layout;
}

ModWindowLayout ComputeModWindowLayout(int window_w, int window_h) {
    ModWindowLayout layout{};

    layout.header_bar = SDL_Rect{0, 0, window_w, 56};

    const int margin = 16;
    const int content_y = layout.header_bar.h + margin;
    const int content_h = std::max(160, window_h - content_y - margin);

    const int list_w = std::max(220, (window_w - (margin * 3)) * 2 / 5);
    const int details_w = std::max(260, window_w - list_w - (margin * 3));

    layout.mod_list = SDL_Rect{margin, content_y, list_w, content_h};
    layout.mod_details = SDL_Rect{layout.mod_list.x + list_w + margin, content_y, details_w, content_h};
    layout.details_gear_button = SDL_Rect{layout.mod_details.x + layout.mod_details.w - 34,
                                          layout.mod_details.y + 8, 26, 26};
    return layout;
}

std::vector<ModEntryUi> CreateSampleMods() {
    std::vector<ModEntryUi> mods;
    mods.push_back(ModEntryUi{
        "UI CORE",
        "RUNTIME TEAM",
        "BASE UI LAYER SHARED BY SAMPLE MODS",
        {},
        {{"ANIMATED PANELS", true}, {"HIGH CONTRAST", false}},
        true,
        true,
    });
    mods.push_back(ModEntryUi{
        "CRYSTAL HUD",
        "AURORA LAB",
        "VISUAL SHELL OVERLAY FOR WINDOW DEMO",
        {"UI CORE"},
        {{"GLASS EFFECT", true}, {"COMPACT STATUS", false}},
        true,
        false,
    });
    mods.push_back(ModEntryUi{
        "INPUT HOOKS",
        "QA TOOLING",
        "KEYBOARD AND MOUSE INPUT ROUTING LAYER",
        {},
        {{"RAW MOUSE", true}, {"DEBOUNCE", true}},
        true,
        false,
    });
    mods.push_back(ModEntryUi{
        "PHOTO MODE SAMPLE",
        "DEMO ONLY",
        "SAMPLE MOD ENTRY WITH NO GAMEPLAY FUNCTION",
        {"UI CORE", "INPUT HOOKS"},
        {{"GRID OVERLAY", true}, {"AUTO SNAPSHOT", false}, {"WIDE CAPTURE", true}},
        false,
        false,
    });
    return mods;
}

std::vector<ModListLayoutRow> BuildModListRows(const std::vector<ModEntryUi>& mods, const SDL_Rect& list_rect) {
    std::vector<ModListLayoutRow> rows;
    rows.reserve(mods.size());

    int cursor_y = list_rect.y + 12;
    for (const auto& mod : mods) {
        ModListLayoutRow row{};
        row.row = SDL_Rect{list_rect.x + 8, cursor_y, list_rect.w - 16, 50};
        row.arrow = SDL_Rect{row.row.x + 8, row.row.y + 17, 14, 14};
        cursor_y += 56;

        if (mod.expanded) {
            for (size_t dep = 0; dep < mod.dependencies.size(); ++dep) {
                SDL_Rect dep_row{row.row.x + 18, cursor_y, row.row.w - 22, 22};
                row.dependency_rows.push_back(dep_row);
                cursor_y += 24;
            }
        }

        rows.push_back(row);
    }

    return rows;
}

std::vector<SDL_Rect> BuildModOptionToggleRects(const SDL_Rect& details_rect, const ModEntryUi& mod) {
    std::vector<SDL_Rect> toggles;
    toggles.reserve(mod.options.size());

    int y = details_rect.y + 158;
    for (size_t i = 0; i < mod.options.size(); ++i) {
        toggles.push_back(SDL_Rect{details_rect.x + 14, y, 20, 20});
        y += 30;
    }

    return toggles;
}

void DrawCheckMark(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderDrawLine(renderer, rect.x + 4, rect.y + rect.h / 2, rect.x + 8, rect.y + rect.h - 5);
    SDL_RenderDrawLine(renderer, rect.x + 8, rect.y + rect.h - 5, rect.x + rect.w - 4, rect.y + 4);
}

void DrawGearIcon(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    const int cx = rect.x + rect.w / 2;
    const int cy = rect.y + rect.h / 2;
    const int outer = std::min(rect.w, rect.h) / 2 - 2;
    const int inner = std::max(3, outer / 2);

    for (int i = 0; i < 8; ++i) {
        const float a = static_cast<float>(i) * 3.1415926f / 4.0f;
        const int x0 = cx + static_cast<int>(std::cos(a) * inner);
        const int y0 = cy + static_cast<int>(std::sin(a) * inner);
        const int x1 = cx + static_cast<int>(std::cos(a) * outer);
        const int y1 = cy + static_cast<int>(std::sin(a) * outer);
        SDL_RenderDrawLine(renderer, x0, y0, x1, y1);
    }

    SDL_Rect center{cx - 4, cy - 4, 8, 8};
    SDL_RenderDrawRect(renderer, &center);
}

bool WriteFramebufferPPM(const std::string& path,
                         const std::array<uint32_t, kDSWidth * kDSHeight>& pixels) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    out << "P6\n" << kDSWidth << " " << kDSHeight << "\n255\n";
    for (uint32_t px : pixels) {
        const uint8_t rgb[3] = {
            static_cast<uint8_t>((px >> 24) & 0xFF),
            static_cast<uint8_t>((px >> 16) & 0xFF),
            static_cast<uint8_t>((px >> 8) & 0xFF),
        };
        out.write(reinterpret_cast<const char*>(rgb), 3);
    }
    return out.good();
}
}

int main(int argc, char* argv[]) {
    Log(LogLevel::Info, "==================================================");
    Log(LogLevel::Info, "  Kingdom Hearts Re:coded — Static Recompilation  ");
    Log(LogLevel::Info, "==================================================");

    std::signal(SIGSEGV, CrashHandler);
    std::signal(SIGABRT, CrashHandler);

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

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        Log(LogLevel::Error, "Failed to init SDL: " + std::string(SDL_GetError()));
        Log(LogLevel::Info, "Continuing in Headless mode...");
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    } else {
        SDL_Window* top_window = SDL_CreateWindow(
            "KH Re:coded - TOP SCREEN",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            kTopWindowDefaultWidth,
            kTopWindowDefaultHeight,
            SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

        SDL_Window* bottom_window = SDL_CreateWindow(
            "KH Re:coded - BOTTOM SCREEN",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            kBottomWindowDefaultWidth,
            kBottomWindowDefaultHeight,
            SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

        SDL_Window* mod_window = SDL_CreateWindow(
            "KH Re:coded - MOD MANAGER",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            980,
            720,
            SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);

        auto create_renderer = [](SDL_Window* win) -> SDL_Renderer* {
            if (!win) return nullptr;
            SDL_Renderer* out = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
            if (!out) {
                out = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
            }
            return out;
        };

        SDL_Renderer* top_renderer = create_renderer(top_window);
        SDL_Renderer* bottom_renderer = create_renderer(bottom_window);
        SDL_Renderer* mod_renderer = create_renderer(mod_window);
        SDL_Texture* top_screen_texture = nullptr;
        SDL_Texture* bottom_screen_texture = nullptr;

        if (top_renderer) {
            top_screen_texture = SDL_CreateTexture(top_renderer, SDL_PIXELFORMAT_RGBA8888,
                                                   SDL_TEXTUREACCESS_STREAMING, kDSWidth, kDSHeight);
        }
        if (bottom_renderer) {
            bottom_screen_texture = SDL_CreateTexture(bottom_renderer, SDL_PIXELFORMAT_RGBA8888,
                                                      SDL_TEXTUREACCESS_STREAMING, kDSWidth, kDSHeight);
        }

        if (top_window && bottom_window && mod_window && top_renderer && bottom_renderer && mod_renderer &&
            top_screen_texture && bottom_screen_texture) {
            SDL_SetRenderDrawBlendMode(top_renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawBlendMode(bottom_renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawBlendMode(mod_renderer, SDL_BLENDMODE_BLEND);

            SDL2Renderer ds_renderer(top_renderer);
            g_memory.GetGXEngine().renderer = &ds_renderer;
            g_memory.GetEngine2DA().renderer = &ds_renderer;
            g_memory.GetEngine2DB().renderer = &ds_renderer;

            std::array<uint32_t, kDSWidth * kDSHeight> top_pixels{};
            std::array<uint32_t, kDSWidth * kDSHeight> bottom_pixels{};

            const int debug_dump_vblank =
                ReadEnvInt("KH_DEBUG_DUMP_FRAME_VBLANK", 0, 0, 1000000000);
            bool debug_dump_done = (debug_dump_vblank == 0);

            std::vector<ModEntryUi> mods = CreateSampleMods();
            int selected_mod = 0;
            bool show_options_dropdown = false;
            bool show_keyboard_settings = false;
            bool show_mod_settings = false;
            bool touch_drag_active = false;
            bool mods_window_visible = false;

            const Uint32 top_window_id = SDL_GetWindowID(top_window);
            const Uint32 bottom_window_id = SDL_GetWindowID(bottom_window);
            const Uint32 mod_window_id = SDL_GetWindowID(mod_window);

            Log(LogLevel::Info, "Desktop UI active: top-screen menu now controls separate mods window.");
            Log(LogLevel::Info, "Controller overlay remains removed. Input uses keyboard + mouse touch.");

            SDL_Event e;
            while (g_running) {
                int top_w = 0;
                int top_h = 0;
                SDL_GetWindowSize(top_window, &top_w, &top_h);
                TopWindowLayout top_layout = ComputeTopWindowLayout(top_w, top_h);

                int bottom_w = 0;
                int bottom_h = 0;
                SDL_GetWindowSize(bottom_window, &bottom_w, &bottom_h);
                BottomWindowLayout bottom_layout = ComputeBottomWindowLayout(bottom_w, bottom_h);

                int mod_w = 0;
                int mod_h = 0;
                SDL_GetWindowSize(mod_window, &mod_w, &mod_h);
                if (mod_w <= 0) mod_w = 980;
                if (mod_h <= 0) mod_h = 720;
                ModWindowLayout mod_layout = ComputeModWindowLayout(mod_w, mod_h);

                g_memory.GetInputManager().SetDisplayScale(
                    static_cast<float>(bottom_layout.bottom_screen.w) / static_cast<float>(kDSWidth),
                    bottom_layout.bottom_screen.x,
                    bottom_layout.bottom_screen.y);

                SDL_Rect top_bounds{0, 0, top_w, top_h};
                SDL_Rect keyboard_overlay = CenterRect(top_bounds, 650, 420);
                SDL_Rect keyboard_close{keyboard_overlay.x + keyboard_overlay.w - 32, keyboard_overlay.y + 10, 20, 20};

                SDL_Rect mod_bounds{0, 0, mod_w, mod_h};
                SDL_Rect mod_overlay = CenterRect(mod_bounds, 560, 420);
                SDL_Rect mod_overlay_close{mod_overlay.x + mod_overlay.w - 32, mod_overlay.y + 10, 20, 20};

                while (SDL_PollEvent(&e)) {
                    if (e.type == SDL_QUIT) {
                        g_running = false;
                        break;
                    }

                    if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE) {
                        if (e.window.windowID == mod_window_id) {
                            mods_window_visible = false;
                            show_mod_settings = false;
                            SDL_HideWindow(mod_window);
                            continue;
                        }
                        g_running = false;
                        break;
                    }

                    if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
                        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F1) {
                            LogRuntimeSnapshot("Manual debug snapshot (F1)");
                        }
                        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
                            g_running = false;
                            break;
                        }
                        g_memory.GetInputManager().HandleKeyEvent(e.key);
                        continue;
                    }

                    if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
                        if (e.button.button != SDL_BUTTON_LEFT) {
                            continue;
                        }

                        const bool pressed = (e.type == SDL_MOUSEBUTTONDOWN);
                        const int mx = e.button.x;
                        const int my = e.button.y;

                        if (e.button.windowID == top_window_id) {
                            if (!pressed) {
                                continue;
                            }

                            bool consumed = false;
                            if (show_keyboard_settings) {
                                if (PointInRect(mx, my, keyboard_close)) {
                                    show_keyboard_settings = false;
                                    consumed = true;
                                } else if (PointInRect(mx, my, keyboard_overlay)) {
                                    consumed = true;
                                }
                            }

                            if (!consumed && PointInRect(mx, my, top_layout.exit_button)) {
                                g_running = false;
                                consumed = true;
                            }

                            if (!consumed && PointInRect(mx, my, top_layout.mods_button)) {
                                mods_window_visible = !mods_window_visible;
                                if (mods_window_visible) {
                                    SDL_ShowWindow(mod_window);
                                    SDL_RaiseWindow(mod_window);
                                } else {
                                    show_mod_settings = false;
                                    SDL_HideWindow(mod_window);
                                }
                                show_options_dropdown = false;
                                consumed = true;
                            }

                            if (!consumed && PointInRect(mx, my, top_layout.options_button)) {
                                show_options_dropdown = !show_options_dropdown;
                                consumed = true;
                            }

                            if (!consumed && show_options_dropdown && PointInRect(mx, my, top_layout.options_keyboard_item)) {
                                show_keyboard_settings = !show_keyboard_settings;
                                show_options_dropdown = false;
                                consumed = true;
                            }

                            if (!consumed && show_options_dropdown && PointInRect(mx, my, top_layout.options_mod_settings_item)) {
                                mods_window_visible = true;
                                show_mod_settings = true;
                                show_options_dropdown = false;
                                SDL_ShowWindow(mod_window);
                                SDL_RaiseWindow(mod_window);
                                consumed = true;
                            }

                            if (!consumed) {
                                show_options_dropdown = false;
                            }
                            continue;
                        }

                        if (e.button.windowID == bottom_window_id) {
                            if (!pressed) {
                                if (touch_drag_active) {
                                    g_memory.GetInputManager().HandleMouseEvent(e.button);
                                    touch_drag_active = false;
                                }
                                continue;
                            }

                            if (PointInRect(mx, my, bottom_layout.bottom_screen)) {
                                g_memory.GetInputManager().HandleMouseEvent(e.button);
                                touch_drag_active = true;
                            } else {
                                touch_drag_active = false;
                            }
                            continue;
                        }

                        if (e.button.windowID == mod_window_id) {
                            if (!mods_window_visible || !pressed || mods.empty()) {
                                continue;
                            }

                            selected_mod = std::clamp(selected_mod, 0, static_cast<int>(mods.size() - 1));
                            bool consumed = false;

                            if (show_mod_settings) {
                                if (PointInRect(mx, my, mod_overlay_close)) {
                                    show_mod_settings = false;
                                    consumed = true;
                                } else if (PointInRect(mx, my, mod_overlay)) {
                                    auto overlay_toggles = BuildModOptionToggleRects(mod_overlay, mods[selected_mod]);
                                    for (size_t i = 0; i < overlay_toggles.size(); ++i) {
                                        if (PointInRect(mx, my, overlay_toggles[i])) {
                                            mods[selected_mod].options[i].enabled = !mods[selected_mod].options[i].enabled;
                                            consumed = true;
                                            break;
                                        }
                                    }
                                    if (!consumed) consumed = true;
                                }
                            }

                            if (!consumed && PointInRect(mx, my, mod_layout.details_gear_button)) {
                                show_mod_settings = true;
                                consumed = true;
                            }

                            if (!consumed && PointInRect(mx, my, mod_layout.mod_list)) {
                                auto rows = BuildModListRows(mods, mod_layout.mod_list);
                                for (size_t i = 0; i < rows.size(); ++i) {
                                    if (!mods[i].dependencies.empty() && PointInRect(mx, my, rows[i].arrow)) {
                                        mods[i].expanded = !mods[i].expanded;
                                        consumed = true;
                                        break;
                                    }
                                    if (PointInRect(mx, my, rows[i].row)) {
                                        selected_mod = static_cast<int>(i);
                                        consumed = true;
                                        break;
                                    }
                                }
                            }

                            if (!consumed && PointInRect(mx, my, mod_layout.mod_details)) {
                                auto detail_toggles = BuildModOptionToggleRects(mod_layout.mod_details, mods[selected_mod]);
                                for (size_t i = 0; i < detail_toggles.size(); ++i) {
                                    if (PointInRect(mx, my, detail_toggles[i])) {
                                        mods[selected_mod].options[i].enabled = !mods[selected_mod].options[i].enabled;
                                        consumed = true;
                                        break;
                                    }
                                }
                            }
                            continue;
                        }
                    }

                    if (e.type == SDL_MOUSEMOTION) {
                        if (e.motion.windowID == bottom_window_id && touch_drag_active) {
                            g_memory.GetInputManager().HandleMouseMotion(e.motion);
                        }
                    }
                }

                ds_renderer.CopyFramebuffers(top_pixels, bottom_pixels);

                if (!debug_dump_done) {
                    const uint64_t vblank = g_vblank_count.load(std::memory_order_relaxed);
                    if (vblank >= static_cast<uint64_t>(debug_dump_vblank)) {
                        const bool top_ok = WriteFramebufferPPM("/tmp/kh_debug_top.ppm", top_pixels);
                        const bool bottom_ok = WriteFramebufferPPM("/tmp/kh_debug_bottom.ppm", bottom_pixels);
                        if (top_ok && bottom_ok) {
                            Log(LogLevel::Info,
                                "Wrote debug frame dumps to /tmp/kh_debug_top.ppm and /tmp/kh_debug_bottom.ppm "
                                "at vblank=" + std::to_string(vblank));
                        } else {
                            Log(LogLevel::Warning,
                                "Failed to write one or more debug frame dumps at vblank=" +
                                std::to_string(vblank));
                        }
                        debug_dump_done = true;
                    }
                }

                SDL_UpdateTexture(top_screen_texture, nullptr, top_pixels.data(), kDSWidth * 4);
                SDL_UpdateTexture(bottom_screen_texture, nullptr, bottom_pixels.data(), kDSWidth * 4);

                FillRect(top_renderer, SDL_Rect{0, 0, top_w, top_h}, MakeColor(10, 14, 24));
                FillRect(top_renderer, top_layout.menu_bar, MakeColor(21, 29, 43));
                FillRect(top_renderer, SDL_Rect{0, kMenuBarHeight - 1, top_w, 1}, MakeColor(82, 122, 174));

                FillRect(top_renderer, top_layout.mods_button,
                         mods_window_visible ? MakeColor(58, 90, 134) : MakeColor(36, 52, 76));
                FillRect(top_renderer, top_layout.options_button,
                         show_options_dropdown ? MakeColor(58, 90, 134) : MakeColor(36, 52, 76));
                FillRect(top_renderer, top_layout.exit_button, MakeColor(122, 46, 52));
                StrokeRect(top_renderer, top_layout.mods_button, MakeColor(98, 138, 190));
                StrokeRect(top_renderer, top_layout.options_button, MakeColor(98, 138, 190));
                StrokeRect(top_renderer, top_layout.exit_button, MakeColor(190, 98, 98));

                DrawText(top_renderer, top_layout.mods_button.x + 18, top_layout.mods_button.y + 9,
                         "MODS", MakeColor(230, 240, 255), 2);
                DrawText(top_renderer, top_layout.options_button.x + 12, top_layout.options_button.y + 9,
                         "OPTIONS", MakeColor(230, 240, 255), 2);
                DrawText(top_renderer, top_layout.exit_button.x + 14, top_layout.exit_button.y + 9,
                         "EXIT", MakeColor(255, 236, 236), 2);

                SDL_Rect top_glow{top_layout.top_screen.x - 10, top_layout.top_screen.y - 10,
                                  top_layout.top_screen.w + 20, top_layout.top_screen.h + 20};
                FillRect(top_renderer, top_glow, MakeColor(25, 38, 58));
                SDL_RenderCopy(top_renderer, top_screen_texture, nullptr, &top_layout.top_screen);
                StrokeRect(top_renderer, top_glow, MakeColor(90, 134, 189));

                DrawText(top_renderer, top_layout.top_screen.x,
                         top_layout.top_screen.y - 28,
                         "TOP SCREEN", MakeColor(217, 235, 255), 2);

                if (show_options_dropdown) {
                    FillRect(top_renderer, top_layout.options_dropdown, MakeColor(28, 39, 58));
                    StrokeRect(top_renderer, top_layout.options_dropdown, MakeColor(92, 130, 182));
                    FillRect(top_renderer, top_layout.options_keyboard_item, MakeColor(40, 55, 78));
                    FillRect(top_renderer, top_layout.options_mod_settings_item, MakeColor(40, 55, 78));
                    DrawText(top_renderer,
                             top_layout.options_keyboard_item.x + 8,
                             top_layout.options_keyboard_item.y + 12,
                             "KEYBOARD SETTINGS",
                             MakeColor(228, 241, 255),
                             1);
                    DrawText(top_renderer,
                             top_layout.options_mod_settings_item.x + 8,
                             top_layout.options_mod_settings_item.y + 12,
                             "MOD SETTINGS",
                             MakeColor(228, 241, 255),
                             1);
                }

                if (show_keyboard_settings) {
                    FillRect(top_renderer, SDL_Rect{0, 0, top_w, top_h}, MakeColor(0, 0, 0, 105));
                    FillRect(top_renderer, keyboard_overlay, MakeColor(27, 35, 49));
                    StrokeRect(top_renderer, keyboard_overlay, MakeColor(104, 140, 187));
                    FillRect(top_renderer, keyboard_close, MakeColor(115, 58, 58));
                    DrawText(top_renderer, keyboard_close.x + 5, keyboard_close.y + 4,
                             "X", MakeColor(245, 228, 228), 1);

                    DrawText(top_renderer, keyboard_overlay.x + 18, keyboard_overlay.y + 16,
                             "KEYBOARD SETTINGS", MakeColor(226, 240, 255), 2);
                    DrawText(top_renderer, keyboard_overlay.x + 18, keyboard_overlay.y + 60,
                             "A : SPACE", MakeColor(188, 213, 242), 1);
                    DrawText(top_renderer, keyboard_overlay.x + 18, keyboard_overlay.y + 82,
                             "B : Z", MakeColor(188, 213, 242), 1);
                    DrawText(top_renderer, keyboard_overlay.x + 18, keyboard_overlay.y + 104,
                             "X : X", MakeColor(188, 213, 242), 1);
                    DrawText(top_renderer, keyboard_overlay.x + 18, keyboard_overlay.y + 126,
                             "Y : C", MakeColor(188, 213, 242), 1);
                    DrawText(top_renderer, keyboard_overlay.x + 18, keyboard_overlay.y + 148,
                             "L : A", MakeColor(188, 213, 242), 1);
                    DrawText(top_renderer, keyboard_overlay.x + 18, keyboard_overlay.y + 170,
                             "R : S", MakeColor(188, 213, 242), 1);
                    DrawText(top_renderer, keyboard_overlay.x + 18, keyboard_overlay.y + 192,
                             "START : ENTER", MakeColor(188, 213, 242), 1);
                    DrawText(top_renderer, keyboard_overlay.x + 18, keyboard_overlay.y + 214,
                             "SELECT : RIGHT SHIFT", MakeColor(188, 213, 242), 1);
                    DrawText(top_renderer, keyboard_overlay.x + 18, keyboard_overlay.y + 236,
                             "D PAD : ARROW KEYS", MakeColor(188, 213, 242), 1);
                    DrawText(top_renderer, keyboard_overlay.x + 18, keyboard_overlay.y + 258,
                             "TOUCH : LEFT MOUSE CLICK ON BOTTOM SCREEN", MakeColor(188, 213, 242), 1);
                }
                SDL_RenderPresent(top_renderer);

                FillRect(bottom_renderer, SDL_Rect{0, 0, bottom_w, bottom_h}, MakeColor(8, 12, 20));
                SDL_Rect bottom_glow{bottom_layout.bottom_screen.x - 12, bottom_layout.bottom_screen.y - 12,
                                     bottom_layout.bottom_screen.w + 24, bottom_layout.bottom_screen.h + 24};
                FillRect(bottom_renderer, bottom_glow, MakeColor(22, 32, 49));
                SDL_RenderCopy(bottom_renderer, bottom_screen_texture, nullptr, &bottom_layout.bottom_screen);
                StrokeRect(bottom_renderer, bottom_glow, MakeColor(84, 124, 178));

                FillRect(bottom_renderer, bottom_layout.status_bar, MakeColor(18, 25, 36));
                StrokeRect(bottom_renderer, bottom_layout.status_bar, MakeColor(66, 95, 133));
                DrawText(bottom_renderer, bottom_layout.status_bar.x + 12, bottom_layout.status_bar.y + 10,
                         "BOTTOM SCREEN - LEFT CLICK TO TOUCH", MakeColor(200, 221, 246), 1);
                SDL_RenderPresent(bottom_renderer);

                if (mods_window_visible) {
                    FillRect(mod_renderer, SDL_Rect{0, 0, mod_w, mod_h}, MakeColor(12, 16, 25));
                    FillRect(mod_renderer, mod_layout.header_bar, MakeColor(20, 28, 41));
                    FillRect(mod_renderer, SDL_Rect{0, mod_layout.header_bar.h - 1, mod_w, 1}, MakeColor(82, 122, 174));
                    DrawText(mod_renderer, 18, 16, "MOD MANAGER", MakeColor(225, 238, 255), 2);
                    DrawText(mod_renderer, mod_w - 248, 22, "TOP BAR MODS TOGGLE", MakeColor(172, 198, 231), 1);

                    FillRect(mod_renderer, mod_layout.mod_list, MakeColor(24, 32, 47));
                    FillRect(mod_renderer, mod_layout.mod_details, MakeColor(24, 32, 47));
                    StrokeRect(mod_renderer, mod_layout.mod_list, MakeColor(66, 94, 132));
                    StrokeRect(mod_renderer, mod_layout.mod_details, MakeColor(66, 94, 132));

                    DrawText(mod_renderer, mod_layout.mod_list.x + 10, mod_layout.mod_list.y + 8,
                             "MOD LIST", MakeColor(220, 238, 255), 2);
                    DrawText(mod_renderer, mod_layout.mod_details.x + 10, mod_layout.mod_details.y + 8,
                             "DETAILS", MakeColor(220, 238, 255), 2);

                    if (!mods.empty()) {
                        selected_mod = std::clamp(selected_mod, 0, static_cast<int>(mods.size() - 1));
                        auto rows = BuildModListRows(mods, mod_layout.mod_list);

                        for (size_t i = 0; i < mods.size(); ++i) {
                            const auto& mod = mods[i];
                            const bool active = (static_cast<int>(i) == selected_mod);
                            FillRect(mod_renderer, rows[i].row,
                                     active ? MakeColor(52, 74, 106) : MakeColor(32, 42, 58));
                            StrokeRect(mod_renderer, rows[i].row,
                                       active ? MakeColor(115, 156, 214) : MakeColor(58, 76, 102));

                            if (!mod.dependencies.empty()) {
                                if (mod.expanded) {
                                    DrawArrowDown(mod_renderer,
                                                  rows[i].arrow.x + rows[i].arrow.w / 2,
                                                  rows[i].arrow.y + 2,
                                                  5,
                                                  MakeColor(201, 228, 255));
                                } else {
                                    DrawArrowRight(mod_renderer,
                                                   rows[i].arrow.x + 3,
                                                   rows[i].arrow.y + rows[i].arrow.h / 2,
                                                   5,
                                                   MakeColor(201, 228, 255));
                                }
                            }

                            DrawText(mod_renderer, rows[i].row.x + 28, rows[i].row.y + 9,
                                     mod.name, MakeColor(235, 244, 255), 2);
                            DrawText(mod_renderer, rows[i].row.x + 28, rows[i].row.y + 30,
                                     mod.enabled ? "STATUS: ENABLED" : "STATUS: DISABLED",
                                     mod.enabled ? MakeColor(150, 220, 180) : MakeColor(225, 155, 155), 1);

                            if (mod.expanded) {
                                for (size_t dep = 0; dep < rows[i].dependency_rows.size(); ++dep) {
                                    const SDL_Rect dep_row = rows[i].dependency_rows[dep];
                                    FillRect(mod_renderer, dep_row, MakeColor(24, 31, 43));
                                    DrawArrowDown(mod_renderer,
                                                  dep_row.x + 8,
                                                  dep_row.y + 5,
                                                  4,
                                                  MakeColor(160, 205, 255));
                                    DrawText(mod_renderer,
                                             dep_row.x + 18,
                                             dep_row.y + 6,
                                             mods[i].dependencies[dep],
                                             MakeColor(182, 212, 248),
                                             1);
                                }
                            }
                        }

                        const ModEntryUi& selected = mods[selected_mod];
                        DrawText(mod_renderer, mod_layout.mod_details.x + 14, mod_layout.mod_details.y + 38,
                                 selected.name, MakeColor(232, 243, 255), 2);
                        DrawText(mod_renderer, mod_layout.mod_details.x + 14, mod_layout.mod_details.y + 64,
                                 "AUTHOR: " + selected.author, MakeColor(173, 195, 224), 1);
                        DrawText(mod_renderer, mod_layout.mod_details.x + 14, mod_layout.mod_details.y + 84,
                                 selected.enabled ? "STATE: ENABLED" : "STATE: DISABLED",
                                 selected.enabled ? MakeColor(147, 219, 180) : MakeColor(225, 156, 156), 1);

                        DrawText(mod_renderer, mod_layout.mod_details.x + 14, mod_layout.mod_details.y + 104,
                                 "SUMMARY", MakeColor(220, 238, 255), 1);
                        DrawText(mod_renderer, mod_layout.mod_details.x + 14, mod_layout.mod_details.y + 120,
                                 selected.summary, MakeColor(180, 208, 238), 1);

                        DrawText(mod_renderer, mod_layout.mod_details.x + 14, mod_layout.mod_details.y + 140,
                                 "OPTIONS", MakeColor(220, 238, 255), 1);
                        auto option_rects = BuildModOptionToggleRects(mod_layout.mod_details, selected);
                        for (size_t i = 0; i < option_rects.size(); ++i) {
                            FillRect(mod_renderer, option_rects[i], MakeColor(42, 56, 78));
                            StrokeRect(mod_renderer, option_rects[i], MakeColor(98, 130, 170));
                            if (selected.options[i].enabled) {
                                DrawCheckMark(mod_renderer, option_rects[i], MakeColor(120, 228, 170));
                            }
                            DrawText(mod_renderer, option_rects[i].x + 30, option_rects[i].y + 5,
                                     selected.options[i].name, MakeColor(214, 231, 252), 1);
                        }

                        FillRect(mod_renderer, mod_layout.details_gear_button, MakeColor(40, 55, 77));
                        StrokeRect(mod_renderer, mod_layout.details_gear_button, MakeColor(104, 142, 188));
                        DrawGearIcon(mod_renderer, mod_layout.details_gear_button, MakeColor(207, 228, 255));
                        DrawText(mod_renderer,
                                 mod_layout.details_gear_button.x - 102,
                                 mod_layout.details_gear_button.y + 8,
                                 "MOD SETTINGS",
                                 MakeColor(201, 223, 248),
                                 1);

                        if (!selected.dependencies.empty()) {
                            int dep_y = mod_layout.mod_details.y + 280;
                            DrawText(mod_renderer, mod_layout.mod_details.x + 14, dep_y,
                                     "DEPENDENCIES", MakeColor(220, 238, 255), 1);
                            dep_y += 18;
                            for (const auto& dep : selected.dependencies) {
                                DrawArrowDown(mod_renderer,
                                              mod_layout.mod_details.x + 18,
                                              dep_y + 2,
                                              4,
                                              MakeColor(162, 206, 255));
                                DrawText(mod_renderer,
                                         mod_layout.mod_details.x + 28,
                                         dep_y + 2,
                                         dep,
                                         MakeColor(190, 214, 244),
                                         1);
                                dep_y += 18;
                            }
                        }

                        if (show_mod_settings) {
                            auto overlay_toggles = BuildModOptionToggleRects(mod_overlay, selected);

                            FillRect(mod_renderer, SDL_Rect{0, 0, mod_w, mod_h}, MakeColor(0, 0, 0, 120));
                            FillRect(mod_renderer, mod_overlay, MakeColor(27, 35, 49));
                            StrokeRect(mod_renderer, mod_overlay, MakeColor(104, 140, 187));
                            FillRect(mod_renderer, mod_overlay_close, MakeColor(115, 58, 58));
                            DrawText(mod_renderer, mod_overlay_close.x + 5, mod_overlay_close.y + 4,
                                     "X", MakeColor(245, 228, 228), 1);

                            DrawText(mod_renderer, mod_overlay.x + 18, mod_overlay.y + 16,
                                     "MOD SETTINGS", MakeColor(226, 240, 255), 2);
                            DrawText(mod_renderer, mod_overlay.x + 18, mod_overlay.y + 50,
                                     selected.name, MakeColor(206, 225, 248), 1);
                            DrawText(mod_renderer, mod_overlay.x + 18, mod_overlay.y + 72,
                                     "SAMPLE MOD CONTROL PANEL", MakeColor(166, 192, 223), 1);

                            for (size_t i = 0; i < overlay_toggles.size(); ++i) {
                                FillRect(mod_renderer, overlay_toggles[i], MakeColor(42, 56, 78));
                                StrokeRect(mod_renderer, overlay_toggles[i], MakeColor(98, 130, 170));
                                if (selected.options[i].enabled) {
                                    DrawCheckMark(mod_renderer, overlay_toggles[i], MakeColor(120, 228, 170));
                                }
                                DrawText(mod_renderer,
                                         overlay_toggles[i].x + 30,
                                         overlay_toggles[i].y + 5,
                                         selected.options[i].name,
                                         MakeColor(214, 231, 252),
                                         1);
                            }
                        }
                    }

                    SDL_RenderPresent(mod_renderer);
                }

                g_ui_present_count.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }
        } else {
            Log(LogLevel::Error,
                "Failed to initialize desktop windows/renderers/textures: " + std::string(SDL_GetError()));
            Log(LogLevel::Info, "Continuing in Headless mode (no window)...");
            while (g_running) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        if (top_screen_texture) SDL_DestroyTexture(top_screen_texture);
        if (bottom_screen_texture) SDL_DestroyTexture(bottom_screen_texture);
        if (top_renderer) SDL_DestroyRenderer(top_renderer);
        if (bottom_renderer) SDL_DestroyRenderer(bottom_renderer);
        if (mod_renderer) SDL_DestroyRenderer(mod_renderer);
        if (top_window) SDL_DestroyWindow(top_window);
        if (bottom_window) SDL_DestroyWindow(bottom_window);
        if (mod_window) SDL_DestroyWindow(mod_window);
        SDL_Quit();
    }
    
    g_memory.GetAudioManager().Shutdown();

    arm9_thread.join();
    arm7_thread.join();
    timing_thread.join();
    if (watchdog_thread.joinable()) {
        watchdog_thread.join();
    }

    Log(LogLevel::Info, "Runtime engine shutdown cleanly.");
    return 0;
}
