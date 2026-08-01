# Felidae VS Code Extension

VS Code support for Felidae `.fx` files.

## Features

- Syntax highlighting for `.fx` source files
- Felidae file icon assets plus the optional `Felidae File Icons` icon theme
- `#` line comments, bracket pairs, auto-closing pairs, and region folding markers
- Folding for multi-line methods, facts, maps, arrays, and grouped statements
- Semantic highlighting for method parameters, immutable bindings, lambda items, and member-access bases
- Distinct library-prefix coloring for calls such as `array:get`, `json.get`, `math.pow`, and `system.print`
- Snippets for `main`, imports, facts, methods, lambdas, returns, core libraries, and named arguments
- Import links, builtin hover docs, and Go to Definition for facts, methods, and core libraries
- Completion for stdlib module calls, in-scope variables/globals, imported modules, and named fact/method arguments
- Outline view and breadcrumbs (Document Symbols) for facts, methods, and globals
- A quick fix for "Fact type is not implicitly iterable" diagnostics
- CodeLens actions beside `main(...)`: `Run | Debug | Visualize`
- Problems diagnostics reported by `felidae_debug --check-json`, debounced while typing
- Debug Console query execution while a Felidae debug session is active
- Simulated breakpoints, Step Over, Step In, and Step Out for source navigation
- Data visualizer using debugger graph snapshots, with SVG export

## File Icon Theme

The extension contributes `.fx` language icons and an optional file icon theme.

To show the Felidae icon in tabs and Explorer:

1. Run `Preferences: File Icon Theme`.
2. Select `Felidae File Icons`.
3. Reload the VS Code window if another extension still claims `.fx`.

The extension also sets `*.fx` to the `felidae` language by default. If VS Code opens a file as HLSL, use `Change Language Mode` and select `Felidae`.

## Runtime Validation

The extension does not run separate TypeScript language-error validation.
`felidae_debug` is the source of truth for Problems diagnostics, library/method
listing, and LSP; Celidae is a separate tool dedicated to fact-relationship
visualization (ER diagrams, graphs, tree diagrams, statistical views) and has
no diagnostics or `--check-json`/`--lsp` support of its own. The extension
calls:

```powershell
build\felidae_debug.exe path\to\file.fx --check-json
```

`felidae_debug` also provides `build\felidae_debug.exe --lsp` for JSON-RPC
stdio clients. The VS Code extension keeps direct `--check-json` diagnostics
to avoid adding a second TypeScript validator or an extra client dependency.

Runtime diagnostics run when:

- A `.fx` file is opened
- A `.fx` editor tab becomes active
- A `.fx` file changes (debounced ~350ms after the last keystroke)
- A `.fx` file is saved
- `Run`, `Debug`, or `Run Query` is started

If `felidae_debug.exe` is missing, the Problems panel shows a warning because
runtime validation is unavailable. Configure the path with:

```json
{
  "felidae.debugInterpreterPath": "build/felidae_debug.exe"
}
```

You can also set `FELIDAE_DEBUG_PATH` to an absolute `felidae_debug`
executable path.

Celidae's visualizer executable is configured separately:

```json
{
  "felidae.celidaePath": "build/celidae.exe"
}
```

or `CELIDAE_PATH` for an absolute path. See [Visualize](#visualize) below.

## Run

Use the CodeLens above `main(...)` or the command palette:

- `Felidae: Run`
- `Felidae: Run Query`

The normal run command uses:

```json
{
  "felidae.interpreterPath": "build/felidae.exe"
}
```

Before execution, the extension checks the file through `felidae_debug --check-json`.
Direct fact declarations such as `Employee(name: "Alice")` are valid. The
debugger reports an error only when a method body tries to use a fact type as an
implicit iterator, for example `Employee(e)`; use `lambda(Employee, e => ...)`
or an explicit array/list for iteration.

## Debug

Use the `Debug` CodeLens above `main(...)`, or create a launch configuration:

```json
{
  "type": "felidae",
  "request": "launch",
  "name": "Debug Felidae Main",
  "program": "${file}",
  "interpreterPath": "${workspaceFolder}/build/felidae_debug.exe",
  "stopOnEntry": true
}
```

For a query:

```json
{
  "type": "felidae",
  "request": "launch",
  "name": "Debug Felidae Query",
  "program": "${file}",
  "query": "? Engineer(name: name)",
  "interpreterPath": "${workspaceFolder}/build/felidae_debug.exe",
  "stopOnEntry": true
}
```

The debug adapter launches `felidae_debug.exe`. Runtime execution stays in C++; source stepping is simulated by the extension so normal execution remains lightweight.

Supported debug behavior:

- Continue resumes the Felidae AST debugger process.
- Breakpoints are verified on executable Felidae lines.
- Step Over moves to the next executable source line.
- Step In jumps to a matching method definition when the current line calls one.
- Step Out returns to the next executable caller line.
- Variables show a lightweight simulated view of visible parameters and immutable bindings.

## Debug Console Queries

While a Felidae debug session is active, type a query in the Debug Console:

```felidae
? Employee(name: name)
```

You may omit the leading `?`:

```felidae
Employee(name: name)
```

The extension runs the query through `felidae_debug --query` against the active program.

## Visualize

Use `Visualize` beside `main(...)` or run `Felidae: Visualize`.

The visualizer asks `celidae --visualize-data-json --load-imports` for a
viewer-ready runtime data snapshot. Use `celidae --inspect-graph` for a
lightweight source/file graph, and add `--load-imports` when a scenario needs
imported fact DBs. Celidae can also emit standalone HTML with
`--visualize-data-html --load-imports`. The extension does not leave temporary
JSON files behind. The view is a data-analysis workbench rather than only a
code graph:

- Graph view with Draw.io-style canvas grid, force/flow/circle layouts, node
  filters, search, selection details, and SVG export.
- Profile view with node/relationship metrics and quick charts for facts,
  fields, globals, methods, libraries, and edge labels.
- Quality view for noisy or faulty log-shaped data, including isolated nodes,
  duplicate labels, sparse runtime metadata, high fan-out hubs, and unlabeled
  relationships.
- Data Table view for searchable fact/method/library/global rows, degree
  counts, quality signals, and runtime detail.
- HTML export for sharing a visual analysis snapshot from the debugger.

## Build During Development

From `vs-code-extension`:

```powershell
npm install
npm run compile
npm run lint
```

From the project root, build both runtime binaries:

```powershell
clang++ -std=c++17 -Wall -Wextra -Isrc -Ithird_party src/main.cpp src/FelidaeRuntime.cpp src/Visualization.cpp src/Lexer.cpp src/Parser.cpp src/Interpreter.cpp src/Env.cpp src/Memory.cpp native_modules/csv/NativeCsv.cpp native_modules/http/NativeHttp.cpp native_modules/process/NativeProcess.cpp -o build/felidae.exe
clang++ -std=c++17 -Wall -Wextra -Isrc -Ithird_party src/felidae_debug.cpp src/FelidaeRuntime.cpp src/Visualization.cpp src/Lexer.cpp src/Parser.cpp src/Interpreter.cpp src/Env.cpp src/Memory.cpp src/AstAnalyzer.cpp native_modules/csv/NativeCsv.cpp native_modules/http/NativeHttp.cpp native_modules/process/NativeProcess.cpp -o build/celidae.exe
```

## Package And Install

Package:

```powershell
npx vsce package
```

Install the generated VSIX:

```powershell
code --install-extension felidae-vscode-0.0.2.vsix
```

Reload VS Code after installing so the `.fx` language, file icon, CodeLens, diagnostics, and debugger are all refreshed.
