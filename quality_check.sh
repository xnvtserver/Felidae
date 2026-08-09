#!/usr/bin/env bash

set -u
set -o pipefail

# ============================================================
# Felidae C++ Quality Check
#
# Runs static analysis, compiler diagnostics, sanitizers,
# Valgrind, tests, coverage helpers, and lightweight profiling.
#
# Designed to be safe on development machines:
#   - single-job builds by default
#   - low process priority
#   - no automatic stress testing
#   - expensive checks are optional
#
# Usage:
#
#   ./quality_check.sh
#   ./quality_check.sh --full
#   ./quality_check.sh --valgrind
#   ./quality_check.sh --sanitize
#
# Reports:
#
#   quality_reports/
#
# ============================================================

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPORT_DIR="${PROJECT_ROOT}/quality_reports"

BUILD_DIR="${PROJECT_ROOT}/build-quality"
ASAN_BUILD_DIR="${PROJECT_ROOT}/build-quality-asan"
LOCAL_ABSEIL_SOURCE="${PROJECT_ROOT}/build-sentencepiece/_deps/abseil-cpp-src"
LOCAL_PROTOBUF_SOURCE="${PROJECT_ROOT}/build-sentencepiece/_deps/protobuf-src"

JOBS="${QUALITY_JOBS:-1}"

RUN_FULL=0
RUN_VALGRIND=0
RUN_SANITIZERS=0

mkdir -p "${REPORT_DIR}"

TIMESTAMP="$(date '+%Y-%m-%d_%H-%M-%S')"
SUMMARY="${REPORT_DIR}/summary_${TIMESTAMP}.txt"

touch "${SUMMARY}"

log() {
    echo "[$(date '+%H:%M:%S')] $*" | tee -a "${SUMMARY}"
}

section() {
    echo
    echo "============================================================"
    echo "$*"
    echo "============================================================"
}

command_exists() {
    command -v "$1" >/dev/null 2>&1
}

run_safe() {
    local name="$1"
    shift

    section "${name}"
    log "Running: $*"

    if nice -n 10 "$@"; then
        log "${name}: PASS"
        return 0
    else
        local rc=$?
        log "${name}: FAIL (exit=${rc})"
        return "${rc}"
    fi
}

for arg in "$@"; do
    case "${arg}" in
        --full)
            RUN_FULL=1
            RUN_VALGRIND=1
            RUN_SANITIZERS=1
            ;;
        --valgrind)
            RUN_VALGRIND=1
            ;;
        --sanitize|--sanitizers)
            RUN_SANITIZERS=1
            ;;
        *)
            echo "Unknown option: ${arg}"
            echo "Usage: $0 [--full] [--valgrind] [--sanitize]"
            exit 2
            ;;
    esac
done

cd "${PROJECT_ROOT}" || exit 1

section "Felidae C++ Quality Analysis"

log "Project root: ${PROJECT_ROOT}"
log "Reports: ${REPORT_DIR}"
log "Build jobs: ${JOBS}"
log "Full mode: ${RUN_FULL}"
log "Sanitizers: ${RUN_SANITIZERS}"
log "Valgrind: ${RUN_VALGRIND}"

# ------------------------------------------------------------
# Environment information
# ------------------------------------------------------------

{
    echo "=== SYSTEM ==="
    uname -a

    echo
    echo "=== COMPILER ==="

    command_exists g++ && g++ --version | head -1
    command_exists clang++ && clang++ --version | head -1

    echo
    echo "=== CMAKE ==="
    command_exists cmake && cmake --version | head -1

    echo
    echo "=== TOOLS ==="

    for tool in \
        cppcheck \
        clang-tidy \
        clang-format \
        valgrind \
        gcovr \
        lcov \
        perf \
        gdb
    do
        if command_exists "${tool}"; then
            echo "${tool}: FOUND"
        else
            echo "${tool}: MISSING"
        fi
    done
} > "${REPORT_DIR}/environment.txt"

# ------------------------------------------------------------
# Source discovery
# ------------------------------------------------------------

mapfile -t CPP_FILES < <(
    find "${PROJECT_ROOT}" \
        \( -path "${PROJECT_ROOT}/build*" \
           -o -path "${PROJECT_ROOT}/third_party" \
           -o -path "${PROJECT_ROOT}/src/celidae" \
           -o -path "${PROJECT_ROOT}/.git" \
           -o -path "${PROJECT_ROOT}/quality_reports" \) \
        -prune \
        -o \
        -type f \
        \( -name '*.cpp' -o -name '*.cc' -o -name '*.cxx' \) \
        -print
)

mapfile -t HEADER_FILES < <(
    find "${PROJECT_ROOT}" \
        \( -path "${PROJECT_ROOT}/build*" \
           -o -path "${PROJECT_ROOT}/third_party" \
           -o -path "${PROJECT_ROOT}/src/celidae" \
           -o -path "${PROJECT_ROOT}/.git" \
           -o -path "${PROJECT_ROOT}/quality_reports" \) \
        -prune \
        -o \
        -type f \
        \( -name '*.h' -o -name '*.hpp' -o -name '*.hh' \) \
        -print
)

log "C++ source files: ${#CPP_FILES[@]}"
log "Header files: ${#HEADER_FILES[@]}"

printf '%s\n' "${CPP_FILES[@]}" > "${REPORT_DIR}/cpp_files.txt"
printf '%s\n' "${HEADER_FILES[@]}" > "${REPORT_DIR}/header_files.txt"

# ------------------------------------------------------------
# CMake quality build
# ------------------------------------------------------------

if command_exists cmake; then
    section "CMake configure"

    # SentencePiece fetches Abseil when configuring a new build directory.
    # Reuse the existing local source tree when available so quality checks do
    # not require network access or retry a failed clone.
    CMAKE_CONFIGURE_ARGS=(
        -S "${PROJECT_ROOT}"
        -B "${BUILD_DIR}"
        -DCMAKE_BUILD_TYPE=Debug
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    )
    if [ -d "${LOCAL_ABSEIL_SOURCE}" ]; then
        CMAKE_CONFIGURE_ARGS+=(
            "-DFETCHCONTENT_SOURCE_DIR_ABSEIL-CPP=${LOCAL_ABSEIL_SOURCE}"
        )
        log "Using local Abseil source: ${LOCAL_ABSEIL_SOURCE}"
    fi
    if [ -d "${LOCAL_PROTOBUF_SOURCE}" ]; then
        CMAKE_CONFIGURE_ARGS+=(
            "-DFETCHCONTENT_SOURCE_DIR_PROTOBUF=${LOCAL_PROTOBUF_SOURCE}"
        )
        log "Using local Protobuf source: ${LOCAL_PROTOBUF_SOURCE}"
    fi

    nice -n 10 cmake \
        "${CMAKE_CONFIGURE_ARGS[@]}" \
        > "${REPORT_DIR}/cmake_configure.txt" 2>&1

    CMAKE_CONFIG_RESULT=$?

    if [ "${CMAKE_CONFIG_RESULT}" -eq 0 ]; then
        log "CMake configure: PASS"

        section "Compiler build diagnostics"

        nice -n 10 cmake \
            --build "${BUILD_DIR}" \
            --parallel "${JOBS}" \
            > "${REPORT_DIR}/build.txt" 2>&1

        BUILD_RESULT=$?

        if [ "${BUILD_RESULT}" -eq 0 ]; then
            log "Normal build: PASS"
        else
            log "Normal build: FAIL (${BUILD_RESULT})"
        fi
    else
        log "CMake configure: FAIL (${CMAKE_CONFIG_RESULT})"
    fi
else
    log "cmake unavailable; skipping build."
fi

# ------------------------------------------------------------
# cppcheck
# ------------------------------------------------------------

if command_exists cppcheck; then
    section "cppcheck"

    nice -n 10 cppcheck \
        --enable=all \
        --inconclusive \
        --force \
        --std=c++20 \
        --language=c++ \
        --inline-suppr \
        --suppress=missingIncludeSystem \
        --suppress=unusedFunction \
        --template='{file}:{line}:{column}: [{severity}] {id}: {message}' \
        -j "${JOBS}" \
        "${PROJECT_ROOT}/src" \
        "${PROJECT_ROOT}/tests" \
        2> "${REPORT_DIR}/cppcheck.txt"

    CPPCHECK_RESULT=$?

    log "cppcheck finished with exit ${CPPCHECK_RESULT}"
else
    log "cppcheck missing."
fi

# ------------------------------------------------------------
# clang-tidy
# ------------------------------------------------------------

if command_exists clang-tidy && [ -f "${BUILD_DIR}/compile_commands.json" ]; then
    section "clang-tidy"

    : > "${REPORT_DIR}/clang_tidy.txt"

    CLANG_TIDY_FAILURES=0

    for file in "${CPP_FILES[@]}"; do
        log "clang-tidy: ${file#${PROJECT_ROOT}/}"

        nice -n 10 clang-tidy \
            "${file}" \
            -p "${BUILD_DIR}" \
            --quiet \
            >> "${REPORT_DIR}/clang_tidy.txt" 2>&1

        rc=$?

        if [ "${rc}" -ne 0 ]; then
            CLANG_TIDY_FAILURES=$((CLANG_TIDY_FAILURES + 1))
        fi
    done

    log "clang-tidy files with non-zero result: ${CLANG_TIDY_FAILURES}"
else
    log "clang-tidy or compile_commands.json unavailable."
fi

# ------------------------------------------------------------
# clang-format audit
# ------------------------------------------------------------

if command_exists clang-format; then
    section "clang-format"

    : > "${REPORT_DIR}/clang_format.txt"

    FORMAT_FAILURES=0

    for file in "${CPP_FILES[@]}" "${HEADER_FILES[@]}"; do

        [ -f "${file}" ] || continue

        if ! clang-format --dry-run --Werror "${file}" \
            >> "${REPORT_DIR}/clang_format.txt" 2>&1
        then
            echo "FORMAT ISSUE: ${file#${PROJECT_ROOT}/}" \
                >> "${REPORT_DIR}/clang_format.txt"

            FORMAT_FAILURES=$((FORMAT_FAILURES + 1))
        fi
    done

    log "clang-format files requiring changes: ${FORMAT_FAILURES}"
else
    log "clang-format missing."
fi

# ------------------------------------------------------------
# TODO / FIXME / dangerous-pattern audit
# ------------------------------------------------------------

section "Source pattern audit"

{
    echo "=== TODO / FIXME / HACK ==="

    grep -RInE \
        --exclude-dir=.git \
        --exclude-dir=third_party \
        --exclude-dir=build \
        --exclude-dir=build-quality \
        --exclude-dir=quality_reports \
        '\b(TODO|FIXME|HACK|XXX)\b' \
        src tests 2>/dev/null || true

    echo
    echo "=== POSSIBLY DANGEROUS C/C++ APIs ==="

    grep -RInE \
        --exclude-dir=.git \
        --exclude-dir=third_party \
        --exclude-dir=build \
        --exclude-dir=build-quality \
        --exclude-dir=quality_reports \
        '\b(strcpy|strcat|sprintf|gets|scanf|memcpy|memmove|malloc|calloc|realloc|free)\s*\(' \
        src tests 2>/dev/null || true

    echo
    echo "=== RAW NEW / DELETE ==="

    grep -RInE \
        --exclude-dir=.git \
        --exclude-dir=third_party \
        --exclude-dir=build \
        --exclude-dir=build-quality \
        --exclude-dir=quality_reports \
        '\b(new|delete|delete\[\])\b' \
        src tests 2>/dev/null || true

    echo
    echo "=== THREADING / LOCKING ==="

    grep -RInE \
        --exclude-dir=.git \
        --exclude-dir=third_party \
        --exclude-dir=build \
        --exclude-dir=build-quality \
        --exclude-dir=quality_reports \
        'std::thread|std::jthread|std::async|mutex|lock_guard|unique_lock|shared_mutex|atomic<' \
        src tests 2>/dev/null || true

    echo
    echo "=== POSSIBLE BUSY LOOPS ==="

    grep -RInE \
        --exclude-dir=.git \
        --exclude-dir=third_party \
        --exclude-dir=build \
        --exclude-dir=build-quality \
        --exclude-dir=quality_reports \
        'while\s*\(\s*(true|1)\s*\)|for\s*\(\s*;\s*;\s*\)' \
        src tests 2>/dev/null || true

} > "${REPORT_DIR}/source_patterns.txt"

log "Source pattern audit complete."

# ------------------------------------------------------------
# Tests
# ------------------------------------------------------------

if [ -d "${BUILD_DIR}" ]; then
    section "CTest"

    (
        cd "${BUILD_DIR}" || exit 1

        nice -n 10 ctest \
            --output-on-failure \
            -j "${JOBS}"
    ) > "${REPORT_DIR}/ctest.txt" 2>&1

    CTEST_RESULT=$?

    if [ "${CTEST_RESULT}" -eq 0 ]; then
        log "Tests: PASS"
    else
        log "Tests: FAIL (${CTEST_RESULT})"
    fi
fi

# ------------------------------------------------------------
# ASan + UBSan
# ------------------------------------------------------------

if [ "${RUN_SANITIZERS}" -eq 1 ] && command_exists cmake; then

    section "AddressSanitizer + UndefinedBehaviorSanitizer"

    SAN_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g"

    nice -n 10 cmake \
        -S "${PROJECT_ROOT}" \
        -B "${ASAN_BUILD_DIR}" \
        -DCMAKE_BUILD_TYPE=Debug \
        "-DCMAKE_CXX_FLAGS=${SAN_FLAGS}" \
        "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined" \
        > "${REPORT_DIR}/asan_configure.txt" 2>&1

    if [ "$?" -eq 0 ]; then

        nice -n 10 cmake \
            --build "${ASAN_BUILD_DIR}" \
            --parallel "${JOBS}" \
            > "${REPORT_DIR}/asan_build.txt" 2>&1

        if [ "$?" -eq 0 ]; then

            (
                cd "${ASAN_BUILD_DIR}" || exit 1

                ASAN_OPTIONS="detect_leaks=1:halt_on_error=0" \
                UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=0" \
                nice -n 10 ctest \
                    --output-on-failure \
                    -j "${JOBS}"

            ) > "${REPORT_DIR}/asan_tests.txt" 2>&1

            log "ASan/UBSan tests completed."
        else
            log "ASan build failed."
        fi
    else
        log "ASan configure failed."
    fi
fi

# ------------------------------------------------------------
# Valgrind
# ------------------------------------------------------------

if [ "${RUN_VALGRIND}" -eq 1 ]; then

    if command_exists valgrind; then

        section "Valgrind"

        FELIDAE_BINARY=""

        for candidate in \
            "${BUILD_DIR}/felidae" \
            "${BUILD_DIR}/bin/felidae"
        do
            if [ -x "${candidate}" ]; then
                FELIDAE_BINARY="${candidate}"
                break
            fi
        done

        if [ -n "${FELIDAE_BINARY}" ]; then

            SAMPLE_FILE=""

            for candidate in \
                "${PROJECT_ROOT}/examples/main.fx" \
                "${PROJECT_ROOT}/examples/hello.fx" \
                "${PROJECT_ROOT}/main.fx"
            do
                if [ -f "${candidate}" ]; then
                    SAMPLE_FILE="${candidate}"
                    break
                fi
            done

            if [ -n "${SAMPLE_FILE}" ]; then

                nice -n 15 valgrind \
                    --tool=memcheck \
                    --leak-check=full \
                    --show-leak-kinds=all \
                    --track-origins=yes \
                    --errors-for-leak-kinds=definite,indirect \
                    --error-exitcode=42 \
                    "${FELIDAE_BINARY}" \
                    "${SAMPLE_FILE}" \
                    > "${REPORT_DIR}/valgrind_stdout.txt" \
                    2> "${REPORT_DIR}/valgrind.txt"

                VALGRIND_RESULT=$?

                log "Valgrind exit: ${VALGRIND_RESULT}"

            else
                log "No safe example .fx file found; Valgrind execution skipped."
            fi

        else
            log "Felidae executable not found in quality build."
        fi

    else
        log "Valgrind missing."
    fi
fi

# ------------------------------------------------------------
# Lightweight size/statistics report
# ------------------------------------------------------------

section "Code statistics"

{
    echo "C++ files: ${#CPP_FILES[@]}"
    echo "Header files: ${#HEADER_FILES[@]}"

    echo
    echo "Lines of source:"

    find src tests \
        -type f \
        \( -name '*.cpp' \
           -o -name '*.cc' \
           -o -name '*.cxx' \
           -o -name '*.h' \
           -o -name '*.hpp' \) \
        -print0 2>/dev/null \
        | xargs -0 wc -l 2>/dev/null \
        | tail -1 || true

} > "${REPORT_DIR}/code_statistics.txt"

# ------------------------------------------------------------
# Produce Codex-friendly consolidated report
# ------------------------------------------------------------

CONSOLIDATED="${REPORT_DIR}/CODEX_REVIEW.txt"

{
    echo "============================================================"
    echo "FELIDAE C++ QUALITY REPORT"
    echo "Generated: $(date)"
    echo "============================================================"

    for report in \
        environment.txt \
        build.txt \
        cppcheck.txt \
        clang_tidy.txt \
        clang_format.txt \
        source_patterns.txt \
        ctest.txt \
        asan_tests.txt \
        valgrind.txt \
        code_statistics.txt
    do

        path="${REPORT_DIR}/${report}"

        if [ -f "${path}" ]; then
            echo
            echo
            echo "============================================================"
            echo "REPORT: ${report}"
            echo "============================================================"

            cat "${path}"
        fi

    done

} > "${CONSOLIDATED}"

section "Quality analysis complete"

log "Summary: ${SUMMARY}"
log "Consolidated Codex report: ${CONSOLIDATED}"

echo
echo "Review:"
echo "  ${CONSOLIDATED}"
echo
echo "Recommended Codex instruction:"
echo
echo "  Review quality_reports/CODEX_REVIEW.txt together with the"
echo "  referenced source files. Fix confirmed issues without changing"
echo "  Felidae semantics or architecture. Prioritize correctness,"
echo "  undefined behavior, memory safety, parser termination, races,"
echo "  unnecessary allocations, and measured performance problems."
echo

exit 0
