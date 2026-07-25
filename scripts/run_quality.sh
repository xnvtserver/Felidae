#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

CONFIGURATION="debug"
SKIP_BUILD=0
RUN_FULL_EXAMPLES=0
STRICT=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --configuration) CONFIGURATION="${2:-debug}"; shift 2 ;;
        --skip-build) SKIP_BUILD=1; shift ;;
        --full-examples) RUN_FULL_EXAMPLES=1; shift ;;
        --strict) STRICT=1; shift ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
done

mkdir -p build/quality
REPORT="build/quality/report.md"
{
    echo "# Felidae Quality Report"
    echo
    echo "Generated: $(date -u +"%Y-%m-%dT%H:%M:%SZ")"
    echo "Configuration: $CONFIGURATION"
} > "$REPORT"

run_logged() {
    local title="$1"
    local allow_failure="${2:-0}"
    shift 2
    {
        echo
        echo "## $title"
        echo '```text'
    } >> "$REPORT"
    set +e
    "$@" >> "$REPORT" 2>&1
    local status=$?
    set -e
    echo '```' >> "$REPORT"
    if [[ "$status" -ne 0 && "$allow_failure" -eq 0 && "$STRICT" -eq 1 ]]; then
        exit "$status"
    fi
}

if [[ "$SKIP_BUILD" -eq 0 ]]; then
    run_logged "Build" 0 ./build.sh --target native --configuration "$CONFIGURATION"
fi

tidy_files=(
    src/Interpreter.cpp
    src/Env.cpp
    src/Memory.cpp
    src/NativeRuntime.cpp
    src/BuiltinRegistry.cpp
    src/FelidaeRuntime.cpp
)

if command -v clang-tidy >/dev/null 2>&1; then
    for file in "${tidy_files[@]}"; do
        run_logged "clang-tidy $file" 1 clang-tidy "$file" -- -std=c++17 -Isrc -isystem third_party
    done
else
    printf '\n## clang-tidy\nSkipped: clang-tidy was not found on PATH.\n' >> "$REPORT"
fi

if command -v cppcheck >/dev/null 2>&1; then
    run_logged "cppcheck" 1 cppcheck --enable=warning,performance,portability,style --std=c++17 --inline-suppr --suppress=missingIncludeSystem -Isrc -Ithird_party src native_modules
else
    printf '\n## cppcheck\nSkipped: cppcheck was not found on PATH. Install cppcheck and rerun this script.\n' >> "$REPORT"
fi

if command -v CodeChecker >/dev/null 2>&1; then
    run_logged "CodeChecker log" 1 CodeChecker log -b "./build.sh --target native --configuration $CONFIGURATION" -o build/quality/compile_commands.json
    run_logged "CodeChecker analyze" 1 CodeChecker analyze build/quality/compile_commands.json -o build/quality/codechecker
else
    printf '\n## CodeChecker\nSkipped: CodeChecker was not found on PATH. Install CodeChecker and rerun this script.\n' >> "$REPORT"
fi

if command -v valgrind >/dev/null 2>&1; then
    run_logged "Valgrind then_pipeline" 1 valgrind --leak-check=full --error-exitcode=99 ./build/felidae examples/then_pipeline.fx
    run_logged "Valgrind direct_main" 1 valgrind --leak-check=full --error-exitcode=99 ./build/felidae examples/direct_main.fx one two
else
    printf '\n## Valgrind\nSkipped: valgrind was not found on PATH. Install valgrind or run this script in a Linux image with Valgrind.\n' >> "$REPORT"
fi

if [[ "$RUN_FULL_EXAMPLES" -eq 1 ]]; then
    run_logged "Felidae example regression" 0 powershell -ExecutionPolicy Bypass -File scripts/test_felidae_examples.ps1
else
    run_logged "Felidae smoke .fx programs" 0 ./build/felidae examples/then_pipeline.fx
    run_logged "Native thread smoke" 0 ./build/felidae examples/native_thread_smoke.fx
    run_logged "Direct main smoke" 0 ./build/felidae examples/direct_main.fx one two
    run_logged "Docs AST debugger check" 0 ./build/felidae_debug docs/server.fx --check-json
fi

echo "Quality report written to $REPORT"
