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
Person(name: "Alice", role: "Engineer").
Person(name: "Bob", role: "Manager").
```

Methods use explicit named inputs and immutable bindings:

```felidae
main(arguments: system.stdin) =>
    engineers := lambda(Person, p => p.role == "Engineer"),
    return (
        count: count(engineers),
        args: arguments.args
    ).
```

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

On this Windows checkout, direct `clang++` builds are the most reliable path:

```powershell
clang++ -std=c++17 -Wall -Wextra -Isrc -Ithird_party src/main.cpp src/FelidaeRuntime.cpp src/Visualization.cpp src/Lexer.cpp src/Parser.cpp src/Interpreter.cpp src/Env.cpp src/Memory.cpp native_modules/csv/NativeCsv.cpp native_modules/http/NativeHttp.cpp native_modules/process/NativeProcess.cpp -o build/felidae.exe

clang++ -std=c++17 -Wall -Wextra -Isrc -Ithird_party src/felidae_debug.cpp src/FelidaeRuntime.cpp src/Visualization.cpp src/Lexer.cpp src/Parser.cpp src/Interpreter.cpp src/Env.cpp src/Memory.cpp src/AstAnalyzer.cpp native_modules/csv/NativeCsv.cpp native_modules/http/NativeHttp.cpp native_modules/process/NativeProcess.cpp -o build/celidae.exe
```

CMake targets are also provided:

```powershell
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
