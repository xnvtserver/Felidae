#!/usr/bin/env bash
set -euo pipefail

# Direct-interpreter build entry point.  It has no compiler, VM, model
# training, RocksDB, or external runtime options.
MODE="debug"
RUN_TESTS=0
JOBS=""

usage() {
    echo "usage: ./build.sh [debug|release|sanitize] [--test] [--jobs N]"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        debug|release|sanitize) MODE="$1"; shift ;;
        --test) RUN_TESTS=1; shift ;;
        --jobs)
            JOBS="${2:-}"
            [[ "$JOBS" =~ ^[1-9][0-9]*$ ]] || { echo "--jobs expects a positive integer" >&2; exit 2; }
            shift 2
            ;;
        --help|-h) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

case "$MODE" in
    debug) BUILD_TYPE=Debug; BUILD_DIR=build/debug; SANITIZERS=OFF ;;
    release) BUILD_TYPE=Release; BUILD_DIR=build/release; SANITIZERS=OFF ;;
    sanitize) BUILD_TYPE=Debug; BUILD_DIR=build/asan; SANITIZERS=ON ;;
esac

BUILD_TESTS=OFF
if [[ "$RUN_TESTS" -eq 1 ]]; then BUILD_TESTS=ON; fi
CMAKE_ARGS=(
    -S .
    -B "$BUILD_DIR"
    "-DCMAKE_BUILD_TYPE=$BUILD_TYPE"
    "-DFELIDAE_BUILD_TESTS=$BUILD_TESTS"
    "-DFELIDAE_ENABLE_SANITIZERS=$SANITIZERS"
)
cmake "${CMAKE_ARGS[@]}"

BUILD_ARGS=(--build "$BUILD_DIR" --target felidae)
if [[ -n "$JOBS" ]]; then BUILD_ARGS+=(--parallel "$JOBS"); fi
cmake "${BUILD_ARGS[@]}"

if [[ "$RUN_TESTS" -eq 1 ]]; then
    ctest --test-dir "$BUILD_DIR" --output-on-failure
fi
