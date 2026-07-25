# Felidae Native Modules

Felidae keeps the interpreter small by treating native libraries as runtime modules.

## Architecture Boundary

Package names are not compiled into the interpreter. `core/<package>.fx`
defines the public Felidae API and calls `system_library_loader`; typed ABI
declarations live in `core/system/flibrary/<package>.fx`. The generic runtime
performs library loading, C ABI invocation, JSON marshalling, type validation,
and error propagation.

The interpreter retains language/runtime intrinsics only: syntax, facts and
indexes, unification/backtracking, control flow, imports, method invocation,
threads over immutable snapshots, primitive Felidae value representation, and
the native bridge. Optional package computation belongs in independently
built native modules.

`core/smoke.fx` and `native_modules/smoke/NativeSmoke.cpp` form a minimal ABI
extension example; the CSV package demonstrates the full public-wrapper plus
`core/system/flibrary` declaration layout. An equivalent package and
compatible library can be added without rebuilding `felidae`.

## Import Resolution

For an import such as:

```felidae
import "csv".
```

the interpreter checks in this order:

1. `core/csv.fx`
2. A platform native library for `csv`

The `.fx` API is registered lazily. Its native library is not opened during
import; resolution and loading happen only when a package method executes.
Missing libraries therefore fail at method execution and never fall back to
an interpreter implementation.

Native library names are platform-specific:

- Windows: `csv.dll`, `felidae_csv.dll`
- Linux: `libcsv.so`, `libfelidae_csv.so`, `csv.so`
- macOS: `libcsv.dylib`, `libfelidae_csv.dylib`, `csv.dylib`

The resolver searches near the importing file and under workspace module folders such as:

- `native_modules/<module>/`
- `modules/<module>/`

WASM cannot load host shared libraries. Package calls report
`native packages are unsupported in the Felidae WASM runtime`; host package
implementations are not embedded.

If neither a `.fx` declaration file nor a native library exists, the interpreter fails during load/check with `Module '<name>' not found`.

Native libraries are opened lazily when the imported module is first required. Handles are closed when the interpreter instance is destroyed, so parallel interpreter processes do not keep unnecessary native modules resident after shutdown.

## Declaration Files

`core/<module>.fx` contains the public Felidae callable surface:

```felidae
file.readFile(path: string) => ().
csv.parse(data: string, access: array) => ().
```

These declarations are intentionally bodyless. They describe names and argument types; heavy work belongs in a native shared library.

Before Felidae calls a native library it validates every concrete argument
against the declaration. For example, if the declaration says `value: string`
and the caller passes `value: 42`, execution stops in the interpreter with a
type error. The DLL/shared object is not called.

## Native ABI

Every native module must export this C ABI:

```cpp
extern "C" char* felidae_native_call(const char* function_name,
                                     const char* args_json);
extern "C" void felidae_native_free(char* ptr);
```

`args_json` is a JSON object containing evaluated input arguments. The response
must be JSON. A response object can bind output fields such as `access`, `out`,
`result`, or `value`; an object with an `error` string is reported as a native
failure.

The native module owns the returned buffer until Felidae calls
`felidae_native_free`.

## Runtime Direction

New non-core modules should target the shared-library ABI so the interpreter can
load and unload them independently from the main binary.

Celidae uses the same resolver through:

```powershell
build\celidae.exe file.fx --check-json
```

That means editor validation also reports missing source/native modules before
execution. Long-lived editor clients can use `build\celidae.exe --lsp` for the
same diagnostics over JSON-RPC stdio.
