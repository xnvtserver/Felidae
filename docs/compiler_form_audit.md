# Compiler and Form VM audit

Audit date: 2026-08-16. Measurements are Windows Release builds using the
existing `felidae_integer_parser_benchmark` target, five process-start runs.

## Current boundaries

```text
felidae_compiler.exe
  source.fx -> SentencePiece -> IntegerParser -> AST -> IrCodeGenerator -> .fir

felidae_vm.exe
  program.fir -> binary validation -> IR verifier -> Form RegisterVm
```

`felidae_vm.exe` links only `src/form/`; it has no parser, AST, SentencePiece,
or LibTorch dependency. The compiler owns SentencePiece and optional LibTorch
mixfix support.

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

- AST interpreter, reasoning, native-runtime, environment, and memory runtime
  sources were unreachable from active CMake targets.
- The obsolete WASM entry point and stale `FelidaeRuntime` wrapper were
  unreachable.
- The old `LegacyIrAdapter` was renamed to `IrCodeGenerator` because it is
  active AST-to-IR code generation, not compatibility runtime code.

## Remaining complexity worth addressing

1. `IntegerParser.cpp` is 1,649 lines. It owns SentencePiece-ID parsing,
   operator/mixfix routing, AST construction, and three AST-to-IR helper
   implementations. Move the AST-to-IR helpers into a dedicated lowering unit;
   parsing should not own code generation.
2. `IrCodeGenerator.cpp` and the parser both participate in lowering. Their
   responsibilities should become explicit: parser constructs AST only;
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

`clang-tidy` and `cppcheck` were not installed in the audit environment. The
current repeatable gates are CMake builds, CTest, the parser benchmark, and the
binary round-trip tests. Install static-analysis tools before treating their
absence as a clean report.
