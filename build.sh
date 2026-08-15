#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

if [[ "${1:-}" == "--clean" ]]; then
    rm -rf "$BUILD_DIR"
fi

GENERATOR_ARGS=()
BINARY="order_matching_engine"
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        GENERATOR_ARGS=(-G "MinGW Makefiles")
        BINARY="order_matching_engine.exe"
        ;;
esac

JOBS="$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" "${GENERATOR_ARGS[@]}"
cmake --build "$BUILD_DIR" -j"$JOBS"

echo "Build succeeded: $BUILD_DIR/$BINARY"
