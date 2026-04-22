# Kingdom Hearts Re:coded Static Recompilation

This project recompiles Nintendo DS game code from Kingdom Hearts Re:coded into native C++ and runs it through a custom runtime.

It is split into three core components:
- lifter: translates ARM binaries into C++ source files
- runtime: emulates DS hardware behavior and executes lifted code
- mod_api: static modding interface used by runtime-side hooks

## Legal and Scope

- This repository does not include proprietary game assets.
- You must use your own legally dumped ROM.
- Generated code and extracted assets are produced locally on your machine.

## Quick Start

### Linux and macOS

1. Put your ROM in the repository root, for example recoded.nds.
2. Run:

```bash
chmod +x ./setup.sh
./setup.sh recoded.nds
```

Debug build:

```bash
./setup.sh -d recoded.nds
```

Final executable:

- build/runtime/recoded

### Windows

1. Put your ROM in the repository root, for example recoded.nds.
2. Run:

```bat
setup.bat recoded.nds
```

Final executable:

- build\runtime\recoded.exe (or equivalent generator output)

## How It Works

This is the full pipeline used by setup.sh.

### 1) Validate and prepare input

- The script checks required tools (cmake, python3, sha256sum, awk).
- It validates the ROM checksum against a known US dump hash.
- If the ROM is split into parts (for example recoded.nds.partaa, partab, ...), it recombines them.

### 2) Build the lifter

- The lifter binary is built in build_lifter as lifter_engine.
- If source inputs have not changed and cache is valid, this step is skipped.

### 3) Extract ROM contents

- ndstool extracts key files into recoded/:
  - recoded/arm9.bin
  - recoded/y9.bin
  - recoded/overlay/
  - recoded/data/
- Extraction is cached by ROM hash in recoded/.extract_stamp.

### 4) Lift ARM binaries to C++

- arm9.bin is lifted first.
- Overlays listed in y9.bin are parsed and lifted in parallel.
- Output goes to runtime/src/generated.
- Lifting is cached using ROM hash plus lifter binary hash in runtime/src/generated/.lift_stamp.

### 5) Generate registration glue

- setup.sh emits runtime/src/generated/master_registration.cpp.
- That file wires all lifted registration functions into RegisterAllLiftedFunctions.
- Runtime uses this to map runtime addresses to generated handlers.

### 6) Build runtime executable

- CMake configures the root project in build/.
- Target runtime_engine is built and output as recoded.

## Runtime Execution Model

At runtime, the engine follows this flow:

1. Initialize virtual DS state (memory map, hardware blocks, CPU contexts).
2. Load arm9.bin into emulated main RAM.
3. Load overlay metadata from y9.bin.
4. Start ARM9 and ARM7 execution threads.
5. Dispatch translated code by address through the overlay manager and generated registry.
6. Route memory-mapped IO reads and writes to emulated hardware modules (IRQ, timers, DMA, GX, 2D engine, audio, input, save).
7. Drive frame timing with a VBlank timing thread.

## Build and Test Commands

### Build all targets

```bash
cmake -S . -B build
cmake --build build -j"$(nproc)"
```

### Runtime tests

```bash
./build/runtime/runtime_tests --gtest_color=no
```

### Lifter tests

```bash
cmake -S lifter -B build_lifter
cmake --build build_lifter -j"$(nproc)"
ctest --test-dir build_lifter --output-on-failure
```

## Running

Default extracted data path:

```bash
./build/runtime/recoded recoded/data
```

You can also pass a custom data directory:

```bash
./build/runtime/recoded /path/to/extracted/data
```

## Debugging

Watchdog mode is useful for startup stalls:

```bash
KH_DEBUG_WATCHDOG=1 KH_DEBUG_WATCHDOG_POLL_MS=300 ./build/runtime/recoded recoded/data
```

Useful environment variables:
- KH_DEBUG_WATCHDOG
- KH_DEBUG_WATCHDOG_POLL_MS
- KH_DEBUG_WATCHDOG_STALL_MS
- KH_DEBUG_HEARTBEAT_MS
- KH_DEBUG_SAME_DISPATCH_WARN

Optional instruction spam mode:

```bash
./build/runtime/recoded recoded/data superdebug
```

## setup.sh Options

```text
-d, --debug             Debug build with EXTREME_DEBUG enabled
-j, --jobs N            Compile parallelism
--lift-jobs N           Overlay lifting parallelism
--skip-lifter-build     Reuse existing lifter binary
--force-extract         Ignore extract cache and re-extract
--force-lift            Ignore lift cache and re-lift
```

## Repository Layout

- lifter/: offline ARM-to-C++ translation tool
- runtime/: execution runtime and hardware emulation
- mod_api/: hook and mod loading API
- recoded/: extracted local ROM content (not source-controlled)
- tools/: helper tools and bundled dependencies

## Current Status

Implemented and actively used:
- memory map and CPU context
- IRQ, timers, DMA, IPC, math engine
- VFS and overlay loading
- 2D and 3D rendering pipeline integration paths
- SDL input/audio plumbing

Still evolving:
- full compatibility and edge-case behavior
- boot and gameplay correctness across all content paths
- performance tuning and regression hardening

## License

Project source code in this repository is original work under the repository license.
Game content remains owned by its respective rights holders and is not distributed here.
