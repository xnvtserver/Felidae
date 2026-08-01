# Felidae IntelliJ IDEA Plugin

IntelliJ IDEA language support for Felidae `.fx` files, backed by the
felidae_debug AST analysis for diagnostics and Celidae for fact graph inspection.

## Features

- Registers `.fx` as Felidae files
- Basic syntax highlighting for comments, strings, numbers, keywords, operators, and core library calls
- Diagnostics from `felidae_debug --check-json`, including C++ AST analyzer warnings
- Celidae visual analytics action using `celidae --inspect-graph`
- Brace matching for `()`, `{}`, and `[]`
- File type, action, and tool-window icons
- Live templates for `main`, facts, methods, fallback rules, lambdas, `throw`, `Fact.*` queries, and `probability.*` calls
- Quick Documentation (Ctrl+Q) for stdlib calls, backed by the same content the VS Code extension shows
- Completion for stdlib module calls and in-scope facts/methods/globals/bindings
- A gutter icon above `main(...)` to run (click) or check/visualize (right-click)
- Go to Declaration (Ctrl+B / Ctrl+Click) for facts, methods, and stdlib calls
- A "Run Felidae Query..." action, mirroring the VS Code extension's Run Query command
- A Settings > Tools > Felidae page for the felidae/felidae_debug/celidae executable paths

The plugin intentionally does not implement Felidae semantic validation in Java.
It delegates file checks to felidae_debug so IntelliJ IDEA, VS Code, and the
runtime stay aligned. Celidae is a separate tool dedicated to fact-relationship
visualization (ER diagrams, graphs, tree diagrams, statistical views) and has
no diagnostics support of its own.

felidae_debug also exposes `felidae_debug --lsp` for JSON-RPC stdio clients. The IntelliJ
plugin uses direct `--check-json` diagnostics today so it stays lightweight and
does not duplicate language semantics in Java.

## Debug Host

By default the plugin looks for:

```text
<project-root>/build/celidae.exe
<project-root>/build/celidae
<project-root>/build/felidae_debug.exe
<project-root>/build/felidae_debug
```

You can override this with:

```powershell
$env:CELIDAE_PATH="C:\path\to\celidae.exe"
```

The old `FELIDAE_DEBUG_PATH` and `build/felidae_debug.exe` names are still
accepted as migration fallbacks.

Alternatively, set explicit paths under **Settings | Tools | Felidae** — those
take priority over both environment variables and auto-detection.

## Run

```powershell
.\gradlew.bat runIde
```

Use **Tools | Check Felidae File** for diagnostics and
**Tools | Visualize Felidae Data Graph** for the runtime graph snapshot.

## Build

```powershell
.\gradlew.bat buildPlugin
```

The packaged plugin is written under:

```text
build/distributions/
```

Install it from IntelliJ IDEA with **Settings | Plugins | Install Plugin from
Disk...**. Development builds may also create jars under `build/libs/`, but the
Gradle distribution artifact is the expected install package.
