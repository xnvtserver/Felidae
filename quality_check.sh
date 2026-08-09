#!/usr/bin/env bash

set -u
set -o pipefail

# ============================================================
# Felidae C++ Quality Check
#
# Focus:
#   - compiler diagnostics
#   - dead/unreachable/unused code
#   - unsafe string and buffer handling
#   - invalid allocation/lifetime/ownership patterns
#   - undefined behaviour
#   - static analysis
#   - sanitizer and Valgrind checks
#   - optimization/performance smells
#
# Celidae is intentionally excluded from all source analysis.
#
# Safety:
#   - one build/analyzer job by default
#   - low process priority
#   - no automatic stress tests
#   - expensive runtime checks are opt-in
#
# Usage:
#   ./quality_check.sh
#   ./quality_check.sh --sanitize
#   ./quality_check.sh --valgrind
#   ./quality_check.sh --analyze
#   ./quality_check.sh --full
#
# Environment overrides:
#   QUALITY_JOBS=1
#   QUALITY_NICE=10
#
# Reports:
#   quality_reports/
#   quality_reports/CODEX_REVIEW.txt
# ============================================================

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPORT_DIR="${PROJECT_ROOT}/quality_reports"

BUILD_DIR="${PROJECT_ROOT}/build-quality"
ASAN_BUILD_DIR="${PROJECT_ROOT}/build-quality-asan"
ANALYZE_BUILD_DIR="${PROJECT_ROOT}/build-quality-analyze"

LOCAL_ABSEIL_SOURCE="${PROJECT_ROOT}/build-sentencepiece/_deps/abseil-cpp-src"
LOCAL_PROTOBUF_SOURCE="${PROJECT_ROOT}/build-sentencepiece/_deps/protobuf-src"

JOBS="${QUALITY_JOBS:-1}"
NICE_LEVEL="${QUALITY_NICE:-10}"

RUN_FULL=0
RUN_VALGRIND=0
RUN_SANITIZERS=0
RUN_ANALYZER=0

# The normal gate deliberately validates only the active interpreter pipeline.
# Building every optional native module and the SentencePiece trainer makes a
# routine quality pass needlessly hot while adding no coverage to its default
# regression suite.  Those targets remain available through --full.
CORE_BUILD_TARGETS=(felidae felidae_sentencepiece_model_test)

mkdir -p "${REPORT_DIR}"

TIMESTAMP="$(date '+%Y-%m-%d_%H-%M-%S')"
SUMMARY="${REPORT_DIR}/summary_${TIMESTAMP}.txt"
CONSOLIDATED="${REPORT_DIR}/CODEX_REVIEW.txt"

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

safe_run() {
    local name="$1"
    local output="$2"
    shift 2

    section "${name}"
    log "Running: $*"

    if nice -n "${NICE_LEVEL}" "$@" >"${output}" 2>&1; then
        log "${name}: PASS"
        return 0
    else
        local rc=$?
        log "${name}: FAIL (exit=${rc})"
        return "${rc}"
    fi
}

is_valid_positive_integer() {
    [[ "$1" =~ ^[1-9][0-9]*$ ]]
}

if ! is_valid_positive_integer "${JOBS}"; then
    echo "QUALITY_JOBS must be a positive integer." >&2
    exit 2
fi

if ! [[ "${NICE_LEVEL}" =~ ^-?[0-9]+$ ]]; then
    echo "QUALITY_NICE must be an integer." >&2
    exit 2
fi

for arg in "$@"; do
    case "${arg}" in
        --full)
            RUN_FULL=1
            RUN_VALGRIND=1
            RUN_SANITIZERS=1
            RUN_ANALYZER=1
            ;;
        --valgrind)
            RUN_VALGRIND=1
            ;;
        --sanitize|--sanitizers)
            RUN_SANITIZERS=1
            ;;
        --analyze|--analyzer)
            RUN_ANALYZER=1
            ;;
        *)
            echo "Unknown option: ${arg}" >&2
            echo "Usage: $0 [--full] [--valgrind] [--sanitize] [--analyze]" >&2
            exit 2
            ;;
    esac
done

cd "${PROJECT_ROOT}" || exit 1

section "Felidae C++ Quality Analysis"

log "Project root: ${PROJECT_ROOT}"
log "Reports: ${REPORT_DIR}"
log "Build/analyzer jobs: ${JOBS}"
log "Nice level: ${NICE_LEVEL}"
log "Sanitizers: ${RUN_SANITIZERS}"
log "Valgrind: ${RUN_VALGRIND}"
log "Clang analyzer: ${RUN_ANALYZER}"
log "Celidae: EXCLUDED"

# ------------------------------------------------------------
# Shared exclusions
# ------------------------------------------------------------

# Keep these synchronized for find/grep/analyzers.
EXCLUDED_DIR_NAMES=(
    ".git"
    "third_party"
    "quality_reports"
    "src/celidae"
)

# Build trees are excluded by pattern separately.
is_excluded_file() {
    local file="$1"

    case "${file}" in
        "${PROJECT_ROOT}/src/celidae/"* | \
        "${PROJECT_ROOT}/third_party/"* | \
        "${PROJECT_ROOT}/quality_reports/"* | \
        "${PROJECT_ROOT}/.git/"* | \
        "${PROJECT_ROOT}/build"*/*)
            return 0
            ;;
    esac

    return 1
}

grep_project() {
    grep -RInE \
        --exclude-dir=.git \
        --exclude-dir=third_party \
        --exclude-dir=quality_reports \
        --exclude-dir=celidae \
        --exclude-dir=build \
        --exclude-dir=build-quality \
        --exclude-dir=build-quality-asan \
        --exclude-dir=build-quality-analyze \
        --exclude='*.min.*' \
        "$@" \
        src tests 2>/dev/null || true
}

# ------------------------------------------------------------
# Environment
# ------------------------------------------------------------

{
    echo "=== SYSTEM ==="
    uname -a

    echo
    echo "=== COMPILERS ==="
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
        scan-build \
        valgrind \
        gcovr \
        lcov \
        perf \
        gdb \
        nm \
        size \
        readelf
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
        \( -name '*.h' -o -name '*.hpp' -o -name '*.hh' -o -name '*.hxx' \) \
        -print
)

ALL_SOURCE_FILES=("${CPP_FILES[@]}" "${HEADER_FILES[@]}")

log "C++ source files: ${#CPP_FILES[@]}"
log "Header files: ${#HEADER_FILES[@]}"

printf '%s\n' "${CPP_FILES[@]}" > "${REPORT_DIR}/cpp_files.txt"
printf '%s\n' "${HEADER_FILES[@]}" > "${REPORT_DIR}/header_files.txt"

# ------------------------------------------------------------
# CMake configure helper
# ------------------------------------------------------------

cmake_configure_args() {
    local build_dir="$1"
    local build_type="$2"

    CMAKE_ARGS=(
        -S "${PROJECT_ROOT}"
        -B "${build_dir}"
        "-DCMAKE_BUILD_TYPE=${build_type}"
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    )

    if [ -d "${LOCAL_ABSEIL_SOURCE}" ]; then
        CMAKE_ARGS+=("-DFETCHCONTENT_SOURCE_DIR_ABSEIL-CPP=${LOCAL_ABSEIL_SOURCE}")
    fi

    if [ -d "${LOCAL_PROTOBUF_SOURCE}" ]; then
        CMAKE_ARGS+=("-DFETCHCONTENT_SOURCE_DIR_PROTOBUF=${LOCAL_PROTOBUF_SOURCE}")
    fi
}

# ------------------------------------------------------------
# Warning-heavy quality build
# ------------------------------------------------------------

BUILD_OK=0

if command_exists cmake; then
    section "CMake warning-heavy quality build"

    cmake_configure_args "${BUILD_DIR}" "Debug"

    CMAKE_ARGS+=(
        -DCMAKE_C_FLAGS=
        -DCMAKE_CXX_FLAGS=
        -DFELIDAE_ENABLE_STRICT_WARNINGS=ON
    )

    nice -n "${NICE_LEVEL}" cmake \
        "${CMAKE_ARGS[@]}" \
        > "${REPORT_DIR}/cmake_configure.txt" 2>&1

    CMAKE_CONFIG_RESULT=$?

    if [ "${CMAKE_CONFIG_RESULT}" -eq 0 ]; then
        log "CMake configure: PASS"

        build_args=(--build "${BUILD_DIR}" --parallel "${JOBS}")
        if [ "${RUN_FULL}" -eq 0 ]; then
            build_args+=(--target "${CORE_BUILD_TARGETS[@]}")
        fi

        nice -n "${NICE_LEVEL}" cmake \
            "${build_args[@]}" \
            > "${REPORT_DIR}/build.txt" 2>&1

        BUILD_RESULT=$?

        if [ "${BUILD_RESULT}" -eq 0 ]; then
            BUILD_OK=1
            log "Warning-heavy build: PASS"
        else
            log "Warning-heavy build: FAIL (${BUILD_RESULT})"
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

    CPPCHECK_ARGS=(
        --enable=all
        --inconclusive
        --force
        --std=c++20
        --language=c++
        --inline-suppr
        --suppress=missingIncludeSystem
        --template='{file}:{line}:{column}: [{severity}] {id}: {message}'
        -j "${JOBS}"
        "-i${PROJECT_ROOT}/src/celidae"
        "-i${PROJECT_ROOT}/third_party"
    )

    # Newer cppcheck versions support exhaustive checking. Add it only when
    # available to remain compatible with older Ubuntu packages.
    if cppcheck --help 2>&1 | grep -q -- '--check-level'; then
        CPPCHECK_ARGS+=(--check-level=exhaustive)
    fi

    nice -n "${NICE_LEVEL}" cppcheck \
        "${CPPCHECK_ARGS[@]}" \
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

if [ "${RUN_ANALYZER}" -eq 1 ] && command_exists clang-tidy && [ -f "${BUILD_DIR}/compile_commands.json" ]; then
    section "clang-tidy"

    : > "${REPORT_DIR}/clang_tidy.txt"

    CLANG_TIDY_FAILURES=0

    # These families target correctness, memory/lifetime, unsafe strings,
    # dead/redundant code and measurable performance smells.
    CLANG_TIDY_CHECKS='clang-analyzer-*,bugprone-*,performance-*,portability-*,cert-*,cppcoreguidelines-*,modernize-*,readability-redundant-*,-modernize-use-trailing-return-type,-cppcoreguidelines-avoid-magic-numbers,-readability-magic-numbers'

    for file in "${CPP_FILES[@]}"; do
        if is_excluded_file "${file}"; then
            continue
        fi

        log "clang-tidy: ${file#${PROJECT_ROOT}/}"

        nice -n "${NICE_LEVEL}" clang-tidy \
            "${file}" \
            -p "${BUILD_DIR}" \
            "-checks=${CLANG_TIDY_CHECKS}" \
            --quiet \
            >> "${REPORT_DIR}/clang_tidy.txt" 2>&1

        rc=$?

        if [ "${rc}" -ne 0 ]; then
            CLANG_TIDY_FAILURES=$((CLANG_TIDY_FAILURES + 1))
        fi
    done

    log "clang-tidy files with non-zero result: ${CLANG_TIDY_FAILURES}"
else
    log "clang-tidy skipped (use --analyze); or compile_commands.json unavailable."
fi

# ------------------------------------------------------------
# Clang Static Analyzer / scan-build
# ------------------------------------------------------------

if [ "${RUN_ANALYZER}" -eq 1 ]; then
    if command_exists scan-build && command_exists cmake; then
        section "Clang Static Analyzer"

        rm -rf "${ANALYZE_BUILD_DIR}"

        cmake_configure_args "${ANALYZE_BUILD_DIR}" "Debug"

        nice -n "${NICE_LEVEL}" scan-build \
            --status-bugs \
            -o "${REPORT_DIR}/scan-build" \
            cmake "${CMAKE_ARGS[@]}" \
            > "${REPORT_DIR}/scan_build_configure.txt" 2>&1

        if [ "$?" -eq 0 ]; then
            nice -n "${NICE_LEVEL}" scan-build \
                --status-bugs \
                -o "${REPORT_DIR}/scan-build" \
                cmake --build "${ANALYZE_BUILD_DIR}" --parallel "${JOBS}" \
                > "${REPORT_DIR}/scan_build.txt" 2>&1

            log "Clang Static Analyzer completed."
        else
            log "Clang Static Analyzer configure failed."
        fi
    else
        log "scan-build unavailable; static analyzer skipped."
    fi
fi

# ------------------------------------------------------------
# clang-format audit
# ------------------------------------------------------------

if command_exists clang-format; then
    section "clang-format"

    : > "${REPORT_DIR}/clang_format.txt"

    FORMAT_FAILURES=0

    for file in "${ALL_SOURCE_FILES[@]}"; do
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
# Dead / unused / unreachable code audit
# ------------------------------------------------------------

section "Dead and unreachable code audit"

{
    echo "=== TODO / FIXME / HACK / XXX ==="
    grep_project '\b(TODO|FIXME|HACK|XXX)\b'

    echo
    echo "=== UNUSED / DEAD CODE MARKERS ==="
    grep_project '\[\[maybe_unused\]\]|__attribute__\s*\(\(unused\)\)|#if\s+0|#ifdef\s+NEVER|UNUSED\('

    echo
    echo "=== SUSPICIOUS UNREACHABLE CODE ==="
    grep_project '\b(return|throw|break|continue)\b[^;]*;[[:space:]]*[A-Za-z_({]'

    echo
    echo "=== EMPTY / PLACEHOLDER IMPLEMENTATIONS ==="
    grep_project '\{\s*\}|return[[:space:]]+(nullptr|false|0|"")\s*;[[:space:]]*//[[:space:]]*(TODO|FIXME)'

} > "${REPORT_DIR}/dead_code_patterns.txt"

log "Dead-code pattern audit complete."

# ------------------------------------------------------------
# String / buffer overflow audit
# ------------------------------------------------------------

section "String and buffer safety audit"

{
    echo "=== HIGH-RISK C STRING / FORMAT APIs ==="
    grep_project '\b(strcpy|strcat|sprintf|vsprintf|gets|scanf|sscanf|fscanf|strncpy|strncat)\s*\('

    echo
    echo "=== RAW MEMORY COPY / MOVE / SET ==="
    grep_project '\b(memcpy|memmove|memset|bcopy)\s*\('

    echo
    echo "=== FIXED-SIZE CHARACTER BUFFERS ==="
    grep_project '\b(char|unsigned[[:space:]]+char|std::byte)[[:space:]]+[A-Za-z_][A-Za-z0-9_]*[[:space:]]*\[[^]]+\]'

    echo
    echo "=== C STRING EXTRACTION / POINTER LIFETIME HOTSPOTS ==="
    grep_project '\.(c_str|data)\(\)|std::string_view|const[[:space:]]+char[[:space:]]*\*'

    echo
    echo "=== FORMAT / SIZE CONVERSION HOTSPOTS ==="
    grep_project '\b(snprintf|vsnprintf)\s*\(|static_cast<[^>]*(int|unsigned|size_t)[^>]*>|reinterpret_cast<'

    echo
    echo "=== MANUAL STRING SIZE / INDEX ARITHMETIC ==="
    grep_project '\.(size|length)\(\)[[:space:]]*[+\-*]|[+\-*][[:space:]]*\.(size|length)\(\)|\[[^]]*(size|length)\(\)[^]]*\]'

} > "${REPORT_DIR}/string_buffer_safety.txt"

log "String/buffer safety audit complete."

# ------------------------------------------------------------
# Memory allocation / ownership / lifetime audit
# ------------------------------------------------------------

section "Memory allocation and lifetime audit"

{
    echo "=== RAW ALLOCATION / DEALLOCATION ==="
    grep_project '\b(new|delete|delete\[\]|malloc|calloc|realloc|free|aligned_alloc)\b'

    echo
    echo "=== ARRAY NEW / DELETE HOTSPOTS ==="
    grep_project 'new[[:space:]]+[^;]+\[[^]]+\]|delete[[:space:]]*\[\]'

    echo
    echo "=== SMART POINTER CREATION / OWNERSHIP ==="
    grep_project 'std::(unique_ptr|shared_ptr|weak_ptr)|std::make_(unique|shared)'

    echo
    echo "=== POSSIBLE ADDRESS / REFERENCE LIFETIME HOTSPOTS ==="
    grep_project 'return[[:space:]]+&|return[[:space:]]+std::(string_view|span)|std::span<|std::reference_wrapper'

    echo
    echo "=== REALLOCATION / POINTER INVALIDATION HOTSPOTS ==="
    grep_project '\.(push_back|emplace_back|insert|reserve|resize)\s*\(|\brealloc\s*\('

    echo
    echo "=== POINTER CASTS ==="
    grep_project '\b(reinterpret_cast|const_cast|dynamic_cast|static_cast)[[:space:]]*<[^>]*\*>'

} > "${REPORT_DIR}/memory_lifetime_patterns.txt"

log "Memory/lifetime audit complete."

# ------------------------------------------------------------
# Concurrency / runaway CPU / parser robustness audit
# ------------------------------------------------------------

section "Concurrency and runaway CPU audit"

{
    echo "=== THREADING / ASYNC / LOCKING ==="
    grep_project 'std::thread|std::jthread|std::async|hardware_concurrency|mutex|lock_guard|unique_lock|shared_mutex|atomic<'

    echo
    echo "=== BUSY / UNBOUNDED LOOPS ==="
    grep_project 'while[[:space:]]*\([[:space:]]*(true|1)[[:space:]]*\)|for[[:space:]]*\([[:space:]]*;[[:space:]]*;[[:space:]]*\)'

    echo
    echo "=== POSSIBLE RETRY LOOPS ==="
    grep_project '\b(retry|retries|attempt|attempts|backtrack|backtracking|while|for)\b'

    echo
    echo "=== RECURSION / PARSER LIMIT HOTSPOTS ==="
    grep_project '\b(recursion|recursive|depth|iteration|iterations|candidateAttempts|backtrackingAttempts|limit|budget)\b'

} > "${REPORT_DIR}/cpu_concurrency_patterns.txt"

log "CPU/concurrency audit complete."

# ------------------------------------------------------------
# Optimization / allocation / copy audit
# ------------------------------------------------------------

section "Optimization opportunity audit"

{
    echo "=== STRING COPYING / CONSTRUCTION HOTSPOTS ==="
    grep_project 'std::string[[:space:]]*\(|std::string[[:space:]]+[A-Za-z_]|substr\s*\(|to_string\s*\('

    echo
    echo "=== CONTAINER COPY / GROWTH HOTSPOTS ==="
    grep_project '\.(push_back|emplace_back|insert|reserve|resize|shrink_to_fit)\s*\('

    echo
    echo "=== SHARED_PTR HOTSPOTS ==="
    grep_project 'std::shared_ptr|std::make_shared'

    echo
    echo "=== MAP / UNORDERED_MAP HOTSPOTS ==="
    grep_project 'std::(unordered_map|map|unordered_set|set)<'

    echo
    echo "=== REGEX / STREAM HOTSPOTS ==="
    grep_project 'std::regex|std::stringstream|std::istringstream|std::ostringstream'

    echo
    echo "=== EXCEPTION HOTSPOTS ==="
    grep_project '\bthrow\b|\bcatch[[:space:]]*\('

} > "${REPORT_DIR}/optimization_patterns.txt"

log "Optimization audit complete."

# ------------------------------------------------------------
# Tests
# ------------------------------------------------------------

if [ -d "${BUILD_DIR}" ] && command_exists ctest; then
    section "CTest"

    (
        cd "${BUILD_DIR}" || exit 1

        nice -n "${NICE_LEVEL}" ctest \
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
# ASan + UBSan + LeakSanitizer
# ------------------------------------------------------------

if [ "${RUN_SANITIZERS}" -eq 1 ] && command_exists cmake; then
    section "ASan + UBSan + LeakSanitizer"

    cmake_configure_args "${ASAN_BUILD_DIR}" "Debug"

    SAN_FLAGS="-fsanitize=address,undefined,leak -fno-omit-frame-pointer -g3 -O1"

    CMAKE_ARGS+=(
        "-DCMAKE_C_FLAGS=${SAN_FLAGS}"
        "-DCMAKE_CXX_FLAGS=${SAN_FLAGS}"
        "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined,leak"
    )

    nice -n "${NICE_LEVEL}" cmake \
        "${CMAKE_ARGS[@]}" \
        > "${REPORT_DIR}/asan_configure.txt" 2>&1

    if [ "$?" -eq 0 ]; then
        nice -n "${NICE_LEVEL}" cmake \
            --build "${ASAN_BUILD_DIR}" \
            --parallel "${JOBS}" \
            > "${REPORT_DIR}/asan_build.txt" 2>&1

        if [ "$?" -eq 0 ]; then
            (
                cd "${ASAN_BUILD_DIR}" || exit 1

                ASAN_OPTIONS="detect_leaks=1:halt_on_error=0:strict_string_checks=1:check_initialization_order=1" \
                UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=0" \
                LSAN_OPTIONS="report_objects=1" \
                nice -n "${NICE_LEVEL}" ctest \
                    --output-on-failure \
                    -j "${JOBS}"
            ) > "${REPORT_DIR}/asan_tests.txt" 2>&1

            log "Sanitizer tests completed."
        else
            log "Sanitizer build failed."
        fi
    else
        log "Sanitizer configure failed."
    fi
fi

# ------------------------------------------------------------
# Valgrind memcheck
# ------------------------------------------------------------

if [ "${RUN_VALGRIND}" -eq 1 ]; then
    if command_exists valgrind; then
        section "Valgrind Memcheck"

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
                    --read-var-info=yes \
                    --expensive-definedness-checks=yes \
                    --errors-for-leak-kinds=definite,indirect,possible \
                    --error-exitcode=42 \
                    "${FELIDAE_BINARY}" \
                    "${SAMPLE_FILE}" \
                    > "${REPORT_DIR}/valgrind_stdout.txt" \
                    2> "${REPORT_DIR}/valgrind.txt"

                VALGRIND_RESULT=$?
                log "Valgrind exit: ${VALGRIND_RESULT}"
            else
                log "No safe example .fx file found; Valgrind skipped."
            fi
        else
            log "Felidae executable not found in quality build."
        fi
    else
        log "Valgrind missing."
    fi
fi

# ------------------------------------------------------------
# Binary / dead-section information
# ------------------------------------------------------------

section "Binary symbol and size information"

{
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
        echo "Binary: ${FELIDAE_BINARY}"

        if command_exists size; then
            echo
            echo "=== SIZE ==="
            size "${FELIDAE_BINARY}" || true
        fi

        if command_exists nm; then
            echo
            echo "=== LARGE DEFINED SYMBOLS ==="
            nm -S --size-sort --radix=d "${FELIDAE_BINARY}" 2>/dev/null \
                | tail -100 || true
        fi
    else
        echo "Felidae binary unavailable."
    fi
} > "${REPORT_DIR}/binary_symbols.txt"

# ------------------------------------------------------------
# Code statistics, excluding Celidae
# ------------------------------------------------------------

section "Code statistics"

{
    echo "C++ files: ${#CPP_FILES[@]}"
    echo "Header files: ${#HEADER_FILES[@]}"

    echo
    echo "Lines of analyzed source (Celidae excluded):"

    if [ "${#ALL_SOURCE_FILES[@]}" -gt 0 ]; then
        printf '%s\0' "${ALL_SOURCE_FILES[@]}" \
            | xargs -0 wc -l 2>/dev/null \
            | tail -1 || true
    else
        echo "0 total"
    fi

} > "${REPORT_DIR}/code_statistics.txt"

# ------------------------------------------------------------
# Codex-friendly consolidated report
# ------------------------------------------------------------

{
    echo "============================================================"
    echo "FELIDAE C++ QUALITY REPORT"
    echo "Generated: $(date)"
    echo "Celidae: excluded"
    echo "============================================================"

    echo
    echo "PRIORITY FOR REVIEW:"
    echo "1. Build/compiler correctness warnings"
    echo "2. ASan/UBSan/LSan and Valgrind findings"
    echo "3. clang-tidy / clang-analyzer correctness findings"
    echo "4. String/buffer overflow and lifetime hazards"
    echo "5. Allocation/ownership mistakes"
    echo "6. Dead/unreachable/unused code"
    echo "7. Parser termination / runaway CPU risks"
    echo "8. Measured or structurally clear optimization opportunities"
    echo
    echo "Do not treat grep pattern matches as confirmed bugs."
    echo "Inspect the referenced source before changing behavior."

    for report in \
        environment.txt \
        cmake_configure.txt \
        build.txt \
        cppcheck.txt \
        clang_tidy.txt \
        scan_build.txt \
        clang_format.txt \
        dead_code_patterns.txt \
        string_buffer_safety.txt \
        memory_lifetime_patterns.txt \
        cpu_concurrency_patterns.txt \
        optimization_patterns.txt \
        ctest.txt \
        asan_tests.txt \
        valgrind.txt \
        binary_symbols.txt \
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
echo "Suggested Codex instruction:"
echo
echo "  Review quality_reports/CODEX_REVIEW.txt and the referenced Felidae"
echo "  sources. Exclude src/celidae completely. Confirm each finding before"
echo "  editing. Fix correctness, UB, lifetime/allocation, string/buffer safety,"
echo "  dead code, parser termination and concurrency issues first. Then optimize"
echo "  only clear or measured bottlenecks. Preserve Felidae syntax, AST,"
echo "  interpreter semantics, SentencePiece integer parsing architecture and tests."
echo

exit 0
