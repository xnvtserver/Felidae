# Felidae

Felidae is a functional logic language for `.fx` files where code and data share
the same shape. Facts act as a lightweight database, methods transform and query
those facts explicitly, and native services stay outside the compiler and Form VM.

The repository contains two isolated C++ products:

- `felidae_compiler.exe`: `SentencePiece -> IntegerParser -> AST compiler -> verified .fir`.
- `felidae_vm.exe`: `.fir -> verifier -> Form register VM`.
- `felidae_debug.exe`: `SentencePiece -> IntegerParser -> AST Analyzer`, for diagnostics, AST
  checks, and editor LSP integration.

The compiler does not execute source directly, and the Form VM does not depend
on parser, AST, or source syntax. The debugger is a separate diagnostic target.

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

Compile a program, then execute its binary artifact:

```powershell
build\felidae_compiler.exe examples\main.fx
build\felidae_vm.exe build\main.fir
```

Check a file with structured diagnostics:

```powershell
build\felidae_debug.exe examples\main.fx --check-json
```

Start the Felidae debugger JSON-RPC language server:

```powershell
build\felidae_debug.exe --lsp
```

Create a visualization snapshot:

```powershell
build\celidae.exe examples\main.fx --load-imports > snapshot.html
```

Celidae offers nine views. Four are structural — `schema`, `graph`, `er`,
`hierarchy` — describing what the program declares. The other five analyse the
literal values facts carry: `timeline` (records in date order, with per-period
volume and a spike test), `stats` (coverage and data-quality findings),
`distribution` (per-field histograms and robust outlier detection),
`comparison` (correlation between fields), and `cluster` (records projected onto
their principal components and grouped by k-means, with the number of segments
chosen by silhouette rather than fixed).

Running `celidae program.fx` bundles every view into one self-contained HTML
file; `--template=<name>` narrows that to just one. This is the only output
Celidae produces - there is no separate JSON or SVG export mode - because the
charts that carry the analysis here (treemaps, heatmaps, parallel coordinates,
decision trees, per-panel chart-type switching) are meant to be read by
hovering, filtering and switching how they are drawn, and a static copy of
them was a worse version of a better thing. To find out which views a
program's data actually supports, and why:

```powershell
build\celidae.exe examples\main.fx --recommend
```

Celidae's own acceptance tests — payload integrity, analysis correctness against
a fixture with known answers, XML/JSON escaping, determinism, and a sweep of
every example program across every view:

```powershell
.\scripts\test_celidae.ps1          # add -Quick to skip the corpus sweep
```

Target-specific C++ files live under `src/celidae` and `src/debugger`, split so
the two halves fail differently: `Analytics.cpp` holds the measurements (a bug
there gives a wrong number) and `Reasoning.cpp` holds the explanations (a bug
there gives a right number with a wrong story). Both run on Eigen, vendored
header-only in `third_party/Eigen`:

| Decomposition | Step |
| --- | --- |
| `SelfAdjointEigenSolver` | PCA projection for the segments view |
| `JacobiSVD` | effective rank, condition number, collinearity |
| `LDLT` | Mahalanobis multivariate outliers |
| `CompleteOrthogonalDecomposition` | rank-revealing least squares for "what moves this number" |
| `LDLT` | Fisher discriminant directions for oblique decision-tree splits |
| `SelfAdjointEigenSolver` | Fiedler-vector seriation, so related fields adjoin in a heatmap |
| `SelfAdjointEigenSolver` | Laplacian eigenmaps for the SVG network layout |
| `JacobiSVD` | correspondence analysis: two categorical fields in one space |
| `JacobiSVD` | latent semantic analysis over text values (TF-IDF) |

Nothing about any particular dataset is written down. Which charts a fact type
earns is decided by measuring its shape, and a view that cannot answer its own
question declines and says why rather than substituting an axis it does not
have — `celidae <file> --recommend` prints those verdicts.

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
build\felidae_debug.exe examples\diagnostics_ast_warnings.fx --check-json
build\felidae_debug.exe examples\invalid\undeclared_body_var.fx --check-json
build\celidae.exe examples\main.fx --load-imports > snapshot.html
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

### SonarQube

Run a local SonarQube server and C++ analysis entirely through Docker:

```powershell
.\scripts\run_sonar_scan.ps1 -StartOnly -Token placeholder
```

Open `http://localhost:9000`, complete the first-run setup, create a project
token, then run:

```powershell
.\scripts\run_sonar_scan.ps1 -Token "your-project-token"
```

The scanner creates a Linux CMake compilation database before analysis, so
SonarQube can inspect the interpreter and native modules rather than treating
the C++ sources as plain text. Stop the local service with:

```powershell
docker compose -f .\docker-compose.sonar.yml down
```

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
actions, debugger integration, felidae_debug-backed Problems diagnostics, and a data
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
felidae_debug `--check-json`, and adds run/check/visualize actions.

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

