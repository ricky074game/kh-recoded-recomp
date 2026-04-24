# Boot-To-Playable Recovery Plan

Status: active investigation.

> **For agentic workers:** Treat this as an execution plan, not a speculative wishlist. Update the checklist as findings change. Prefer fixing root causes over adding new startup skips.

**Goal:** Move the project from the current headless/boot-stalled state to a fully playable runtime that reaches the normal game startup flow, initializes save data correctly, plays cutscenes, and remains stable during gameplay.

**Definition of "fully playable":**
- Boots through the real game initialization path without forced title/fallback display hacks.
- Reaches the normal title screen and attract/cutscene flow.
- Creates or loads save data through the real game path.
- Enters gameplay and remains stable through common actions.
- Renders, inputs, audio, overlays, timers, and save I/O behave closely enough for normal progression.

**Important current conclusion:** The primary issue is not missing extracted data. `arm9.bin`, `y9.bin`, and title assets are loading. The major blockers are bootflow correctness, lifted code correctness, sparse decode/control-flow gaps, and incomplete runtime helper/hardware behavior.

---

## 1. Current Known Blockers

- [x] Confirm that startup data is present and being loaded.
- [x] Confirm that the frontend fallback title path is only masking a deeper boot failure.
- [x] Identify one real lifter bug affecting early ARM9 boot.
- [ ] Finish tracing the remaining early boot helper chain failures.
- [ ] Remove startup shortcuts once real behavior works.

### Confirmed issues

1. **Early ARM9 boot does not complete correctly**
   - The game does not reliably reach the real display-init state.
   - `POWCNT1`, `DISPCNT-A`, and `DISPCNT-B` were previously never reached in the failing path.
   - The fallback title render is a symptom, not the cause.

2. **The lifter had a real Thumb shift emission bug**
   - Thumb `lsrs` / `asrs` three-operand forms were emitted like plain register moves.
   - This broke real early-boot logic.
   - This is now fixed in `lifter/src/c_emitter.cpp` and covered by tests.

3. **Boot still enters sparse or invalid control-flow regions**
   - After the shift fix, boot advances further but still falls into addresses such as:
   - `0x0202A240`
   - `0x0202ABFC`
   - `0x020069D8`
   - `0x020235B2`
   - At least some of these look like data/literal holes rather than real missing code blocks.

4. **The helper chain around `0x01FF80D4` / `0x01FF80E4` is still not faithfully modeled**
   - There are runtime shortcuts in `runtime/src/hw/hw_overlay.cpp` that keep boot moving.
   - Those shortcuts are useful for diagnosis, but they are not the final fix.
   - When the shortcut is removed, the real chain still hits sparse/unmapped targets.

5. **Some boot-time helper stubs are still bring-up approximations**
   - Several stubs in `runtime/src/hw/hw_mainram_helper_0202B454.cpp` seed state or force returns.
   - Those may be necessary temporarily, but they can also push boot into the wrong path if they remain inaccurate.

6. **BIOS and hardware behavior is still incomplete**
   - One real missing BIOS feature (`SWI 0x10 BitUnPack`) was added.
   - More BIOS, memory, timing, interrupt, GX, and peripheral details may still be missing for startup and gameplay correctness.

---

## 2. Recovery Strategy

The project should be fixed in this order:

- [ ] Phase A: make the real boot path execute correctly
- [ ] Phase B: remove the fake/fallback startup dependencies
- [ ] Phase C: reach title screen, save init, and cutscenes
- [ ] Phase D: reach stable gameplay
- [ ] Phase E: harden correctness and regressions

The priority is to stop the runtime from entering bad control-flow/data holes during startup. Do not spend time polishing frontend presentation until the real game-owned boot path is working.

---

## 3. Phase A: Fix Real Boot Execution

Current status as of `2026-04-24`:
- Hard boot halt at `0x0202A240` was turned into continued execution by shimming the bad `0x020254E8 -> 0x02000D90` helper return path.
- Boot is still not complete: ARM9 stays alive but remains headless, `POWCNT1`/`DISPCNT-A`/`DISPCNT-B` still do not hand display ownership to the real game path, and several bring-up shims are still active.

### Task A1: Finish lifter correctness work for early boot instructions

**Files:**
- `lifter/src/c_emitter.cpp`
- `lifter/tests/test_emitter.cpp`
- `runtime/src/generated/arm9_chunk_*.cpp`

- [x] Fix Thumb immediate `lsrs` / `asrs` emission.
- [x] Add emitter regression tests for those forms.
- [x] Audit other standalone Thumb shift/operand forms used in the same boot cluster.
- [x] Add tests for any newly found bad instruction forms.
- [x] Regenerate ARM9 output after each lifter fix and verify the changed blocks.

**Exit criteria:**
- No known mismatches between Capstone decode and generated C++ in the hot boot region already identified.

### Task A2: Fix sparse decode / control-flow analysis gaps

**Files:**
- `lifter/src/arm_decoder.cpp`
- `lifter/src/main.cpp`
- `lifter/tests/test_decoder.cpp`
- `runtime/src/generated/arm9_chunk_*.cpp`

- [x] Investigate why control flow reaches non-code/data holes like `0x0202A240` and `0x020235B2`.
- [ ] Distinguish between:
- [ ] bad branch target resolution
- [x] missed Thumb islands
- [ ] legitimate code not being seeded
- [x] bogus fallthrough into literal/data regions
- [ ] Add decoder tests for branch/literal-pool patterns that match the failing startup regions.
- [ ] Improve CFG seeding or branch resolution only where justified by evidence.
- [ ] Avoid solving data-hole bugs by blindly adding more static-gap recovery.

Notes:
- `0x0202A240` was confirmed to be a non-decodable Thumb/data hole, not a missing asset load.
- Current evidence points to overlapping ARM/Thumb entry behavior plus bad fallthrough/helper state, not a single missing static seed.

**Exit criteria:**
- Early boot no longer falls into known data/unmapped holes in the current startup path.

### Task A3: Replace bring-up shortcuts with correct helper behavior

**Files:**
- `runtime/src/hw/hw_overlay.cpp`
- `runtime/src/hw/hw_mainram_helper_0202B454.cpp`
- other helper files under `runtime/src/hw/`

- [x] Re-audit the `0x01FF80D4` and `0x01FF80E4` handling.
- [ ] Determine which currently registered helper stubs are:
- [ ] correct enough to keep
- [ ] temporary but harmless
- [ ] actively pushing boot down the wrong path
- [ ] Replace shortcut returns with real semantics where possible.
- [ ] Remove or narrow special-case boot skips once the underlying path works.

Notes:
- `0x01FF80E4` was corrected to preserve `r4` instead of collapsing it to `1`.
- A targeted shim at `0x020254E8` now preserves the observed `r0 = r4 >> 9` status and stops the old boot-halting tail return.
- The helper chain is still not real enough to remove the broader boot skips yet.

**Exit criteria:**
- Startup can pass through the real helper chain without requiring the current boot skip at `0x02000C9C -> 0x01FF80D4`.

### Task A4: Complete missing BIOS/hardware behavior needed by boot

**Files:**
- `runtime/src/hw/hw_bios.cpp`
- `runtime/src/core/memory_map.*`
- `runtime/src/hw/*`
- `runtime/tests/*`

- [x] Add `SWI 0x10 BitUnPack`.
- [ ] Trace any remaining SWIs used before title screen.
- [ ] Validate DMA, IPC, timers, IRQ, GX, and VRAM behavior against boot expectations.
- [ ] Confirm display-init register writes happen in the real path.
- [ ] Add targeted tests for each missing hardware semantic found during boot.

Notes:
- We confirmed the current runtime is still headless after startup progress; the real display-init ownership path is not complete yet.

**Exit criteria:**
- Real startup reaches stable display ownership and no longer needs the frontend title fallback.

---

## 4. Phase B: Reach Real Title Screen and Save Init

### Task B1: Remove fallback-driven startup

**Files:**
- `runtime/src/core/title_screen_loader.cpp`
- `runtime/src/core/runtime_qt_frontend.cpp`
- `runtime/src/core/main.cpp`

- [ ] Keep fallback title rendering only as a debug tool, not as normal boot behavior.
- [ ] Confirm the game itself renders title/cutscene output into the DS display path.
- [ ] Confirm the frontend presents real game frames, not a placeholder/headless state.

**Exit criteria:**
- The title screen appears because the game rendered it, not because the frontend forced it.

### Task B2: Verify real savefile initialization

**Files:**
- save/VFS-related runtime files
- file I/O and archive handling code

- [ ] Trace the game's actual save initialization path.
- [ ] Verify that required files/directories are created exactly where the game expects them.
- [ ] Confirm first-boot save creation and subsequent save loading.
- [ ] Validate failure handling if a save is missing or invalid.

**Exit criteria:**
- A fresh run creates or initializes save data through the game's own logic.

### Task B3: Reach and preserve cutscene flow

**Files:**
- 2D/3D renderers
- video/cutscene playback code if separate
- audio and timing systems

- [ ] Verify that intro and title-adjacent cutscenes run without being skipped.
- [ ] Confirm audio/video/timing stay synchronized enough for those sequences.
- [ ] Fix any mode transitions or resource loads used specifically by cutscene paths.

**Exit criteria:**
- Intro/title cutscenes play in-engine without hard skips or fake routing.

---

## 5. Phase C: Reach Stable Gameplay

### Task C1: Enter gameplay from a clean boot

**Files:**
- overlay loading/runtime dispatch
- gameplay subsystems touched after title

- [ ] Confirm overlay transitions after title work.
- [ ] Confirm world/mission load paths work.
- [ ] Confirm player control begins without softlock or black screen.

**Exit criteria:**
- A normal boot can reach a controllable gameplay state.

### Task C2: Fix gameplay-critical hardware/runtime behavior

**Files:**
- input, timers, math, DMA, interrupts, 2D/3D engines, audio, filesystem

- [ ] Validate player input.
- [ ] Validate timing-sensitive gameplay logic.
- [ ] Validate battle/UI transitions.
- [ ] Validate menu flows and pause/state changes.
- [ ] Validate asset streaming and overlay swaps during play.

**Exit criteria:**
- Normal gameplay actions do not immediately desync, freeze, or corrupt rendering/audio.

---

## 6. Phase D: Make It "Fully Playable"

### Task D1: Build a playability checklist

- [ ] Boot from clean state
- [ ] Intro/cutscene path
- [ ] Title screen
- [ ] New game
- [ ] Save creation
- [ ] Load existing save
- [ ] Enter gameplay
- [ ] Move, attack, interact, transition scenes
- [ ] Open menus
- [ ] Survive extended play session

### Task D2: Validate representative content

- [ ] First boot
- [ ] Returning boot with existing save
- [ ] At least one combat sequence
- [ ] At least one room/scene transition
- [ ] At least one cutscene after boot
- [ ] At least one save/load round trip

### Task D3: Remove or quarantine temporary hacks

- [ ] Review all boot/helper shortcuts and debug-only recoveries.
- [ ] Remove those no longer needed.
- [ ] Clearly isolate any unavoidable temporary workarounds behind comments and flags.

**Exit criteria:**
- The project can be honestly described as playable without relying on major fake boot shortcuts.

---

## 7. Validation and Tooling

### Required diagnostics to keep using

- [ ] Watchdog runs for startup stalls
- [ ] Runtime snapshots for boot-state globals
- [ ] Capstone comparison against generated blocks
- [ ] Focused regression tests in lifter and runtime

### Useful current artifacts

- `scratch/disasm_probe.cpp`
- watchdog logs
- runtime snapshot logging in `runtime/src/core/main.cpp`

### Add next

- [ ] A repeatable "boot smoke test" that fails when startup falls into known unmapped targets.
- [ ] A small comparison harness for decoded instruction vs emitted code in hot regions.
- [ ] A tracked list of currently intentional helper shortcuts and why they still exist.

---

## 8. Recommended Immediate Next Steps

- [ ] Keep the Thumb shift fix and regenerated ARM9 output.
- [ ] Investigate the `0x0202A240` path as a control-flow/data-hole problem, not a missing asset problem.
- [ ] Reconstruct the real chain that currently reaches `0x020235B2`.
- [ ] Add decoder tests or targeted seeding only if the target is proven to be real executable code.
- [ ] Revisit the `0x01FF80D4` shortcut only after the helper chain behind it is actually valid.

---

## 9. Practical Truth About Scope

Getting from "boots to blank screens" to "fully playable" is not one fix. It is a sequence:

1. Correct lifted instructions.
2. Correct control flow.
3. Correct boot helpers and BIOS behavior.
4. Correct display/save/cutscene init.
5. Correct gameplay-time hardware/runtime behavior.

The project becomes fully playable only when all five layers are solid enough together.
