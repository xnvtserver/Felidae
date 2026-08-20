# Felidae

Felidae compiles `.fx` source into verified, integer-only `.bin` artifacts.
`.bin` uses the incompatible **FELBIN v8** container; legacy `FELIR`/`.fir`
artifacts are rejected and their `.fx` sources must be recompiled.
The compiler and VM are separate C++ executables:

```text
source.fx -> SentencePiece IDs -> IntegerParser -> AST compiler -> verified .bin
program.bin -> binary loader -> verifier -> Form register VM
```

The Form VM executes IR only. It does not link the parser, AST, Interpreter,
or legacy AST runtime. `.bin` stores integer opcodes, registers, symbol IDs,
SentencePiece text IDs, constants, source-map spans, and procedure metadata;
it never stores source syntax, pointers, or AST objects.

## Build on Windows

SentencePiece and LibTorch are C++ dependencies. No Python or Celidae runtime
is required.

```powershell
cmake -S . -B build
cmake --build build --config Debug --target felidae_compiler felidae_vm
```

Both executables are written directly to `build/`:

```text
build/felidae_compiler.exe
build/felidae_vm.exe
```

When LibTorch DLLs are not already on `PATH`, add them before running either
executable:

```powershell
$env:PATH = 'C:\libtorch\lib;' + $env:PATH
```

## Compile, inspect, execute

The compiler writes the binary beside its executable, so source folders stay
unchanged:

```powershell
build\felidae_compiler.exe v2_examples\form_core_concepts.fx
build\felidae_vm.exe build\form_core_concepts.bin
```

The VM verifies the binary again before execution. A malformed, truncated, or
unverified `.bin` fails before it can run.

For the long-lived Form daemon mode:

```powershell
build\felidae_vm.exe --serve build\form_core_concepts.bin
# stdin commands: run, facts [type-id], field <symbol-id>, history,
# proof <child-type-id> <ancestor-type-id>, load <other.bin>, modules, quit
```

## Working examples

These examples use the current compiler/VM pipeline and produce `.bin` files
in `build/`.

| Example | Covers |
| --- | --- |
| `v2_examples/form_core_concepts.fx` | globals, procedures, arrays/maps, facts, fields and comparisons |
| `v2_examples/degree_profiles.fx` | deterministic `similarity`, `membership`, and `Degree` thresholds |
| `v2_examples/fact_degree_loop.fx` | typed fact traversal and degree-carrying values |
| `v2_examples/mixfix_ir_roundtrip.fx` | annotation-declared mixfix resolved to normal IR calls |
| `v2_examples/mixfix_deep_ir_nesting.fx` | deeply nested prefix/infix mixfix forms; result is `9`, `36`, `756` |
| `v2_examples/mixfix_fuzzy_fact_report.fx` | mixfix composition with facts and fuzzy degrees |

For example:

```powershell
build\felidae_compiler.exe v2_examples\mixfix_deep_ir_nesting.fx
build\felidae_vm.exe build\mixfix_deep_ir_nesting.bin
# {#...: 9, #...: 36, #...: 756}
```

Fact keys and type names display as numeric symbol IDs by design. VM text is
stored as SentencePiece IDs and is decoded only for display; symbol spelling is
not serialized into `.bin`.

## Mixfix and recurrent models

Normal syntax and uniquely resolved annotated mixfix stay deterministic and
compile directly to the same register IR. The compiler-side `MixfixStateModel`
uses a C++ LibTorch GRU and accepts only a finite structural IR vocabulary.
Its output is verifier-gated; missing models, invalid output, confidence
failure, or malformed spans are compile errors and never fall back to AST
execution.

The VM has a different optional recurrent model for `SemanticEval`/future
`SSM_PROCESS` work. It is built from `src/form/RuntimeStateModel.cpp` and uses
typed result actions (facts, text, arrays, maps, values, or `Degree`), never
implicit truthiness. Use a versioned runtime artifact explicitly:

```powershell
build\felidae_vm.exe --model models\runtime_gru_benchmark build\form_core_concepts.bin
```

## Validation

Run the focused compiler, binary, VM, SentencePiece, and GRU checks:

```powershell
$env:PATH = 'C:\libtorch\lib;' + $env:PATH
ctest --test-dir build -R 'felidae_(sentencepiece_model|sentencepiece_pipeline|mixfix_state_model|form_binary)' --output-on-failure
```

Create a portable shipment only after a successful build:

```powershell
# Builds, runs focused tests, stages dist/, starts the staged executables,
# and compiles/runs the nested-mixfix smoke program through them.
cmake --build build --target felidae_beta
# equivalent wrapper:
.\build.ps1 -Beta
```

`dist/` contains the two executables, models, and required LibTorch DLLs; it
does not contain source or CMake build state. The staged package writes
`SHA256SUMS.txt`; verify it before uploading the beta.

## Beta release checklist

1. Start with the existing `build/` directory and a clean intended source
   revision. Do not create a parallel build directory.
2. Configure a Release LibTorch build, then run `cmake --build build --target
   felidae_beta`.
3. Confirm the gate reports the compiler and VM version
   `0.2.3-beta.1`, all focused tests pass, and `dist/` contains only the
   approved executables, models, runtime DLLs, `README.txt`, and
   `SHA256SUMS.txt`.
4. Copy `dist/` to a clean Windows machine, verify checksums, compile a `.fx`
   file, and run its generated `.bin` using only the copied folder.
5. Tag and publish the exact revision only after that clean-machine smoke
   succeeds. Do not publish files directly from `build/`.
