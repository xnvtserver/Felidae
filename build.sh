#!/usr/bin/env bash
set -euo pipefail

# Direct-interpreter build entry point. RocksDB is the fact store.
MODE="debug"
RUN_TESTS=0
JOBS=""
PLATFORM="native"

usage() {
    echo "usage: ./build.sh [debug|release|sanitize] [--platform native|x64|x86|arm|arm64] [--test] [--jobs N]"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        debug|release|sanitize) MODE="$1"; shift ;;
        --test) RUN_TESTS=1; shift ;;
        --platform)
            PLATFORM="${2:-}"
            case "$PLATFORM" in
                native|x64|x86|arm|arm64) ;;
                *) echo "--platform expects native, x64, x86, arm, or arm64" >&2; exit 2 ;;
            esac
            shift 2
            ;;
        --jobs)
            JOBS="${2:-}"
            [[ "$JOBS" =~ ^[1-9][0-9]*$ ]] || { echo "--jobs expects a positive integer" >&2; exit 2; }
            shift 2
            ;;
        --help|-h) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

case "$(uname -m)" in
    x86_64|amd64) HOST_PLATFORM=x64 ;;
    i386|i486|i586|i686) HOST_PLATFORM=x86 ;;
    arm64|aarch64) HOST_PLATFORM=arm64 ;;
    armv6*|armv7*|arm) HOST_PLATFORM=arm ;;
    *) echo "Unsupported host architecture: $(uname -m)" >&2; exit 2 ;;
esac
if [[ "$PLATFORM" == "native" ]]; then PLATFORM="$HOST_PLATFORM"; fi

case "$MODE" in
    debug) BUILD_TYPE=Debug; BUILD_NAME=debug; SANITIZERS=OFF ;;
    release) BUILD_TYPE=Release; BUILD_NAME=release; SANITIZERS=OFF ;;
    sanitize) BUILD_TYPE=Debug; BUILD_NAME=asan; SANITIZERS=ON ;;
esac
BUILD_DIR="build/$BUILD_NAME/$PLATFORM"

BUILD_TESTS=OFF
if [[ "$RUN_TESTS" -eq 1 ]]; then BUILD_TESTS=ON; fi
CMAKE_ARGS=(
    -S .
    -B "$BUILD_DIR"
    "-DCMAKE_BUILD_TYPE=$BUILD_TYPE"
    "-DFELIDAE_BUILD_TESTS=$BUILD_TESTS"
    "-DFELIDAE_ENABLE_SANITIZERS=$SANITIZERS"
    "-DFELIDAE_DEPENDENCY_JOBS=${JOBS:-1}"
)

if [[ "$(uname -s)" == "Darwin" ]]; then
    case "$PLATFORM" in
        x64) CMAKE_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=x86_64") ;;
        arm64) CMAKE_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=arm64") ;;
        *) echo "macOS supports --platform x64 or arm64" >&2; exit 2 ;;
    esac
elif [[ "$PLATFORM" != "$HOST_PLATFORM" && -z "${CMAKE_TOOLCHAIN_FILE:-}" ]]; then
    echo "Cross-compiling from $HOST_PLATFORM to $PLATFORM requires CMAKE_TOOLCHAIN_FILE" >&2
    exit 2
fi
if [[ -n "${CMAKE_TOOLCHAIN_FILE:-}" ]]; then
    CMAKE_ARGS+=("-DCMAKE_TOOLCHAIN_FILE=$CMAKE_TOOLCHAIN_FILE")
fi
cmake "${CMAKE_ARGS[@]}"

BUILD_TARGETS=(felidae)
if [[ "$RUN_TESTS" -eq 1 ]]; then BUILD_TARGETS+=(felidae_storage_tests); fi
BUILD_ARGS=(--build "$BUILD_DIR" --target "${BUILD_TARGETS[@]}")
if [[ -n "$JOBS" ]]; then BUILD_ARGS+=(--parallel "$JOBS"); fi
cmake "${BUILD_ARGS[@]}"

if [[ "$RUN_TESTS" -eq 1 ]]; then
    ctest --test-dir "$BUILD_DIR" --output-on-failure
fi
