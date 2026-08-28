# Compiler and Form VM audit

Original benchmark date: 2026-08-16. Measurements are Windows Release builds using the
existing `felidae_integer_parser_benchmark` target, five process-start runs.
Stabilization findings were revalidated on Linux on 2026-08-28.

## Current boundaries

```text
felidae_compiler.exe
  source.fx -> SentencePiece -> IntegerParser -> compiler HIR -> IrCodeGenerator -> .bin

felidae_vm.exe
  program.bin -> binary validation -> IR verifier -> Form RegisterVm
              -> optional LibTorch tensor/runtime-SSM backend
```

`felidae_vm.exe` has no parser or compiler-HIR dependency. It uses SentencePiece only
at the display/model boundary and, on supported desktop builds, links LibTorch
for deterministic tensor operations and the optional runtime SSM. Core Form
IR verification and execution remain independent of compiler lowering.

## Measurements

| Input | Minimum | Median | Maximum |
| --- | ---: | ---: | ---: |
| `ast_typed_methods.fx` | 3,909 us | 4,676 us | 5,690 us |
| `custom_operator_overload.fx` | 2,663 us | 3,927 us | 4,682 us |
| `benchmark_recursive_system_run.fx` | 2,585 us | 4,068 us | 6,260 us |

For `6 * 7`, the parser benchmark reported 4--14 us SentencePiece encoding,
42--75 us IR compilation, 1--2 us verification, and 4--6 us VM execution.
Process/model initialization dominated total time (886--1,794 us).

## Proven dead code removed

- The old executable-AST interpreter, reasoning, native-runtime, environment, and memory runtime
  sources were unreachable from active CMake targets.
- The obsolete WASM entry point and stale `FelidaeRuntime` wrapper were
  unreachable.
- The old `LegacyIrAdapter` was renamed to `IrCodeGenerator` because it is
  active HIR-to-IR code generation, not compatibility runtime code.

## Stabilized contracts

- Compiler and debugger source parsing share `CompilerFrontend` file
  normalization, entry resolution, SentencePiece parsing, and an explicit
  operator registry. The debugger no longer maintains a divergent parser path.
- Debugger JSON-RPC input uses `nlohmann_json`; malformed requests produce
  protocol errors instead of being interpreted by raw string searches.
- RegisterVm verification and execution share `irInstructionWidth()` as the
  single executable-layout contract.
- Fact hierarchy registration rejects duplicate parents and cycles. Assignable
  searches have deterministic creation-order results.
- Map and fact equality are key-based, independent of field insertion order.
  Fact similarity reuses the same keyed-field algorithm without non-owning
  stack-backed `shared_ptr` adapters.
- Fact retention rejects malformed fields and facts owned by another runtime.
  Variable lookup tests cover immutable globals/locals and frame isolation.

## Remaining complexity worth addressing

1. `IntegerParser.cpp` is about 2,650 lines. It owns SentencePiece-ID parsing,
   operator/mixfix routing, HIR construction, and three HIR-to-IR helper
   implementations. Move the HIR-to-IR helpers into a dedicated lowering unit;
   parsing should not own code generation.
2. `IrCodeGenerator.cpp` and the parser both participate in lowering. Their
   responsibilities should become explicit: parser constructs HIR only;
   codegen lowers expressions, globals, methods, and conditions.
3. `IrModule` is the compiler construction form, while `LinkedIrModule` is the
   flattened binary form. The names obscure that distinction. A later API
   cleanup should name them `UnlinkedIrModule` and `LinkedIrModule`.
4. Operator annotations and `OperatorRegistry` are parser dependencies for
   mixfix declarations. They are not dead, but should be isolated from normal
   expression parsing behind a small parser routing interface.
5. `Symbol.h` is required for stable IDs and binary symbol-spelling validation.
   `BuiltinRegistry` is required for qualified builtin and annotation IDs;
   tests cover `math:sin` and `system:print` recognition.

## Tooling status

The current Linux machine provides Clang tools, IWYU, `cppcheck`, Valgrind,
Flawfinder, Coccinelle, Doxygen, Graphviz, `pahole`, `bpftrace`, Heaptrack,
Linux perf, and KCachegrind. They are not part of ordinary compilation because
complete analysis is intentionally an explicit release gate. Run them against
the configured `build/debug` compile database and focused executables; a tool
being installed is not evidence that its checks passed.
