#!/usr/bin/env bash
set -euo pipefail

# Felidae has one native build graph: CMake owns SentencePiece, generated
# grammar IDs, the integer assembler, and native modules.  Do not add a
# parallel direct-compiler source list here.
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${FELIDAE_BUILD_DIR:-${ROOT_DIR}/build}"
CONFIGURATION="Release"
TARGET="native"
SANITIZE=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --configuration)
            case "${2:-}" in
                debug) CONFIGURATION="Debug" ;;
                release|production) CONFIGURATION="Release" ;;
                sanitize) CONFIGURATION="Debug"; SANITIZE=1 ;;
                *) echo "Unknown configuration: ${2:-}" >&2; exit 2 ;;
            esac
            shift 2
            ;;
        --target)
            TARGET="${2:-native}"
            shift 2
            ;;
        native|debug|release|production|sanitize)
            if [[ "$1" == "native" ]]; then TARGET="native"; else
                case "$1" in
                    debug) CONFIGURATION="Debug" ;;
                    sanitize) CONFIGURATION="Debug"; SANITIZE=1 ;;
                    *) CONFIGURATION="Release" ;;
                esac
            fi
            shift
            ;;
        --warnings-as-errors)
            # CMake keeps project warning policy authoritative.
            shift
            ;;
        *) echo "Unknown build option: $1" >&2; exit 2 ;;
    esac
done

if [[ "$TARGET" != "native" ]]; then
    echo "Unsupported target '$TARGET': the retired direct compiler path cannot build SentencePiece." >&2
    echo "Provide a CMake toolchain/preset for that platform, then run CMake directly." >&2
    exit 2
fi

LOCAL_ABSEIL="${ROOT_DIR}/build-sentencepiece/_deps/abseil-cpp-src"
LOCAL_PROTOBUF="${ROOT_DIR}/build-sentencepiece/_deps/protobuf-src"
CMAKE_ARGS=(-S "$ROOT_DIR" -B "$BUILD_DIR" "-DCMAKE_BUILD_TYPE=${CONFIGURATION}")
if [[ "$SANITIZE" -eq 1 ]]; then
    CMAKE_ARGS+=("-DFELIDAE_ENABLE_SANITIZERS=ON")
fi
if [[ -d "$LOCAL_ABSEIL" ]]; then
    CMAKE_ARGS+=("-DFETCHCONTENT_SOURCE_DIR_ABSEIL-CPP=${LOCAL_ABSEIL}")
fi
if [[ -d "$LOCAL_PROTOBUF" ]]; then
    CMAKE_ARGS+=("-DFETCHCONTENT_SOURCE_DIR_PROTOBUF=${LOCAL_PROTOBUF}")
fi

nice -n 19 cmake "${CMAKE_ARGS[@]}"
nice -n 19 cmake --build "$BUILD_DIR" --parallel "${FELIDAE_JOBS:-1}"
