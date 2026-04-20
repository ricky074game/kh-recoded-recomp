# Kingdom Hearts Re:coded — Static Recompilation

A clean-room static recompilation of *Kingdom Hearts Re:coded* (Nintendo DS) into native C++ targeting modern PC hardware (Vulkan, SDL2, Miniaudio).

## Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│                        Build Pipeline                           │
│  User provides .nds → ndstool extracts → Lifter generates C++   │
│  → Compiler builds native .exe                                  │
└──────────────┬──────────────────────────────────────┬────────────┘
               │                                      │
      ┌────────▼────────┐                    ┌────────▼────────┐
      │     Lifter       │                    │    Runtime       │
      │ (Offline Tool)   │                    │ (Execution Eng.) │
      │                  │                    │                  │
      │ • Capstone ARM   │       emits        │ • NDSMemory      │
      │ • Control Flow   │───────C++──────────│ • CPU_Context    │
      │ • C++ Emitter    │                    │ • HW Subsystems  │
      │ • Flag Deferral  │                    │ • VFS            │
      └──────────────────┘                    │ • ARM9/7 Threads │
                                              │ • Timing/IRQ     │
                                              └────────┬─────────┘
                                                       │
                                              ┌────────▼─────────┐
                                              │     Mod API       │
                                              │ • HookRegistry    │
                                              │ • Pre/Post Hooks  │
                                              │ • DLL/SO Loading  │
                                              └──────────────────┘
```

## Project Structure

| Directory   | Purpose                                                        |
|-------------|----------------------------------------------------------------|
| `lifter/`   | Offline ARM→C++ binary translator (Capstone-based)             |
| `runtime/`  | Native execution engine ("Virtual DS Motherboard")             |
| `mod_api/`  | C++ hooking API for runtime modification by external mods      |
| `recoded/`  | Extracted game assets and binaries (user-supplied)             |
| `tools/`    | Build utilities (ndstool, miniaudio)                          |

## Building

### Required Tools

To build the static recompilation on your local machine, you must have the following tools installed:
- **CMake** (v3.20 or newer)
- **C++20 Compiler** (GCC 10+, Clang 12+, or MSVC 2019+)
- **Python 3** (Optional, for advanced lifter scripts)
- **Git** (for fetching Capstone and Google Test dependencies)
- **A legally dumped `.nds` ROM** of *Kingdom Hearts Re:coded*

### Step-by-Step Build Instructions

This project uses an automated build orchestrator to extract your ROM, run the Capstone lifter, and compile the final executable without hosting any proprietary assets.

#### Linux & macOS
1. Open a terminal and clone the repository.
2. Place your legally dumped ROM in the project root (e.g., `recoded.nds`).
3. Run the setup script:
   ```bash
   chmod +x ./setup.sh && ./setup.sh recoded.nds
   ```
   Optional debug build:
   ```bash
   ./setup.sh -d recoded.nds
   ```
4. The script will output the compiled executable in the `build/` directory.

#### Windows
1. Open PowerShell or Command Prompt and clone the repository.
2. Place your legally dumped ROM in the project root (e.g., `recoded.nds`).
3. Run the setup batch script:
   ```cmd
   setup.bat recoded.nds
   ```
4. The script will output the compiled executable in the `build/` directory.

### Manual Build Commands

If you prefer to build the project manually after extracting the ROM:

```bash
# Configure + build all targets
cmake -S . -B build && cmake --build build -j"$(nproc)"

# Runtime tests
./build/runtime/runtime_tests --gtest_color=no

# Lifter tests
cmake -S lifter -B build_lifter && cmake --build build_lifter -j"$(nproc)" && ctest --test-dir build_lifter --output-on-failure
```

### Build Targets

| Target            | Description                              |
|-------------------|------------------------------------------|
| `runtime_engine`  | Main runtime target (outputs `recoded`) |
| `runtime_tests`   | Google Test suite for the runtime        |
| `lifter_engine`   | Offline ARM→C++ translator CLI tool      |
| `lifter_tests`    | Google Test suite for the lifter         |
| `mod_api`         | Static library for mod hooking           |

## Running

```bash
# Run the runtime engine (default data dir: recoded/data)
./build/runtime/recoded

# Or specify a custom data directory
./build/runtime/recoded /path/to/extracted/data

# Run the lifter on an ARM9 binary
./build/lifter/lifter_engine recoded/arm9.bin output.cpp 0x02000000
```

### Runtime Debugging (Black Screen / Stalls)

If the game window opens but appears stuck, run the runtime with the watchdog enabled:

```bash
KH_DEBUG_WATCHDOG=1 KH_DEBUG_WATCHDOG_POLL_MS=300 ./build/runtime/recoded recoded/data
```

By default, this does **not** spam per-instruction trace lines.

If you explicitly want instruction spam, add the extra runtime argument `superdebug`:

```bash
KH_DEBUG_WATCHDOG=1 ./build/runtime/recoded recoded/data superdebug
```

Press `F1` in the runtime window to print a manual debug snapshot.

Useful watchdog env vars:
- `KH_DEBUG_WATCHDOG=1`: enable watchdog thread logs.
- `KH_DEBUG_WATCHDOG_POLL_MS=300`: polling period for progress checks.
- `KH_DEBUG_WATCHDOG_STALL_MS=3000`: report if ARM9 dispatch has no progress for this duration.
- `KH_DEBUG_HEARTBEAT_MS=0`: periodic runtime status snapshot interval (0 disables heartbeat, default).
- `KH_DEBUG_SAME_DISPATCH_WARN=10000`: warn when ARM9 repeatedly dispatches the same address.
- `KH_SUPERDEBUG=1`: environment fallback to force instruction spam (same as passing `superdebug`).

What to look for in logs:
- `sameDispatch` rapidly increasing at one `lastDispatch` address indicates a tight loop.
- `gxSwaps=0` and `submit2D=0` indicate rendering command submission has not started yet.
- Growing `vblank` with no render submits indicates timing is alive but game bootstrap is likely stuck.

## Hardware Emulation Status

| Subsystem          | Status       | Notes                                     |
|--------------------|--------------|-------------------------------------------|
| VFS (File System)  | ✅ Complete  | FAT/FNT parsing, file-by-ID/path loading  |
| Memory Map         | ✅ Complete  | Main RAM, WRAM, ITCM, DTCM, VRAM, OAM    |
| CPU Context        | ✅ Complete  | Full register file, mode switching, CP15  |
| IPC (FIFO/Sync)    | ✅ Complete  | Lock-free SPSC queue, IPCSYNC register    |
| DMA Controller     | ✅ Complete  | 4 channels, addr control, memcpy transfer |
| Math Engine        | ✅ Complete  | Div32/64, Sqrt32/64, div-by-zero behavior |
| Hardware Timers    | ✅ Complete  | 4 channels, prescaler, chrono-based       |
| RTC                | ✅ Complete  | BCD encoding, SPI protocol state machine  |
| IRQ Controller     | ✅ Complete  | IME/IE/IF, VBlank, all IRQ sources        |
| Binary Lifter      | ✅ Complete  | ARM/Thumb decode, 25+ instruction emitters|
| 3D Geometry Engine | 🔲 Phase 5  | Fixed-function GPU intercept              |
| 2D Engine (OAM/BG) | 🔲 Phase 6  | Sprite/tile rendering                     |
| Save Data (SPI)    | 🔲 Phase 7  | EEPROM/Flash emulation                   |
| Overlays           | 🔲 Phase 8  | Dynamic code swapping                    |

## Development Conventions

1. **TDD First**: Every hardware feature must have ≥3 tests before implementation.
2. **Memory Safety**: All game memory access goes through `NDSMemory::Read/Write` wrappers.
3. **No Raw Pointers**: The lifter resolves literal pools statically; no runtime pointer chasing.
4. **Clean Room**: No proprietary ROM data in logic directories. VFS loads from user's local disk.
5. **GBATEK Compliance**: All hardware emulation matches GBATEK specifications exactly.

## License

This project contains no proprietary code or assets. The lifter, runtime wrappers, and mod API are original work. Users must supply their own legally-dumped `.nds` ROM.