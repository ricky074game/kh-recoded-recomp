#!/bin/bash

# setup.sh - Kingdom Hearts Re:coded Static Recompilation Build Orchestrator
# This script extracts the ROM, runs the lifter with chunking, and compiles.

set -euo pipefail

usage() {
    cat <<'EOF'
Usage: ./setup.sh [options] <path_to_nds_rom>

Options:
  -d, --debug            Build runtime in Debug mode with EXTREME_DEBUG enabled.
  -j, --jobs N           Max parallel compile jobs (default: auto-tuned for CPU/RAM).
      --lift-jobs N      Max parallel overlay-lift jobs (default: min(jobs, 4)).
    --skip-lifter-build Skip rebuilding lifter_engine when binary already exists.
      --force-extract    Force ROM extraction even when cache is valid.
      --force-lift       Force binary lifting even when cache is valid.
  -h, --help             Show this help text.

Environment overrides:
  KH_BUILD_JOBS          Same as --jobs.
  KH_LIFT_JOBS           Same as --lift-jobs.
EOF
}

is_positive_int() {
    [[ "$1" =~ ^[1-9][0-9]*$ ]]
}

detect_cpu_count() {
    nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1
}

detect_mem_gib() {
    if [[ -r /proc/meminfo ]]; then
        awk '/MemTotal:/ { printf "%d", $2 / 1024 / 1024 }' /proc/meminfo
    else
        echo 0
    fi
}

auto_tune_build_jobs() {
    local cpu_count mem_gib mem_cap jobs
    cpu_count=$(detect_cpu_count)
    mem_gib=$(detect_mem_gib)
    jobs="$cpu_count"

    # C++ compilation here is memory-heavy; cap jobs by RAM to avoid swap storms.
    if (( mem_gib > 0 )); then
        mem_cap=$((mem_gib / 3))
        if (( mem_cap < 1 )); then
            mem_cap=1
        fi
        if (( mem_cap < jobs )); then
            jobs="$mem_cap"
        fi
    fi

    if (( jobs < 1 )); then
        jobs=1
    fi
    if (( jobs > 8 )); then
        jobs=8
    fi

    echo "$jobs"
}

wait_for_one_background_job() {
    if wait -n 2>/dev/null; then
        return 0
    fi

    local pid
    pid=$(jobs -pr | head -n 1 || true)
    if [[ -n "$pid" ]]; then
        wait "$pid"
    fi
}

parse_overlay_data() {
    python3 - <<'PY'
import struct

try:
    with open("recoded/y9.bin", "rb") as f:
        data = f.read()

    for i in range(len(data) // 32):
        chunk = data[i * 32 : (i + 1) * 32]
        ovl_id = struct.unpack("<I", chunk[0:4])[0]
        ram_addr = struct.unpack("<I", chunk[4:8])[0]
        print(f"{ovl_id} {ram_addr:08x}")
except Exception:
    pass
PY
}

cmake_configure() {
    local source_dir="$1"
    local build_dir="$2"
    shift 2

    local cmd
    cmd=(cmake -S "$source_dir" -B "$build_dir")
    if [[ ! -f "$build_dir/CMakeCache.txt" && ${#CMAKE_GENERATOR_ARGS[@]} -gt 0 ]]; then
        cmd+=("${CMAKE_GENERATOR_ARGS[@]}")
    fi
    cmd+=("$@")
    "${cmd[@]}"
}

for required_cmd in cmake python3 sha256sum awk; do
    if ! command -v "$required_cmd" >/dev/null 2>&1; then
        echo "Error: Required command '$required_cmd' was not found in PATH."
        exit 1
    fi
done

DEBUG_MODE=0
ROM_PATH=""
FORCE_EXTRACT=0
FORCE_LIFT=0
SKIP_LIFTER_BUILD=0
BUILD_JOBS="${KH_BUILD_JOBS:-}"
LIFT_JOBS="${KH_LIFT_JOBS:-}"
ROOT_CMAKE_FLAGS=()
COMMON_CMAKE_FLAGS=()
CMAKE_GENERATOR_ARGS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        -d|--debug)
            DEBUG_MODE=1
            shift
            ;;
        -j|--jobs)
            if [[ $# -lt 2 ]]; then
                echo "Error: --jobs requires a positive integer value."
                usage
                exit 1
            fi
            BUILD_JOBS="$2"
            shift 2
            ;;
        --lift-jobs)
            if [[ $# -lt 2 ]]; then
                echo "Error: --lift-jobs requires a positive integer value."
                usage
                exit 1
            fi
            LIFT_JOBS="$2"
            shift 2
            ;;
        --force-extract)
            FORCE_EXTRACT=1
            shift
            ;;
        --skip-lifter-build)
            SKIP_LIFTER_BUILD=1
            shift
            ;;
        --force-lift)
            FORCE_LIFT=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        -*)
            echo "Error: Unknown option '$1'"
            usage
            exit 1
            ;;
        *)
            if [[ -z "$ROM_PATH" ]]; then
                ROM_PATH="$1"
            else
                echo "Error: Too many positional arguments."
                usage
                exit 1
            fi
            shift
            ;;
    esac
done

if [[ -z "$ROM_PATH" ]]; then
    usage
    exit 1
fi

if [[ -z "$BUILD_JOBS" ]]; then
    BUILD_JOBS=$(auto_tune_build_jobs)
fi
if ! is_positive_int "$BUILD_JOBS"; then
    echo "Error: --jobs (or KH_BUILD_JOBS) must be a positive integer."
    exit 1
fi

if [[ -z "$LIFT_JOBS" ]]; then
    LIFT_JOBS="$BUILD_JOBS"
    if (( LIFT_JOBS > 4 )); then
        LIFT_JOBS=4
    fi
fi
if ! is_positive_int "$LIFT_JOBS"; then
    echo "Error: --lift-jobs (or KH_LIFT_JOBS) must be a positive integer."
    exit 1
fi

if [[ "$DEBUG_MODE" -eq 1 ]]; then
    ROOT_CMAKE_FLAGS+=("-DCMAKE_BUILD_TYPE=Debug" "-DCMAKE_CXX_FLAGS=-DEXTREME_DEBUG=1")
fi

if command -v ninja >/dev/null 2>&1; then
    CMAKE_GENERATOR_ARGS=("-G" "Ninja")
fi

if command -v ccache >/dev/null 2>&1; then
    COMMON_CMAKE_FLAGS+=("-DCMAKE_C_COMPILER_LAUNCHER=ccache" "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache")
fi

echo "=> Using $BUILD_JOBS compile jobs and $LIFT_JOBS lift jobs."

# Known good dump SHA-256 for US version
KNOWN_SHA256="929f36f1e09b6b0962ac718332033bbd519f2edede18a3ab65f425ddc66e3fd3"

if [[ ! -f "$ROM_PATH" && -f "${ROM_PATH}.partaa" ]]; then
    echo "=> Recombining split ROM..."
    cat "${ROM_PATH}.part"* > "$ROM_PATH"
fi

if [[ ! -f "$ROM_PATH" ]]; then
    echo "Error: ROM file not found at $ROM_PATH"
    exit 1
fi

echo "=> Validating ROM checksum..."
ACTUAL_SHA256=$(sha256sum "$ROM_PATH" | awk '{print $1}')
echo "Calculated SHA-256: $ACTUAL_SHA256"

if [[ "$ACTUAL_SHA256" != "$KNOWN_SHA256" ]]; then
    echo "Warning: Checksum does not match the known good US dump."
    echo "Proceeding anyway, but compilation or runtime errors may occur."
fi

# Step 0: Build lifter first (target-only build avoids compiling tests).
if [[ "$SKIP_LIFTER_BUILD" -eq 1 ]]; then
    if [[ -x build_lifter/lifter_engine ]]; then
        if find lifter -type f -newer build_lifter/lifter_engine | grep -q .; then
            echo "Error: --skip-lifter-build requested, but lifter sources changed since build_lifter/lifter_engine was built."
            echo "Re-run without --skip-lifter-build so lifter_engine can be rebuilt."
            exit 1
        fi
        echo "=> Building Lifter Engine... (skipped by --skip-lifter-build)"
    else
        echo "Error: --skip-lifter-build requested but build_lifter/lifter_engine is missing."
        exit 1
    fi
else
    NEED_LIFTER_BUILD=1
    if [[ "$FORCE_LIFT" -eq 0 && -x build_lifter/lifter_engine ]]; then
        if ! find lifter -type f -newer build_lifter/lifter_engine | grep -q .; then
            NEED_LIFTER_BUILD=0
        fi
    fi

    if [[ "$NEED_LIFTER_BUILD" -eq 0 ]]; then
        echo "=> Building Lifter Engine... (cached, skipping)"
    else
        echo "=> Building Lifter Engine..."
        mkdir -p build_lifter
        cmake_configure lifter build_lifter "${COMMON_CMAKE_FLAGS[@]}"
        cmake --build build_lifter --target lifter_engine --parallel "$BUILD_JOBS"
    fi
fi

# Step 1: Extract ROM via ndstool (cache by ROM hash).
mkdir -p recoded
EXTRACT_STAMP="recoded/.extract_stamp"
EXTRACT_CACHE_KEY="rom_sha=$ACTUAL_SHA256"

NDSTOOL_CMD=()
LOCAL_NDSTOOL="tools/nds_extractor/ndstool/ndstool"
LOCAL_NDSTOOL_EXE="tools/nds_extractor/ndstool.exe"

if [[ -x "$LOCAL_NDSTOOL" ]]; then
    NDSTOOL_CMD=("$LOCAL_NDSTOOL")
elif command -v ndstool >/dev/null 2>&1; then
    NDSTOOL_CMD=("ndstool")
elif [[ -f "$LOCAL_NDSTOOL_EXE" ]] && command -v wine >/dev/null 2>&1; then
    NDSTOOL_CMD=("wine" "$LOCAL_NDSTOOL_EXE")
fi

if [[ "$FORCE_EXTRACT" -eq 0 && -f "$EXTRACT_STAMP" && -f recoded/arm9.bin && -f recoded/y9.bin && -d recoded/overlay ]] && grep -Fxq "$EXTRACT_CACHE_KEY" "$EXTRACT_STAMP"; then
    echo "=> Step 1: Extracting ROM... (cached, skipping)"
else
    echo "=> Step 1: Extracting ROM..."
    if [[ "${#NDSTOOL_CMD[@]}" -gt 0 ]]; then
        "${NDSTOOL_CMD[@]}" -x "$ROM_PATH" -9 recoded/arm9.bin -y9 recoded/y9.bin -y recoded/overlay -d recoded/data
        printf '%s\n' "$EXTRACT_CACHE_KEY" > "$EXTRACT_STAMP"
    else
        if [[ -f recoded/arm9.bin && -f recoded/y9.bin && -d recoded/overlay ]]; then
            echo "Warning: ndstool was not found; reusing existing extracted files in recoded/."
            printf '%s\n' "$EXTRACT_CACHE_KEY" > "$EXTRACT_STAMP"
        else
            echo "Error: ndstool was not found."
            echo "Install ndstool on PATH, or add a native binary at '$LOCAL_NDSTOOL',"
            echo "or install wine to run '$LOCAL_NDSTOOL_EXE'."
            exit 1
        fi
    fi
fi

# Step 2: Run lifter on arm9.bin + overlays (cache by ROM + lifter binary hash).
GEN_DIR="runtime/src/generated"
LIFT_STAMP="$GEN_DIR/.lift_stamp"
LIFTER_SHA256=$(sha256sum build_lifter/lifter_engine | awk '{print $1}')
LIFT_CACHE_KEY="rom_sha=$ACTUAL_SHA256 lifter_sha=$LIFTER_SHA256"

if [[ "$FORCE_LIFT" -eq 0 && -f "$LIFT_STAMP" && -f "$GEN_DIR/master_registration.cpp" ]] && grep -Fxq "$LIFT_CACHE_KEY" "$LIFT_STAMP"; then
    echo "=> Step 2: Running Binary Lifter with Chunking... (cached, skipping)"
else
    echo "=> Step 2: Running Binary Lifter with Chunking..."
    rm -rf "$GEN_DIR"
    mkdir -p "$GEN_DIR"

    echo "   Lifting arm9.bin..."
    ./build_lifter/lifter_engine recoded/arm9.bin "$GEN_DIR" 0x02000000 0x02000800 -1

    echo "   Parsing y9.bin for overlays..."
    OVERLAY_DATA=$(parse_overlay_data)

    in_flight=0
    while read -r OVL_ID RAM_ADDR; do
        if [[ -z "${OVL_ID:-}" || -z "${RAM_ADDR:-}" ]]; then
            continue
        fi

        OVL_FILE=$(printf "recoded/overlay/overlay_%04d.bin" "$OVL_ID")
        if [[ -f "$OVL_FILE" ]]; then
            echo "   Lifting Overlay $OVL_ID at 0x$RAM_ADDR..."
            ./build_lifter/lifter_engine "$OVL_FILE" "$GEN_DIR" "0x$RAM_ADDR" "0x$RAM_ADDR" "$OVL_ID" &
            in_flight=$((in_flight + 1))

            if (( in_flight >= LIFT_JOBS )); then
                wait_for_one_background_job
                in_flight=$((in_flight - 1))
            fi
        fi
    done <<< "$OVERLAY_DATA"
    wait

    echo "=> Generating Master Registration..."
    REG_FILE="$GEN_DIR/master_registration.cpp"
    {
        echo '#include "memory_map.h"'
        echo 'void arm9_register(NDSMemory* mem);'

        while read -r OVL_ID RAM_ADDR; do
            if [[ -n "${OVL_ID:-}" ]]; then
                echo "void overlay_${OVL_ID}_register(NDSMemory* mem);"
            fi
        done <<< "$OVERLAY_DATA"

        echo 'void RegisterAllLiftedFunctions(NDSMemory* mem) {'
        echo '    arm9_register(mem);'
        while read -r OVL_ID RAM_ADDR; do
            if [[ -n "${OVL_ID:-}" ]]; then
                echo "    overlay_${OVL_ID}_register(mem);"
            fi
        done <<< "$OVERLAY_DATA"
        echo '}'
    } > "$REG_FILE"

    printf '%s\n' "$LIFT_CACHE_KEY" > "$LIFT_STAMP"
fi

# Step 3: Configure and compile only the runtime executable target.
echo "=> Step 3: Compiling Final Recompiled Executable... (Debug: $DEBUG_MODE)"
mkdir -p build
cmake_configure . build "${COMMON_CMAKE_FLAGS[@]}" "${ROOT_CMAKE_FLAGS[@]}"
cmake --build build --target runtime_engine --parallel "$BUILD_JOBS"

echo "=> Step 4: SUCCESS! Final executable in build/runtime/recoded"
