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

#include <QApplication>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QTimer>
#include <QLabel>
#include <QVBoxLayout>
#include <QDialog>
#include <QDialogButtonBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QTreeWidget>
#include <QImage>
#include <QPixmap>
#include <QKeyEvent>
#include <QMouseEvent>

#include "memory_map.h"
#include "cpu_context.h"
#include "hw_irq.h"
#include "vfs.h"
#include "ds_debug.h"
#include "hw_overlay.h"
#include "sw_renderer.h"
#include "hw_input.h"
#include "hw_audio.h"

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
    if (!value || *value == '\0') return fallback;
    char* end = nullptr;
    long parsed = std::strtol(value, &end, 10);
    if (end == value || (end && *end != '\0')) return fallback;
    if (parsed < min_val) parsed = min_val;
    if (parsed > max_val) parsed = max_val;
    return static_cast<int>(parsed);
}

static bool ReadEnvBool(const char* name, bool fallback) {
    const char* value = std::getenv(name);
    if (!value || *value == '\0') return fallback;
    std::string lower(value);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "1" || lower == "true" || lower == "yes" || lower == "on") return true;
    if (lower == "0" || lower == "false" || lower == "no" || lower == "off") return false;
    return fallback;
}

void PrintTrace(CPU_Context* ctx) {
    if (!ctx) return;
    std::cout << "Execution Trace (Ring Buffer, last 10):\n";
    uint8_t curr = ctx->trace_idx;
    for (int i = 0; i < 10; ++i) {
        uint8_t idx = (curr - 1 - i) % 256;
        uint32_t addr = ctx->trace_buffer[idx];
        if (addr == 0 && i > 0) break; 
        if (i == 0) std::cout << " -> 0x" << std::hex << std::setw(8) << std::setfill('0') << addr << " (Instruction that crashed)\n";
        else std::cout << "    0x" << std::hex << std::setw(8) << std::setfill('0') << addr << "\n";
    }
}

static void CrashHandler(int signum) {
    std::cout << "\n==================================================\n";
    std::cout << "  FATAL ERROR: Signal " << signum << " caught!\n";
    std::cout << "==================================================\n";
    PrintTrace(g_arm9_ctx.load(std::memory_order_acquire));
    std::exit(signum);
}

static void RunARM9() {
    CPU_Context ctx;
    ctx.InitializeNDS9();
    ctx.mem = &g_memory;
    ctx.running_flag = &g_running;
    g_arm9_ctx.store(&ctx, std::memory_order_release);

    std::ifstream arm9_file("recoded/arm9.bin", std::ios::binary);
    if (arm9_file.is_open()) {
        arm9_file.read(reinterpret_cast<char*>(g_memory.GetMainRAM()), g_memory.GetMainRAMSize());
        Log(LogLevel::Info, "ARM9 binary loaded.");
    }
    g_memory.GetOverlayManager().LoadY9("recoded/y9.bin");
    
    try {
        g_memory.GetOverlayManager().ExecuteDynamicBranch(&ctx, 0x02000800);
    } catch (const std::exception& e) {
        Log(LogLevel::Error, std::string("ARM9 Crash: ") + e.what());
    }
    g_running.store(false);
}

static void RunARM7() {
    CPU_Context ctx;
    ctx.InitializeNDS7();
    ctx.mem = &g_memory;
    while (g_running) {
        CheckInterrupts(&ctx, g_memory.irq_arm7);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

static void RunTimingThread() {
    auto next_vblank = std::chrono::high_resolution_clock::now();
    const auto vblank_interval = std::chrono::nanoseconds(16715000);
    while (g_running) {
        auto now = std::chrono::high_resolution_clock::now();
        if (now >= next_vblank) {
            g_memory.irq_arm9.RaiseIRQ(IRQBits::VBlank);
            g_memory.irq_arm7.RaiseIRQ(IRQBits::VBlank);
            g_vblank_count.fetch_add(1);
            
            g_memory.GetEngine2DA().SubmitFrame(g_memory.GetOAM(), g_memory.GetVRAM(), g_memory.GetVRAMSize(), g_memory.GetPaletteRAM(), g_memory.GetPaletteRAMSize());
            g_memory.GetEngine2DB().SubmitFrame(g_memory.GetOAM(), g_memory.GetVRAM(), g_memory.GetVRAMSize(), g_memory.GetPaletteRAM(), g_memory.GetPaletteRAMSize());
            
            next_vblank += vblank_interval;
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
}

class ScreenWidget : public QLabel {
public:
    ScreenWidget(QWidget* parent = nullptr) : QLabel(parent) {
        setFixedSize(256 * 2, 192 * 2);
        setStyleSheet("background-color: black; border: 2px solid #527AAE;");
    }
    void UpdatePixels(const uint32_t* pixels) {
        QImage img((const uchar*)pixels, 256, 192, QImage::Format_RGBA8888);
        setPixmap(QPixmap::fromImage(img.scaled(size(), Qt::KeepAspectRatio)));
    }
};

class KeyboardSettingsDialog : public QDialog {
public:
    explicit KeyboardSettingsDialog(QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle("Keyboard Settings");

        auto* layout = new QVBoxLayout(this);
        auto* table = new QTableWidget(12, 2, this);
        table->setHorizontalHeaderLabels({"Action", "Binding"});
        table->verticalHeader()->setVisible(false);
        table->horizontalHeader()->setStretchLastSection(true);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);

        const std::pair<const char*, const char*> bindings[] = {
            {"A", "Space"},
            {"B", "Z"},
            {"Select", "Shift"},
            {"Start", "Enter"},
            {"Right", "Right Arrow"},
            {"Left", "Left Arrow"},
            {"Up", "Up Arrow"},
            {"Down", "Down Arrow"},
            {"R", "S"},
            {"L", "A"},
            {"X", "X"},
            {"Y", "C"},
        };

        for (int row = 0; row < 12; ++row) {
            table->setItem(row, 0, new QTableWidgetItem(bindings[row].first));
            table->setItem(row, 1, new QTableWidgetItem(bindings[row].second));
        }

        layout->addWidget(table);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);
        resize(360, 360);
    }
};

class ModManagerDialog : public QDialog {
public:
    explicit ModManagerDialog(QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle("Mod Manager");

        auto* layout = new QVBoxLayout(this);
        auto* tree = new QTreeWidget(this);
        tree->setColumnCount(3);
        tree->setHeaderLabels({"Mod", "Status", "Dependencies"});
        tree->header()->setStretchLastSection(true);

        auto* root = new QTreeWidgetItem({"Built-in Assets", "Enabled", "None"});
        new QTreeWidgetItem(root, {"Runtime overlays", "Enabled", "Base game"});
        new QTreeWidgetItem(root, {"UI skins", "Disabled", "Base game"});
        root->setExpanded(true);
        tree->addTopLevelItem(root);

        layout->addWidget(tree);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);
        resize(520, 360);
    }
};

class MainWindow : public QMainWindow {
private:
    ScreenWidget* top_screen;
    ScreenWidget* bottom_screen;
    SoftwareRenderer ds_renderer;
    QTimer* timer;

public:
    MainWindow() {
        setWindowTitle("Kingdom Hearts Re:coded — Static Recompilation");
        
        QWidget* central = new QWidget(this);
        QVBoxLayout* layout = new QVBoxLayout(central);
        
        top_screen = new ScreenWidget(central);
        bottom_screen = new ScreenWidget(central);
        
        layout->addWidget(new QLabel("TOP SCREEN", central));
        layout->addWidget(top_screen);
        layout->addWidget(new QLabel("BOTTOM SCREEN (TOUCH)", central));
        layout->addWidget(bottom_screen);
        
        setCentralWidget(central);
        
        CreateMenus();
        
        g_memory.GetGXEngine().renderer = &ds_renderer;
        g_memory.GetEngine2DA().renderer = &ds_renderer;
        g_memory.GetEngine2DB().renderer = &ds_renderer;
        
        timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, &MainWindow::UpdateFrame);
        timer->start(16);
    }

    void CreateMenus() {
        QMenuBar* bar = menuBar();
        
        QMenu* fileMenu = bar->addMenu("&File");
        QAction* exitAction = fileMenu->addAction("&Exit");
        connect(exitAction, &QAction::triggered, this, &MainWindow::close);
        
        QMenu* optionsMenu = bar->addMenu("&Options");
        optionsMenu->addAction("&Keyboard Settings", this, [this](){
            KeyboardSettingsDialog dialog(this);
            dialog.exec();
        });
        
        QMenu* modsMenu = bar->addMenu("&Mods");
        modsMenu->addAction("&Mod Manager", this, [this](){
            ModManagerDialog dialog(this);
            dialog.exec();
        });
    }

    void UpdateFrame() {
        if (!g_running) {
            close();
            return;
        }
        
        std::array<uint32_t, 256 * 192> top_pixels;
        std::array<uint32_t, 256 * 192> bottom_pixels;
        ds_renderer.CopyFramebuffers(top_pixels, bottom_pixels);
        
        top_screen->UpdatePixels(top_pixels.data());
        bottom_screen->UpdatePixels(bottom_pixels.data());
        
        g_ui_present_count.fetch_add(1);
    }

protected:
    void keyPressEvent(QKeyEvent* event) override {
        g_memory.GetInputManager().HandleKeyEvent(event, true);
    }
    void keyReleaseEvent(QKeyEvent* event) override {
        g_memory.GetInputManager().HandleKeyEvent(event, false);
    }
    void mousePressEvent(QMouseEvent* event) override {
        UpdateTouch(event, true);
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        UpdateTouch(event, false);
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        UpdateTouch(event, event->buttons() & Qt::LeftButton);
    }

    void UpdateTouch(QMouseEvent* event, bool pressed) {
        QPoint pos = bottom_screen->mapFrom(this, event->pos());
        if (pos.x() >= 0 && pos.x() < bottom_screen->width() &&
            pos.y() >= 0 && pos.y() < bottom_screen->height()) {
            
            g_memory.GetInputManager().SetDisplayScale(
                (float)bottom_screen->width() / 256.0f, 
                bottom_screen->mapTo(this, QPoint(0,0)).x(),
                bottom_screen->mapTo(this, QPoint(0,0)).y()
            );
            
            if (event->type() == QEvent::MouseMove) {
                g_memory.GetInputManager().HandleMouseMotion(event);
            } else {
                g_memory.GetInputManager().HandleMouseEvent(event, pressed);
            }
        }
    }
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    std::signal(SIGSEGV, CrashHandler);
    std::signal(SIGABRT, CrashHandler);

    Log(LogLevel::Info, "Initializing Virtual DS Motherboard...");
    
    std::thread arm9_thread(RunARM9);
    std::thread arm7_thread(RunARM7);
    std::thread timing_thread(RunTimingThread);

    g_memory.GetAudioManager().Initialize();
    g_memory.GetInputManager().LoadConfig("bindings.ini");

    MainWindow win;
    win.show();

    int result = app.exec();

    g_running.store(false);
    g_memory.GetAudioManager().Shutdown();

    arm9_thread.join();
    arm7_thread.join();
    timing_thread.join();

    return result;
}
