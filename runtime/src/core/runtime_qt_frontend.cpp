#include "runtime_qt_frontend.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QDockWidget>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

#include "memory_map.h"
#include "sw_renderer.h"

namespace {
constexpr int kDSWidth = 256;
constexpr int kDSHeight = 192;

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

const char* QtBindingNameForKey(int key) {
    switch (key) {
        case Qt::Key_Space: return "A";
        case Qt::Key_Z: return "B";
        case Qt::Key_Shift: return "Select";
        case Qt::Key_Return:
        case Qt::Key_Enter: return "Start";
        case Qt::Key_Right: return "Right";
        case Qt::Key_Left: return "Left";
        case Qt::Key_Up: return "Up";
        case Qt::Key_Down: return "Down";
        case Qt::Key_S: return "R";
        case Qt::Key_A: return "L";
        case Qt::Key_X: return "X";
        case Qt::Key_C: return "Y";
        default: return nullptr;
    }
}

class QtScreenView final : public QWidget {
public:
    QtScreenView(SoftwareRenderer* renderer,
                 NDSMemory* memory,
                 UiSnapshotCallback snapshotCallback,
                 QWidget* parent = nullptr)
        : QWidget(parent),
          m_renderer(renderer),
          m_memory(memory),
          m_snapshotCallback(std::move(snapshotCallback)) {
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        setAutoFillBackground(false);
    }

    void RefreshFrames() {
        if (m_renderer == nullptr) {
            return;
        }

        std::array<uint32_t, kDSWidth * kDSHeight> top_pixels{};
        std::array<uint32_t, kDSWidth * kDSHeight> bottom_pixels{};
        m_renderer->CopyFramebuffers(top_pixels, bottom_pixels);

        m_topImage = QImage(reinterpret_cast<const uchar*>(top_pixels.data()),
                            kDSWidth,
                            kDSHeight,
                            static_cast<int>(kDSWidth * sizeof(uint32_t)),
                            QImage::Format_RGBA8888).copy();
        m_bottomImage = QImage(reinterpret_cast<const uchar*>(bottom_pixels.data()),
                               kDSWidth,
                               kDSHeight,
                               static_cast<int>(kDSWidth * sizeof(uint32_t)),
                               QImage::Format_RGBA8888).copy();

        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), QColor(13, 18, 28));

        const int margin = 24;
        const int gap = 18;
        const QRect content = rect().adjusted(margin, margin, -margin, -margin);
        const int stackHeight = std::max(0, content.height() - gap);
        const int halfHeight = stackHeight / 2;

        const int widthFromHeight = static_cast<int>(std::lround(halfHeight * (static_cast<double>(kDSWidth) / kDSHeight)));
        const int screenWidth = std::min(content.width(), widthFromHeight);
        const int screenHeight = static_cast<int>(std::lround(screenWidth * (static_cast<double>(kDSHeight) / kDSWidth)));

        const int topX = content.x() + (content.width() - screenWidth) / 2;
        const int topY = content.y() + (halfHeight - screenHeight) / 2;
        const int bottomY = content.y() + halfHeight + gap + (halfHeight - screenHeight) / 2;

        m_topRect = QRect(topX, topY, screenWidth, screenHeight);
        m_bottomRect = QRect(topX, bottomY, screenWidth, screenHeight);

        painter.setPen(QColor(96, 132, 182));
        painter.setBrush(QColor(22, 31, 46));
        painter.drawRoundedRect(m_topRect.adjusted(-8, -8, 8, 8), 12, 12);
        painter.drawRoundedRect(m_bottomRect.adjusted(-8, -8, 8, 8), 12, 12);

        if (!m_topImage.isNull()) {
            painter.drawImage(m_topRect, m_topImage);
        } else {
            painter.fillRect(m_topRect, QColor(4, 7, 12));
        }

        if (!m_bottomImage.isNull()) {
            painter.drawImage(m_bottomRect, m_bottomImage);
        } else {
            painter.fillRect(m_bottomRect, QColor(4, 7, 12));
        }

        painter.setPen(QColor(224, 236, 255));
        painter.drawText(m_topRect.adjusted(0, -26, 0, -6), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("TOP SCREEN"));
        painter.drawText(m_bottomRect.adjusted(0, -26, 0, -6), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("BOTTOM SCREEN"));

        const float scale = (m_bottomRect.width() > 0)
            ? (static_cast<float>(m_bottomRect.width()) / static_cast<float>(kDSWidth))
            : 1.0f;
        m_memory->GetInputManager().SetDisplayScale(scale, m_bottomRect.x(), m_bottomRect.y());
    }

    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        update();
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->isAutoRepeat()) {
            event->ignore();
            return;
        }

        if (event->key() == Qt::Key_F1 && m_snapshotCallback) {
            m_snapshotCallback("Manual debug snapshot (F1)");
        }

        const char* binding = QtBindingNameForKey(event->key());
        if (binding != nullptr) {
            m_memory->GetInputManager().UpdateVirtualButton(binding, true);
            event->accept();
            return;
        }

        QWidget::keyPressEvent(event);
    }

    void keyReleaseEvent(QKeyEvent* event) override {
        if (event->isAutoRepeat()) {
            event->ignore();
            return;
        }

        const char* binding = QtBindingNameForKey(event->key());
        if (binding != nullptr) {
            m_memory->GetInputManager().UpdateVirtualButton(binding, false);
            event->accept();
            return;
        }

        QWidget::keyReleaseEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(event);
            return;
        }

        if (m_bottomRect.contains(event->pos())) {
            m_touchActive = true;
            m_memory->GetInputManager().HandleTouchPoint(event->pos().x(), event->pos().y(), true);
            event->accept();
            return;
        }

        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (!m_touchActive) {
            QWidget::mouseMoveEvent(event);
            return;
        }

        m_memory->GetInputManager().HandleTouchPoint(event->pos().x(), event->pos().y(), true);
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton) {
            QWidget::mouseReleaseEvent(event);
            return;
        }

        if (m_touchActive) {
            m_memory->GetInputManager().HandleTouchPoint(event->pos().x(), event->pos().y(), false);
            m_touchActive = false;
            event->accept();
            return;
        }

        QWidget::mouseReleaseEvent(event);
    }

private:
    SoftwareRenderer* m_renderer = nullptr;
    NDSMemory* m_memory = nullptr;
    UiSnapshotCallback m_snapshotCallback;
    QImage m_topImage;
    QImage m_bottomImage;
    QRect m_topRect;
    QRect m_bottomRect;
    bool m_touchActive = false;
};

class QtRuntimeWindow final : public QMainWindow {
public:
    QtRuntimeWindow(SoftwareRenderer* renderer,
                    NDSMemory* memory,
                    std::atomic<bool>* running,
                    std::atomic<uint64_t>* uiPresentCount,
                    UiSnapshotCallback snapshotCallback,
                    QWidget* parent = nullptr)
        : QMainWindow(parent),
          m_renderer(renderer),
          m_memory(memory),
          m_running(running),
          m_uiPresentCount(uiPresentCount),
          m_snapshotCallback(std::move(snapshotCallback)) {
        setWindowTitle(QStringLiteral("KH Re:coded - Qt Frontend"));
        setMinimumSize(1120, 980);

        m_screenView = new QtScreenView(m_renderer, m_memory, m_snapshotCallback, this);
        setCentralWidget(m_screenView);

        createMenus();
        createModsDock();
        createKeyboardDialog();

        auto* refreshTimer = new QTimer(this);
        connect(refreshTimer, &QTimer::timeout, this, [this]() {
            if (m_screenView != nullptr) {
                m_screenView->RefreshFrames();
            }
            m_uiPresentCount->fetch_add(1, std::memory_order_relaxed);
        });
        refreshTimer->start(16);

        statusBar()->showMessage(QStringLiteral("Qt native shell active"));
        m_screenView->setFocus();
        rebuildModDetails();
    }

protected:
    void closeEvent(QCloseEvent* event) override {
        m_running->store(false, std::memory_order_relaxed);
        QMainWindow::closeEvent(event);
    }

private:
    void createMenus() {
        QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
        QAction* exitAction = fileMenu->addAction(QStringLiteral("E&xit"));
        connect(exitAction, &QAction::triggered, this, &QWidget::close);

        QMenu* modsMenu = menuBar()->addMenu(QStringLiteral("&Mods"));
        QAction* toggleModsAction = modsMenu->addAction(QStringLiteral("Show &Mod Manager"));
        toggleModsAction->setCheckable(true);
        toggleModsAction->setChecked(true);
        connect(toggleModsAction, &QAction::triggered, this, [this, toggleModsAction](bool checked) {
            if (m_modDock != nullptr) {
                m_modDock->setVisible(checked);
            }
            toggleModsAction->setChecked(checked);
        });

        QMenu* optionsMenu = menuBar()->addMenu(QStringLiteral("&Options"));
        QAction* keyboardAction = optionsMenu->addAction(QStringLiteral("&Keyboard Settings"));
        connect(keyboardAction, &QAction::triggered, this, [this]() { showKeyboardDialog(); });

        QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
        QAction* showStatusAction = viewMenu->addAction(QStringLiteral("Show &Status Bar"));
        showStatusAction->setCheckable(true);
        showStatusAction->setChecked(true);
        connect(showStatusAction, &QAction::triggered, this, [this](bool checked) {
            if (statusBar() != nullptr) {
                statusBar()->setVisible(checked);
            }
        });

        QMenu* toolsMenu = menuBar()->addMenu(QStringLiteral("&Tools"));
        QAction* snapshotAction = toolsMenu->addAction(QStringLiteral("Manual Debug Snapshot"));
        connect(snapshotAction, &QAction::triggered, this, [this]() {
            if (m_snapshotCallback) {
                m_snapshotCallback("Manual debug snapshot (menu)");
            }
        });
    }

    void createModsDock() {
        m_modDock = new QDockWidget(QStringLiteral("Mod Manager"), this);
        m_modDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        m_modDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);

        auto* container = new QWidget(m_modDock);
        auto* layout = new QVBoxLayout(container);
        layout->setContentsMargins(12, 12, 12, 12);
        layout->setSpacing(8);

        m_modList = new QListWidget(container);
        m_modDetails = new QLabel(container);
        m_modDetails->setWordWrap(true);
        m_modDetails->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        m_modDetails->setMinimumHeight(200);

        layout->addWidget(m_modList, 2);
        layout->addWidget(m_modDetails, 1);
        container->setLayout(layout);
        m_modDock->setWidget(container);
        addDockWidget(Qt::RightDockWidgetArea, m_modDock);

        m_mods = CreateSampleMods();
        for (size_t i = 0; i < m_mods.size(); ++i) {
            const ModEntryUi& mod = m_mods[i];
            auto* item = new QListWidgetItem(QString::fromStdString(mod.name), m_modList);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            item->setCheckState(mod.enabled ? Qt::Checked : Qt::Unchecked);
            item->setData(Qt::UserRole, static_cast<int>(i));
        }

        connect(m_modList, &QListWidget::currentRowChanged, this, [this](int row) {
            if (row >= 0 && row < static_cast<int>(m_mods.size())) {
                m_selectedMod = row;
                rebuildModDetails();
            }
        });

        connect(m_modList, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
            const int row = item->data(Qt::UserRole).toInt();
            if (row >= 0 && row < static_cast<int>(m_mods.size())) {
                m_mods[static_cast<size_t>(row)].enabled = (item->checkState() == Qt::Checked);
            }
        });

        if (m_modList->count() > 0) {
            m_modList->setCurrentRow(0);
        }
    }

    void createKeyboardDialog() {
        m_keyboardDialog = new QDialog(this);
        m_keyboardDialog->setWindowTitle(QStringLiteral("Keyboard Settings"));
        m_keyboardDialog->setModal(false);
        m_keyboardDialog->resize(520, 360);

        auto* layout = new QVBoxLayout(m_keyboardDialog);
        auto* label = new QLabel(QStringLiteral(
            "Native keyboard mapping\n\n"
            "A: Space\n"
            "B: Z\n"
            "Select: Shift\n"
            "Start: Enter\n"
            "D-Pad: Arrow keys\n"
            "R: S\n"
            "L: A\n"
            "X: X\n"
            "Y: C\n\n"
            "Touch the bottom screen with the left mouse button."),
            m_keyboardDialog);
        label->setWordWrap(true);
        layout->addWidget(label);

        auto* closeButton = new QPushButton(QStringLiteral("Close"), m_keyboardDialog);
        layout->addWidget(closeButton);
        connect(closeButton, &QPushButton::clicked, m_keyboardDialog, &QDialog::hide);
    }

    void showKeyboardDialog() {
        if (m_keyboardDialog != nullptr) {
            m_keyboardDialog->show();
            m_keyboardDialog->raise();
            m_keyboardDialog->activateWindow();
        }
    }

    void rebuildModDetails() {
        if (m_modDetails == nullptr || m_selectedMod < 0 || m_selectedMod >= static_cast<int>(m_mods.size())) {
            return;
        }

        const ModEntryUi& mod = m_mods[static_cast<size_t>(m_selectedMod)];
        QString text;
        text += QStringLiteral("<b>") + QString::fromStdString(mod.name) + QStringLiteral("</b><br>");
        text += QStringLiteral("Author: ") + QString::fromStdString(mod.author) + QStringLiteral("<br>");
        text += QStringLiteral("Enabled: ") + QString::fromLatin1(mod.enabled ? "Yes" : "No") + QStringLiteral("<br><br>");
        text += QString::fromStdString(mod.summary) + QStringLiteral("<br><br>");

        if (!mod.dependencies.empty()) {
            text += QStringLiteral("Dependencies:<br>");
            for (const std::string& dependency : mod.dependencies) {
                text += QStringLiteral("- ") + QString::fromStdString(dependency) + QStringLiteral("<br>");
            }
            text += QStringLiteral("<br>");
        }

        if (!mod.options.empty()) {
            text += QStringLiteral("Options:<br>");
            for (const ModOptionUi& option : mod.options) {
                text += QStringLiteral("- ") + QString::fromStdString(option.name) + QStringLiteral(": ") +
                        QString::fromLatin1(option.enabled ? "On" : "Off") + QStringLiteral("<br>");
            }
        }

        m_modDetails->setText(text);
    }

    SoftwareRenderer* m_renderer = nullptr;
    NDSMemory* m_memory = nullptr;
    std::atomic<bool>* m_running = nullptr;
    std::atomic<uint64_t>* m_uiPresentCount = nullptr;
    UiSnapshotCallback m_snapshotCallback;
    QtScreenView* m_screenView = nullptr;
    QDockWidget* m_modDock = nullptr;
    QListWidget* m_modList = nullptr;
    QLabel* m_modDetails = nullptr;
    QDialog* m_keyboardDialog = nullptr;
    std::vector<ModEntryUi> m_mods;
    int m_selectedMod = -1;
};

}  // namespace

int RunQtFrontend(int argc,
                  char* argv[],
                  SoftwareRenderer* renderer,
                  NDSMemory* memory,
                  std::atomic<bool>* running,
                  std::atomic<uint64_t>* uiPresentCount,
                  UiSnapshotCallback snapshotCallback) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("KH Re:coded Recomp"));

    QtRuntimeWindow window(renderer, memory, running, uiPresentCount, std::move(snapshotCallback));
    window.show();
    return app.exec();
}
