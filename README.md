# Felidae

Felidae is a functional logic language for `.fx` files where code and data share
the same shape. Facts act as a lightweight database, methods transform and query
those facts explicitly, and native modules keep heavy work outside the core
interpreter.

The repository contains two C++ products:

- `felidae.exe`: the lean execution runtime for running programs and queries.
- `celidae.exe`: the diagnostics, debugging, analytics, LSP, and visualization
  host used by editor integrations.

## Language Shape

Facts are stored data:

```felidae
Person(name: "Alice", role: "Engineer")
Person(name: "Bob", role: "Manager")
```

Methods use explicit named inputs and immutable bindings:

```felidae
main(arguments: system.stdin) =>
    engineers := lambda(Person, p => p.role == "Engineer")
    return (
        count: count(engineers),
        args: arguments.args
    )
```

Statements end at a newline, not a trailing `.` — `.` is reserved for decimals
and same-line member access (`a.b.c`). Commas between goals in a body are
still accepted but are optional; a bare newline separates goals just as well.
See [docs_language.md](docs_language.md) for the full grammar, and
[v2_examples](v2_examples) for reference programs. `examples/` still holds
programs written for the older dot-terminated grammar.

Felidae does not implicitly scan all facts from a method call. Use
`lambda(FactType, item => expression)` or an explicit list/array when iteration
is required. This keeps dataflow visible and makes performance easier to reason
about.

## Runtime And Analysis

Run a program:

```powershell
build\felidae.exe examples\main.fx
```

Run a query:

```powershell
build\felidae.exe examples\main.fx "? Employee(name: name)"
```

Check a file with structured diagnostics:

```powershell
build\celidae.exe examples\main.fx --check-json
```

Start the Celidae JSON-RPC language server:

```powershell
build\celidae.exe --lsp
```

Create a visualization snapshot:

```powershell
build\celidae.exe examples\main.fx --visualize-data-json --load-imports
build\celidae.exe examples\main.fx --visualize-data-html --load-imports
```

`build\felidae_debug.exe` is still built as a legacy compatibility binary for
older editor installs.

## Build

Use the platform build wrapper when possible:

```bash
./build.sh
```

`build.sh` detects Linux and macOS hosts, asks before using `sudo` to install
missing build dependencies, and builds `build/felidae`, `build/celidae`, and
`build/felidae_debug`. Supported dependency installers include apt
(Debian/Ubuntu), dnf (Fedora), zypper (openSUSE), pacman (Arch), and Apple
Command Line Tools/Homebrew on macOS.

On Windows:

```powershell
.\build.cmd
.\build.cmd native --configuration production
.\build.cmd --target windows-x64 --configuration release
.\build.cmd --target windows-arm64 --configuration release
```

Build configurations are `debug`, `release`, `production`, and `sanitize`.
Production builds use `-O3`, `NDEBUG`, LTO, and `lld`. Build targets include
`native`, `windows-x64`, `windows-arm64`, `linux-x64`, `linux-arm64`,
`macos-x64`, `macos-arm64`, `android`, and `wasm`. Cross-targets require the
matching platform SDK/toolchain; for Ubuntu LTS package verification, run the
Linux target inside the intended Ubuntu 22.04 or 24.04 build container/runner.

Android is available as an NDK cross-build target when the NDK is installed:

```bash
ANDROID_NDK_HOME=/opt/android-ndk ANDROID_ABI=arm64-v8a ./build.sh --target android
```

Build the browser playground runtime with Emscripten when docs code blocks need in-browser execution:

On Windows:

```powershell
.\build.cmd wasm
```

On Linux/macOS:

```bash
./build.sh --target wasm
```

Without host Emscripten, use Docker:

```bash
./build/felidae wasm.fx
```

`wasm.fx` calls `emcc.fx`, which uses the common `docker.fx` helpers with `emscripten/emsdk:latest`, mounts the repository, and runs the same `./build.sh --target wasm` path inside the container.

That emits `docs/wasm/felidae_wasm.js`, `docs/wasm/felidae_wasm.wasm`, and `docs/wasm/felidae_wasm.data`, with `core/*.fx` packaged for language-native imports.

Linux packages can be created after a successful native Linux build:

```bash
scripts/package-linux.sh
```

The package helper always creates a tarball and creates `.deb`, `.rpm`, or
`.pkg.tar.zst` packages when local distro packaging tools are available.

CMake targets are also provided for environments that prefer generated build
files:

```bash
cmake -S . -B build
cmake --build build
```

## Test

Run the reusable Felidae test suite:

```powershell
build\felidae.exe examples\felidae_test_suite.fx
```

Useful focused checks:

```powershell
build\celidae.exe examples\diagnostics_ast_warnings.fx --check-json
build\celidae.exe examples\invalid\undeclared_body_var.fx --check-json
build\celidae.exe examples\main.fx --visualize-data-json --load-imports
```

Run the production quality gate:

```powershell
.\scripts\run_quality.ps1 -Configuration production -RunFullExamples
```

On Linux or WSL, the same quality gate can also run Valgrind when installed:

```bash
./scripts/run_quality.sh --configuration production --full-examples
```

The quality scripts run the stricter compiler build, `clang-tidy`, `cppcheck`
when installed, CodeChecker when installed, Valgrind on Linux when installed,
and Felidae `.fx` smoke or regression programs. Reports are written to
`build/quality/report.md`.

## Native Modules

Native modules are declared in `core/*.fx` and implemented in C++ modules under
`native_modules/`. The interpreter validates typed arguments before calling a
native function, so a bad user call fails at the Felidae layer rather than
crashing a shared library.

See [docs_native_modules.md](docs_native_modules.md) for the native ABI.

## Editor Support

### VS Code

The VS Code extension is in [vs-code-extension](vs-code-extension). It provides
syntax highlighting, file icons, snippets, import navigation, hovers, CodeLens
actions, debugger integration, Celidae-backed Problems diagnostics, and a data
visualizer with SVG/HTML export.

Development commands:

```powershell
cd vs-code-extension
npm install
npm run compile
npm run lint
npx vsce package
```

Install the generated VSIX:

```powershell
code --install-extension felidae-vscode-0.0.2.vsix
```

### IntelliJ IDEA

The IntelliJ plugin is in [intellij-idea-extension](intellij-idea-extension). It
registers `.fx` files, highlights Felidae syntax, delegates diagnostics to
Celidae `--check-json`, and adds run/check/visualize actions.

Development commands:

```powershell
cd intellij-idea-extension
.\gradlew.bat runIde
.\gradlew.bat buildPlugin
```

If Gradle is not cached locally, the first build needs network access to fetch
the Gradle distribution and IntelliJ Platform dependencies.

## Documentation

- [docs_language.md](docs_language.md): language notes and runtime behavior.
- [docs_native_modules.md](docs_native_modules.md): native module loading and ABI.
- [docs_github_linguist.md](docs_github_linguist.md): GitHub language detection notes.
- [tree-sitter-felidae](tree-sitter-felidae): starter grammar for future editor and GitHub navigation work.

## Repository Notes

Felidae source files use `.fx`. `.gitattributes` marks them as Felidae, but
GitHub will show Felidae as a first-class language only after GitHub Linguist
adds upstream support for the language.

