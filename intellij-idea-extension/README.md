# Felidae IntelliJ IDEA Plugin

IntelliJ IDEA language support for Felidae `.fx` files, backed by the
Celidae analysis host for diagnostics and data graph inspection.

## Features

- Registers `.fx` as Felidae files
- Basic syntax highlighting for comments, strings, numbers, keywords, operators, and core library calls
- Diagnostics from `celidae --check-json`, including AST analyzer warnings from the C++ Celidae host
- Celidae visual analytics action using `celidae --inspect-graph`
- Brace matching for `()`, `{}`, and `[]`
- File type, action, and tool-window icons

The plugin intentionally does not implement Felidae semantic validation in Java.
It delegates file checks to Celidae so IntelliJ IDEA, VS Code, and the
runtime stay aligned.

Celidae also exposes `celidae --lsp` for JSON-RPC stdio clients. The IntelliJ
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
