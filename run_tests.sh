#!/usr/bin/env bash
# Configure, build, and run the Google Test suite via CMake/CTest only.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
BUILD_CONFIG="${BUILD_CONFIG:-Release}"

cd "${ROOT_DIR}"

cmake -S . -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE="${BUILD_CONFIG}"
cmake --build "${BUILD_DIR}" --config "${BUILD_CONFIG}"
ctest --test-dir "${BUILD_DIR}" --build-config "${BUILD_CONFIG}" --output-on-failure "$@"
