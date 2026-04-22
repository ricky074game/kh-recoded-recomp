# Qt6 Migration and Native Menu Bar Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Migrate the runtime from SDL2 to Qt6 to provide a native desktop experience with a real OS menu bar and native dialogs, using vcpkg for self-contained dependency management.

**Architecture:** Replace the SDL2 event loop and rendering bridge with a Qt `QMainWindow`. The DS screens will be rendered as Qt widgets. The software rasterizer will output to raw buffers converted to `QImage`. Vulkan will integrate via `QVulkanWindow`.

**Tech Stack:** C++20, Qt6 (Widgets), Vulkan, vcpkg, CMake.

---

### Task 0: Clone vcpkg

**Files:**
- Create: `vcpkg/` (directory)

- [ ] **Step 1: Clone vcpkg into the root**
```bash
git clone https://github.com/microsoft/vcpkg.git vcpkg
```

- [ ] **Step 2: Bootstrap vcpkg**
```bash
./vcpkg/bootstrap-vcpkg.sh
```

- [ ] **Step 3: Commit**
```bash
git add vcpkg
git commit -m "build: add vcpkg as a dependency manager"
```

---

### Task 1: Initialize vcpkg and Qt6 Dependency

**Files:**
- Create: `vcpkg.json`
- Modify: `CMakeLists.txt`
- Modify: `runtime/CMakeLists.txt`

- [ ] **Step 1: Create vcpkg.json at the root**
```json
{
  "name": "kh-recoded-recomp",
  "version": "1.0.0",
  "dependencies": [
    "qtbase",
    "capstone",
    "gtest"
  ]
}
```

- [ ] **Step 2: Update root CMakeLists.txt to use vcpkg**
```cmake
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
project(kh_recoded_recomp)
```

- [ ] **Step 3: Update runtime/CMakeLists.txt to find Qt6 instead of SDL2**
Remove SDL2 blocks and add:
```cmake
find_package(Qt6 REQUIRED COMPONENTS Widgets Gui Core)
# ... update targets to link against Qt6::Widgets Qt6::Gui Qt6::Core
```

- [ ] **Step 4: Commit**
```bash
git add vcpkg.json CMakeLists.txt runtime/CMakeLists.txt
git commit -m "build: initialize vcpkg and switch dependencies to Qt6"
```

---

### Task 2: Refactor InputManager to Qt

**Files:**
- Modify: `runtime/src/hw/hw_input.h`
- Modify: `runtime/src/hw/hw_input.cpp`

- [ ] **Step 1: Replace SDL includes and types in hw_input.h**
Replace `#include <SDL2/SDL.h>` with `#include <QKeyEvent>` and update method signatures.

- [ ] **Step 2: Update mappings in hw_input.cpp**
Replace `SDL_SCANCODE_*` with `Qt::Key_*`.

- [ ] **Step 3: Commit**
```bash
git add runtime/src/hw/hw_input.h runtime/src/hw/hw_input.cpp
git commit -m "refactor: migrate InputManager from SDL to Qt"
```

---

### Task 3: Decouple Renderer from SDL

**Files:**
- Modify: `runtime/src/backends/sdl_renderer.h` (rename to `sw_renderer.h`)
- Modify: `runtime/src/backends/sdl_renderer.cpp` (rename to `sw_renderer.cpp`)

- [ ] **Step 1: Remove SDL dependencies from Software Renderer**
Change `SDL_Texture*` to `std::vector<uint32_t>` or raw buffers. Remove `Present` method that used `SDL_RenderCopy`.

- [ ] **Step 2: Update CMakeLists.txt with renamed files**

- [ ] **Step 3: Commit**
```bash
git add runtime/src/backends/sw_renderer.h runtime/src/backends/sw_renderer.cpp
git commit -m "refactor: decouple software renderer from SDL"
```

---

### Task 4: Implement Qt Main Window and Event Loop

**Files:**
- Modify: `runtime/src/core/main.cpp`

- [ ] **Step 1: Replace main loop with QApplication::exec()**
Remove `SDL_Init`, `SDL_CreateWindow`, and the `while(g_running)` poll loop.

- [ ] **Step 2: Create MainWindow class**
Define `MainWindow : public QMainWindow` with `QTimer` for frame updates and event overrides for input.

- [ ] **Step 3: Implement QMenuBar**
Add `File`, `Options`, `Mods` menus and connect them to slots.

- [ ] **Step 4: Commit**
```bash
git add runtime/src/core/main.cpp
git commit -m "feat: implement Qt MainWindow and native Menu Bar"
```

---

### Task 5: Migrate Overlays to Native Dialogs

**Files:**
- Modify: `runtime/src/core/main.cpp` (extract to new files if needed)

- [ ] **Step 1: Create KeyboardSettingsDialog**
Use `QDialog` with `QTableWidget` or labels for key config.

- [ ] **Step 2: Create ModManagerDialog**
Use `QDialog` with `QTreeWidget` for mod list and dependencies.

- [ ] **Step 3: Commit**
```bash
git add runtime/src/core/main.cpp
git commit -m "feat: replace custom overlays with native Qt dialogs"
```
