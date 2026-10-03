# Felidae

Felidae is a deterministic functional-logic query and intelligence language.
`.fx` source is tokenized, parsed to an AST, and evaluated directly; durable
facts and graph relationships are stored in RocksDB.

```text
source.fx -> tokenizer -> IntegerParser -> Program AST -> Interpreter -> RocksDB
```

## Build and run

The default build uses the pinned `third_party/rocksdb` Git submodule. Clone
with `--recurse-submodules`, or run
`git submodule update --init --recursive` before configuring CMake. An
installed RocksDB 11.8.1 package can instead be selected with
`-DFELIDAE_USE_SYSTEM_ROCKSDB=ON`.

The build wrappers isolate each requested architecture beneath its named
configuration directory:

```powershell
.\build.cmd debug --platform x64 --jobs 4
.\build.cmd release --platform arm64
# Windows executable: build\debug\x64\Debug\felidae.exe
```

```sh
./build.sh debug --platform native --jobs 4
./build.sh release --platform arm64
# Unix executable on an x64 host: build/debug/x64/felidae
```

Windows accepts `x64`, `x86`, `arm`, and `arm64`. macOS accepts `x64` and
`arm64`. Linux accepts its native architecture; a different target requires
`CMAKE_TOOLCHAIN_FILE` to identify a real cross-compilation toolchain.
Running the same command again is incremental: CMake keeps RocksDB and Felidae
object files under that configuration/platform directory and rebuilds only
sources whose inputs changed. Keep the same configuration, platform, and job
count to maximize reuse.

Direct CMake configuration remains available:

```sh
cmake -S . -B build/debug -DFELIDAE_BUILD_TESTS=ON
cmake --build build/debug --target felidae -j2
build/debug/felidae tests/direct_ast_smoke.fx
build/debug/felidae v2_examples/mixfix_nested_expression.fx
build/debug/felidae tests/direct_ast_smoke.fx --debug
build/debug/felidae --db build/example.db tests/rocks_graph_traversal.fx
```

Live breakpoints and stepping are enabled only by `--debug`; normal execution
leaves the goal hook unset.

## Interactive REPL

Run `felidae` without a source file to open the REPL. On Windows:

```powershell
.\build\debug\x64\Debug\felidae.exe
```

Expressions and `?` queries execute immediately. Starting a line with `def` or
`class` automatically enters multiline input; the declaration is installed
when its normal Felidae `end` is reached. Nested control-flow blocks are
tracked. A failed declaration is rolled back. Type `:help` for the full command
list. Passing a program file together with `--repl` is intentionally rejected;
a file always uses normal program execution. Interactive terminals use colored
prompts and status messages on Windows, Linux, and macOS; redirected output is
plain text, and setting `NO_COLOR` disables styling explicitly.

Use `:debug on` for real goal-hook traces and `:debug locals on` to include
live bindings. `:metrics` reports the last action's interpreter/solver/storage
counters together with RocksDB key, memory, cache, and SST statistics;
`:metrics on` adds a compact summary after each action. Slow interactive
operations show a delayed minimal spinner. These facilities are created only
for a no-file REPL session and add no metrics, animation, formatting, or debug
hook work to normal `.fx` file execution.

The interactive editor highlights Felidae keywords, literals, strings,
comments, operators, and class names using the production lexer. Backspace and
Delete remove one character, Left/Right/Home/End move the cursor, and Up/Down
navigate session history. The highlighter owns an isolated vocabulary so
partially typed or erased words cannot affect interpreter token identities.
Functions, classes, annotations, imports, `:=` globals, expressions, queries,
and dot-terminated facts/rules use the same parser and interpreter as `.fx`
files. The activity indicator is deliberately limited to one, two, or three
small `#` blocks and appears only for perceptibly slow interactive work.
Use `:clear` to clear and redraw the terminal without discarding definitions,
facts, history, debugger settings, or measurements from the current session.

## Facts and memory

RocksDB is the authoritative durable fact and graph store. Each fact is an
independently keyed node. Schemas, indexes, explicit `Link` edges, adjacency,
provenance, and temporal metadata use fixed internal keyspaces.
Interpreter memory is reserved for ASTs, immutable temporary values, local
bindings, debugger frames, cursors, and bounded result batches. Without
`--db`, Felidae creates an isolated temporary database for the execution.

## Tokenization

Felidae uses a deterministic, training-free byte vocabulary compiled into the
interpreter. The lexer handles syntax, reserved words, comments, strings,
punctuation, and numbers; identifier and mixfix-anchor bytes are encoded after
the fixed grammar-token range. No model file is loaded or generated at runtime.

See [code.md](code.md) for the execution architecture and
[docs_language.md](docs_language.md) for language semantics.
