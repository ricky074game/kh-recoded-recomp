#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BIN="$ROOT_DIR/build/runtime/recoded"

if [[ ! -x "$BIN" ]]; then
    echo "Error: runtime binary not found at $BIN"
    echo "Build it first with: cmake --build build --target runtime_engine"
    exit 1
fi

# Avoid Snap/libc interposition issues from host shell environments.
unset LD_LIBRARY_PATH
unset LD_PRELOAD

if [[ $# -eq 0 ]]; then
    exec "$BIN" "$ROOT_DIR/recoded/data"
else
    exec "$BIN" "$@"
fi
