#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

need_cmd() { command -v "$1" >/dev/null 2>&1; }

detect_os_id() {
    if [[ "$(uname -s)" == "Darwin" ]]; then echo "darwin"; return; fi
    if [[ -r /etc/os-release ]]; then . /etc/os-release; echo "${ID:-unknown}"; else uname -s | tr '[:upper:]' '[:lower:]'; fi
}

run_install() {
    local os_id="$1"
    case "$os_id" in
        debian|ubuntu|linuxmint|pop)
            echo "Install command: sudo apt-get update && sudo apt-get install -y clang make"
            read -r -p "clang++ is missing. Install build dependencies with sudo now? [y/N] " answer
            [[ "$answer" =~ ^[Yy]$ ]] || return 1
            sudo apt-get update
            sudo apt-get install -y clang make
            ;;
        fedora)
            echo "Install command: sudo dnf install -y clang make gcc-c++"
            read -r -p "clang++ is missing. Install build dependencies with sudo now? [y/N] " answer
            [[ "$answer" =~ ^[Yy]$ ]] || return 1
            sudo dnf install -y clang make gcc-c++
            ;;
        opensuse*|sles)
            echo "Install command: sudo zypper install -y clang make gcc-c++"
            read -r -p "clang++ is missing. Install build dependencies with sudo now? [y/N] " answer
            [[ "$answer" =~ ^[Yy]$ ]] || return 1
            sudo zypper install -y clang make gcc-c++
            ;;
        arch|manjaro)
            echo "Install command: sudo pacman -Sy --needed clang make base-devel"
            read -r -p "clang++ is missing. Install build dependencies with sudo now? [y/N] " answer
            [[ "$answer" =~ ^[Yy]$ ]] || return 1
            sudo pacman -Sy --needed clang make base-devel
            ;;
        darwin)
            if need_cmd xcode-select; then
                echo "Install command: xcode-select --install"
                read -r -p "clang++ is missing. Install Apple Command Line Tools now? [y/N] " answer
                [[ "$answer" =~ ^[Yy]$ ]] || return 1
                xcode-select --install || true
                echo "After the Apple installer completes, rerun ./build.sh."
                return 1
            fi
            if need_cmd brew; then
                echo "Install command: brew install llvm"
                read -r -p "clang++ is missing. Install LLVM with Homebrew now? [y/N] " answer
                [[ "$answer" =~ ^[Yy]$ ]] || return 1
                brew install llvm
                return 0
            fi
            echo "Install Apple Command Line Tools with: xcode-select --install" >&2
            return 1
            ;;
        *)
            echo "Unsupported automatic dependency install for '$os_id'. Install clang++ manually and rerun ./build.sh." >&2
            return 1
            ;;
    esac
}

ensure_dependencies() {
    if need_cmd clang++; then return 0; fi
    local os_id
    os_id="$(detect_os_id)"
    run_install "$os_id"
    need_cmd clang++ || { echo "clang++ is still unavailable after dependency setup." >&2; exit 1; }
}

target_ext() {
    case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) echo ".exe" ;; *) echo "" ;; esac
}

android_clangxx() {
    local api="${ANDROID_API:-24}"
    local ndk="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
    local host_tag
    [[ -n "$ndk" ]] || return 1
    case "$(uname -s)" in
        Darwin) host_tag="darwin-x86_64" ;;
        Linux) host_tag="linux-x86_64" ;;
        MINGW*|MSYS*|CYGWIN*) host_tag="windows-x86_64" ;;
        *) return 1 ;;
    esac
    case "${ANDROID_ABI:-arm64-v8a}" in
        arm64-v8a) echo "$ndk/toolchains/llvm/prebuilt/$host_tag/bin/aarch64-linux-android${api}-clang++" ;;
        armeabi-v7a) echo "$ndk/toolchains/llvm/prebuilt/$host_tag/bin/armv7a-linux-androideabi${api}-clang++" ;;
        x86_64) echo "$ndk/toolchains/llvm/prebuilt/$host_tag/bin/x86_64-linux-android${api}-clang++" ;;
        x86) echo "$ndk/toolchains/llvm/prebuilt/$host_tag/bin/i686-linux-android${api}-clang++" ;;
        *) return 1 ;;
    esac
}

TARGET="${1:-native}"
if [[ "$TARGET" == "--target" ]]; then TARGET="${2:-native}"; fi
if [[ "$TARGET" == "wasm" ]]; then
    if ! need_cmd em++; then
        echo "WASM build requires Emscripten. Install and activate the Emscripten SDK so em++ is on PATH." >&2
        echo "Example: ./emsdk install latest && ./emsdk activate latest && source ./emsdk_env.sh" >&2
        exit 1
    fi
else
    if [[ "$TARGET" != "android" ]]; then ensure_dependencies; fi
fi

mkdir -p build
EXT="$(target_ext)"
FELIDAE="build/felidae${EXT}"
CELIDAE="build/celidae${EXT}"
FELIDAE_DEBUG="build/felidae_debug${EXT}"
CXX="clang++"

if [[ "$TARGET" == "android" ]]; then
    CXX="$(android_clangxx)" || {
        echo "Android build requires ANDROID_NDK_HOME or ANDROID_NDK_ROOT and a supported ANDROID_ABI." >&2
        echo "Example: ANDROID_NDK_HOME=/opt/android-ndk ANDROID_ABI=arm64-v8a ./build.sh --target android" >&2
        exit 1
    }
    FELIDAE="build/felidae-android-${ANDROID_ABI:-arm64-v8a}"
    CELIDAE="build/celidae-android-${ANDROID_ABI:-arm64-v8a}"
    FELIDAE_DEBUG="build/felidae_debug-android-${ANDROID_ABI:-arm64-v8a}"
fi

COMMON_SOURCES=(
    src/FelidaeRuntime.cpp src/Visualization.cpp src/Lexer.cpp src/Parser.cpp
    src/Interpreter.cpp src/Env.cpp src/Memory.cpp
    native_modules/csv/NativeCsv.cpp native_modules/http/NativeHttp.cpp native_modules/process/NativeProcess.cpp
)
DEBUG_SOURCES=("${COMMON_SOURCES[@]}" src/AstAnalyzer.cpp)
read -r -a EXTRA_LIBS <<< "${FELIDAE_DLOPEN_LIBS:-}"

if [[ "$TARGET" == "wasm" ]]; then
    mkdir -p docs/wasm
    echo "Building docs/wasm/felidae_wasm.js"
    em++ -std=c++17 -O2 -fexceptions -Isrc -Ithird_party \
        src/felidae_wasm.cpp "${COMMON_SOURCES[@]}" \
        --bind \
        -s MODULARIZE=1 \
        -s "EXPORT_NAME='FelidaeWasm'" \
        -s ENVIRONMENT=web \
        -s ALLOW_MEMORY_GROWTH=1 \
        -s DISABLE_EXCEPTION_CATCHING=0 \
        -s ASSERTIONS=1 \
        --preload-file core@/core \
        -o docs/wasm/felidae_wasm.js
    echo "Built docs/wasm/felidae_wasm.js, docs/wasm/felidae_wasm.wasm, and docs/wasm/felidae_wasm.data"
    exit 0
fi
echo "Building $FELIDAE"
"$CXX" -std=c++17 -Wall -Wextra -Isrc -Ithird_party src/main.cpp "${COMMON_SOURCES[@]}" -o "$FELIDAE" "${EXTRA_LIBS[@]}"

echo "Building $CELIDAE"
"$CXX" -std=c++17 -Wall -Wextra -Isrc -Ithird_party src/felidae_debug.cpp "${DEBUG_SOURCES[@]}" -o "$CELIDAE" "${EXTRA_LIBS[@]}"

echo "Building $FELIDAE_DEBUG"
"$CXX" -std=c++17 -Wall -Wextra -Isrc -Ithird_party src/felidae_debug.cpp "${DEBUG_SOURCES[@]}" -o "$FELIDAE_DEBUG" "${EXTRA_LIBS[@]}"

echo "Built $FELIDAE, $CELIDAE, and $FELIDAE_DEBUG"

