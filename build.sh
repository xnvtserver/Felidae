#!/usr/bin/env bash
set -euo pipefail

# User configuration. Environment variables or command-line options may
# override these values without creating another build script.
MODE="${FELIDAE_MODE:-test}"                       # test | release
ENABLE_TRAINING="${FELIDAE_ENABLE_TRAINING:-OFF}" # ON | OFF
ENABLE_LIBTORCH="${FELIDAE_ENABLE_LIBTORCH:-auto}" # auto | ON | OFF
PLATFORM="${FELIDAE_PLATFORM:-auto}"               # auto | linux | macos | windows | android | generic
ARCHITECTURE="${FELIDAE_ARCH:-auto}"               # auto | x86_64 | arm64 | armv7
ANDROID_API="${FELIDAE_ANDROID_API:-24}"
JOBS="${FELIDAE_JOBS:-auto}"
SANITIZE=0

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

usage() {
    echo "usage: ./build.sh [--mode test|release] [--training ON|OFF] [--libtorch ON|OFF]"
    echo "                  [--platform auto|linux|macos|windows|android|generic]"
    echo "                  [--arch auto|x86_64|arm64|armv7] [--android-api N]"
    echo "                  [--jobs N|auto] [--sanitize]"
}

normalize_boolean() {
    case "${1,,}" in
        1|on|true|yes) echo ON ;;
        0|off|false|no) echo OFF ;;
        *) echo "Invalid boolean value: $1" >&2; exit 2 ;;
    esac
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --mode) MODE="${2:-}"; shift 2 ;;
        --training) ENABLE_TRAINING="${2:-}"; shift 2 ;;
        --libtorch) ENABLE_LIBTORCH="${2:-}"; shift 2 ;;
        --platform) PLATFORM="${2:-}"; shift 2 ;;
        --arch) ARCHITECTURE="${2:-}"; shift 2 ;;
        --android-api) ANDROID_API="${2:-}"; shift 2 ;;
        --jobs) JOBS="${2:-}"; shift 2 ;;
        --sanitize) SANITIZE=1; shift ;;
        --configuration)
            case "${2:-}" in
                debug) MODE=test ;;
                release|production) MODE=release ;;
                sanitize) MODE=test; SANITIZE=1 ;;
                *) echo "Unknown configuration: ${2:-}" >&2; exit 2 ;;
            esac
            shift 2
            ;;
        test|debug) MODE=test; shift ;;
        release|production) MODE=release; shift ;;
        sanitize) MODE=test; SANITIZE=1; shift ;;
        --help|-h) usage; exit 0 ;;
        --warnings-as-errors|native) shift ;;
        --target)
            if [[ "${2:-}" != "native" ]]; then
                echo "Only the CMake native target is supported" >&2
                exit 2
            fi
            shift 2
            ;;
        *) echo "Unknown build option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

case "${MODE,,}" in
    test) MODE=test; CONFIGURATION=Debug; BUILD_TESTS=ON ;;
    release) MODE=release; CONFIGURATION=Release; BUILD_TESTS=OFF ;;
    *) echo "Invalid MODE '$MODE'; expected test or release" >&2; exit 2 ;;
esac

if [[ "${PLATFORM,,}" == auto ]]; then
    case "$(uname -s)" in
        Linux) PLATFORM=linux ;;
        Darwin) PLATFORM=macos ;;
        MINGW*|MSYS*|CYGWIN*) PLATFORM=windows ;;
        *) PLATFORM=generic ;;
    esac
else
    PLATFORM="${PLATFORM,,}"
fi
case "$PLATFORM" in linux|macos|windows|android|generic) ;; *) echo "Invalid platform '$PLATFORM'" >&2; exit 2 ;; esac

if [[ "${ARCHITECTURE,,}" == auto ]]; then
    case "$(uname -m)" in
        x86_64|amd64) ARCHITECTURE=x86_64 ;;
        arm64|aarch64) ARCHITECTURE=arm64 ;;
        armv7*|armhf) ARCHITECTURE=armv7 ;;
        *) ARCHITECTURE="$(uname -m)" ;;
    esac
else
    ARCHITECTURE="${ARCHITECTURE,,}"
fi
case "$(uname -m)" in
    x86_64|amd64) HOST_ARCHITECTURE=x86_64 ;;
    arm64|aarch64) HOST_ARCHITECTURE=arm64 ;;
    armv7*|armhf) HOST_ARCHITECTURE=armv7 ;;
    *) HOST_ARCHITECTURE="$(uname -m)" ;;
esac

ENABLE_TRAINING="$(normalize_boolean "$ENABLE_TRAINING")"
if [[ "${ENABLE_LIBTORCH,,}" == auto ]]; then
    if [[ "$PLATFORM" == android ]]; then
        ENABLE_LIBTORCH=OFF
    else
        ENABLE_LIBTORCH=ON
    fi
else
    ENABLE_LIBTORCH="$(normalize_boolean "$ENABLE_LIBTORCH")"
fi
if [[ "${JOBS,,}" == auto ]]; then
    if command -v getconf >/dev/null 2>&1; then
        JOBS="$(getconf _NPROCESSORS_ONLN)"
    elif command -v sysctl >/dev/null 2>&1; then
        JOBS="$(sysctl -n hw.ncpu)"
    else
        JOBS=1
    fi
fi
if [[ ! "$JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "Invalid job count '$JOBS'; expected auto or a positive integer" >&2
    exit 2
fi
if [[ "$ENABLE_TRAINING" == ON && "$ENABLE_LIBTORCH" != ON ]]; then
    echo "Training requires LibTorch; set ENABLE_LIBTORCH=ON" >&2
    exit 2
fi

if [[ "$PLATFORM" == linux && "$ARCHITECTURE" == "$HOST_ARCHITECTURE" ]]; then
    DEFAULT_BUILD_DIR="${ROOT_DIR}/build/${MODE}"
else
    DEFAULT_BUILD_DIR="${ROOT_DIR}/build/${PLATFORM}-${ARCHITECTURE}/${MODE}"
fi
BUILD_DIR="${FELIDAE_BUILD_DIR:-$DEFAULT_BUILD_DIR}"
CMAKE_ARGS=(
    -S "$ROOT_DIR"
    -B "$BUILD_DIR"
    "-DCMAKE_BUILD_TYPE=${CONFIGURATION}"
    "-DFELIDAE_BUILD_TESTS=${BUILD_TESTS}"
    "-DFELIDAE_ENABLE_LIBTORCH=${ENABLE_LIBTORCH}"
    "-DFELIDAE_ENABLE_TRAINING=${ENABLE_TRAINING}"
)
CAN_RUN_TESTS=1
case "$PLATFORM" in
    macos)
        case "$ARCHITECTURE" in
            arm64) CMAKE_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=arm64") ;;
            x86_64) CMAKE_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=x86_64") ;;
            *) echo "Unsupported macOS architecture '$ARCHITECTURE'" >&2; exit 2 ;;
        esac
        ;;
    android)
        NDK_DIR="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
        if [[ -z "$NDK_DIR" || ! -f "$NDK_DIR/build/cmake/android.toolchain.cmake" ]]; then
            echo "Android builds require ANDROID_NDK_HOME (or ANDROID_NDK_ROOT)" >&2
            exit 2
        fi
        case "$ARCHITECTURE" in
            arm64) ANDROID_ABI=arm64-v8a ;;
            armv7) ANDROID_ABI=armeabi-v7a ;;
            x86_64) ANDROID_ABI=x86_64 ;;
            *) echo "Unsupported Android architecture '$ARCHITECTURE'" >&2; exit 2 ;;
        esac
        CMAKE_ARGS+=(
            "-DCMAKE_TOOLCHAIN_FILE=${NDK_DIR}/build/cmake/android.toolchain.cmake"
            "-DANDROID_ABI=${ANDROID_ABI}"
            "-DANDROID_PLATFORM=android-${ANDROID_API}"
        )
        CAN_RUN_TESTS=0
        ;;
esac
if [[ -n "${FELIDAE_TOOLCHAIN_FILE:-}" ]]; then
    CMAKE_ARGS+=("-DCMAKE_TOOLCHAIN_FILE=${FELIDAE_TOOLCHAIN_FILE}")
    CAN_RUN_TESTS=0
fi
if [[ "$ENABLE_LIBTORCH" == ON ]]; then
    if [[ "$PLATFORM" != linux || "$ARCHITECTURE" != "$HOST_ARCHITECTURE" ]]; then
        if [[ -z "${FELIDAE_LIBTORCH_PATH:-}" ]]; then
            echo "Cross-platform LibTorch builds require FELIDAE_LIBTORCH_PATH; use --libtorch OFF only when LibTorch is unavailable for the target" >&2
            exit 2
        fi
    fi
    CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=${FELIDAE_LIBTORCH_PATH:-/opt/libtorch}")
fi
if [[ "$SANITIZE" -eq 1 ]]; then
    CMAKE_ARGS+=("-DFELIDAE_ENABLE_SANITIZERS=ON")
fi

echo "Felidae platform=${PLATFORM} arch=${ARCHITECTURE} mode=${MODE} configuration=${CONFIGURATION} training=${ENABLE_TRAINING} libtorch=${ENABLE_LIBTORCH} jobs=${JOBS}"
cmake "${CMAKE_ARGS[@]}"

if [[ "$MODE" == test ]]; then
    cmake --build "$BUILD_DIR" --config "$CONFIGURATION" \
        --target felidae_compiler felidae_vm felidae_tests --parallel "$JOBS"
    if [[ "$CAN_RUN_TESTS" -eq 1 ]]; then
        ctest --test-dir "$BUILD_DIR" --build-config "$CONFIGURATION" --output-on-failure
    else
        echo "Cross-compiled tests were built but not executed on the host"
    fi
else
    cmake --build "$BUILD_DIR" --config "$CONFIGURATION" \
        --target felidae_dist --parallel "$JOBS"
fi
