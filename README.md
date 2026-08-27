# Felidae

Felidae compiles `.fx` source into verified, integer-only `.bin` artifacts.
`.bin` uses the beta **FELBIR v13** container. During beta, binaries must be
rebuilt whenever the current IR version changes; pre-release compatibility
formats are intentionally not retained.
The compiler and VM are separate C++ executables:

```text
source.fx -> SentencePiece IDs -> IntegerParser -> AST compiler -> verified .bin
program.bin -> binary loader -> verifier -> Form register VM
```

The compiler emits one executable, variable-width IR. It is verified once at
the compiler or hostile-binary boundary and executed directly by the Form
register VM; there is no assembler, secondary ISA, or lowering stage.
`.bin` stores numeric opcodes and operands, registers, constants, source-map
spans, procedure metadata, and a module symbol table whose entries are complete
SentencePiece ID sequences. It never stores source syntax, pointers, AST
objects, symbol hashes, or a parallel UTF-8 symbol-identity system.

The Form VM does not link the parser or AST. SentencePiece decoding is a
presentation adapter used for meaningful output, while execution and runtime
SSM context remain integer-only.

For intelligence-style evaluation, use the reproducible
[reasoning and robustness benchmark](docs/reasoning_benchmark.md). It includes
coffee-vending and HVAC examples using hierarchy, explicit unification
predicates, theorem-like mixfix statements, bounded alternative proofs, and
safe failure cases. The benchmark reports proof accuracy, fault tolerance,
determinism, latency, and held-out learning separately; it does not claim a
human IQ score or native Prolog backtracking.

## Build and debug on Linux or WSL

Felidae requires CMake 3.21 or newer, a C++20 compiler (GCC 8+ or Clang 7+),
and the pinned Git submodules. Ninja and GDB are recommended for fast builds
and native debugging. Python and LibTorch are not required for the normal
compiler and VM.

Clone the repository with its submodules, or initialize them in an existing
checkout:

```bash
git clone --recurse-submodules <repository-url>
# existing checkout:
git submodule update --init --recursive
```

Keep Debug, sanitizer, and Release configuration state in separate build
directories. The portable `build.sh` exposes its normal controls at the top:

```bash
MODE="test"          # test or release
ENABLE_TRAINING="OFF" # ON requires LibTorch
ENABLE_LIBTORCH="OFF"
JOBS="auto"           # detected logical CPUs, or a positive integer
```

Test mode configures `build/test`, builds the compiler, VM, and tests, then
runs CTest. Release mode configures `build/release`, builds optimized binaries,
and dynamically generates `build/release/dist/`; `dist/` is never a static
source folder.

```bash
./build.sh --mode test
./build.sh --mode test --jobs 8
./build.sh --mode test --libtorch ON --training ON
./build.sh --mode release --libtorch ON --training OFF
```

Normal builds use all detected logical CPUs. Use `--jobs N` or
`FELIDAE_JOBS=N` to cap parallelism when LibTorch compilation approaches the
machine's memory limit.

Linux x86-64 and ARM use native CMake when run on that architecture. macOS
supports Intel and Apple Silicon, and Android uses the official NDK toolchain:

```bash
./build.sh --platform macos --arch arm64 --mode release --libtorch OFF

ANDROID_NDK_HOME=/opt/android-ndk \
  ./build.sh --platform android --arch arm64 --android-api 24 \
  --mode release --libtorch OFF
```

For another OS or cross-compiler, use `--platform generic` and set
`FELIDAE_TOOLCHAIN_FILE`. Cross-platform
LibTorch builds must also set `FELIDAE_LIBTORCH_PATH` to a package built for
the target architecture. Android tests are compiled but not executed on the
host. Training is compiled into the compiler and VM only when both training
and LibTorch are enabled; it is never started by `build.sh`.

A conservative manual Debug build suitable for machines with limited memory is:

```bash
cmake -S . -B build/debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DFELIDAE_ENABLE_LIBTORCH=OFF \
  -DFELIDAE_BUILD_TESTS=ON

cmake --build build/debug \
  --target felidae_compiler felidae_vm felidae_debug felidae_tests \
  --parallel 1
```

Run the complete test suite before debugging or changing the compiler/VM:

```bash
ctest --test-dir build/debug --output-on-failure
```

Compile and execute a working example:

```bash
./build/debug/felidae_compiler v2_examples/form_core_concepts.fx
./build/debug/felidae_vm build/debug/form_core_concepts.bin
```

The compiler writes the generated `.bin` into its own executable directory,
not beside the source. The VM verifies the binary before executing it.
Test binaries and model fixtures use deterministic subdirectories below
`build/<configuration>/test-artifacts/` and `build/<configuration>/model-tests/`;
tests do not create random build directories in the system temporary folder.

Use GDB to debug either side of the compiler/VM boundary:

```bash
gdb --args ./build/debug/felidae_compiler v2_examples/form_core_concepts.fx
gdb --args ./build/debug/felidae_vm build/debug/form_core_concepts.bin
```

Debug builds also provide an opt-in action trace. `FELIDAE_TRACE=1` writes
parser and verified-IR dispatch details to `stderr`; normal program results
remain on `stdout`. The trace code is guarded by `#ifndef NDEBUG` and is absent
from Release builds:

```bash
FELIDAE_TRACE=1 ./build/debug/felidae_compiler v2_examples/form_core_concepts.fx
FELIDAE_TRACE=1 ./build/debug/felidae_vm build/debug/form_core_concepts.bin
```

Parser lines report source bytes, SentencePiece token count, the single encode
pass, statement completion, compiler-SSM spans, and verified compiler-IR
sizes. VM lines report the verified module version followed by call depth,
instruction PC, fixed opcode ID, and decoded instruction width.

The repository's `.vscode/launch.json` contains Linux GDB profiles. Build
first, then adjust their executable paths from `build/` to `build/debug/` when
using the separate directory above. The current default VS Code build task is
Windows-specific and should not be used unchanged on Linux or WSL.

`felidae_debug` is a non-executing source-analysis tool rather than the Form
register VM. It can emit human-readable or structured diagnostics:

```bash
./build/debug/felidae_debug v2_examples/form_core_concepts.fx --check
./build/debug/felidae_debug v2_examples/form_core_concepts.fx --check-json
```

For memory errors and undefined behavior, create a separate sanitizer build:

```bash
cmake -S . -B build/asan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DFELIDAE_ENABLE_SANITIZERS=ON \
  -DFELIDAE_ENABLE_LIBTORCH=OFF \
  -DFELIDAE_BUILD_TESTS=ON

cmake --build build/asan --target felidae_tests --parallel 1
ctest --test-dir build/asan --output-on-failure
```

Check `git status --short` before and after builds. Build products belong in
their build directories. Normal builds consume the checked-in SentencePiece
model and ID header without rewriting them. Model regeneration, training, and
beta packaging are explicit maintainer workflows and should not run during
routine setup. Increase `--parallel` only when the machine has enough memory.

### Fixed tokenizer corpus

The compiler's fixed SentencePiece model is generated in pure C++ from
[`datasets/tokenizer/felidae-tokenizer-v1.jsonl`](datasets/tokenizer/felidae-tokenizer-v1.jsonl).
The versioned corpus currently covers 32 representative programs across 19
syntax families, including facts, hierarchy, methods, control flow, operators,
queries, mixfix forms, semantic intrinsics, Unicode, strings, temporal fields,
and Prolog-style fact iteration. Each JSONL row has exactly
`schema_version`, `id`, `family`, and `source`; duplicate IDs, oversized rows,
unknown fields, or insufficient family coverage stop model generation.

Grammar spellings remain pinned SentencePiece user-defined symbols. The model
uses identity normalization, a bounded 1024-piece vocabulary, deterministic
single-threaded training, and byte fallback for unseen UTF-8. Generated token
IDs record the corpus schema, record count, and dataset hash. Tokenizer IDs
are the canonical lexical representation in executable IR symbol and text
tables; opcodes, registers, numeric values, indexes, and branch targets remain
ordinary numeric IR operands.

Before intentional regeneration, inspect the Git history and status of the
model, generated ID header, grammar, and tokenizer corpus. Then run:

```bash
cmake --build build/debug \
  --target felidae_regenerate_sentencepiece_model --parallel 8
```

Keep the generated changes only when an authoritative input changed and the
tokenizer and pipeline tests pass.


For your Debian + C++20 Felidae build, install the official **Linux LibTorch C++ package** rather than `pip install torch`. PyTorch’s LibTorch distribution includes the C++ headers, shared libraries, and CMake config files needed by `find_package(Torch)`. ([PyTorch Docs][1])

Start with the basic packages:

```bash
sudo apt update

sudo apt install -y \
  build-essential \
  cmake \
  ninja-build \
  wget \
  unzip \
  ca-certificates
```

For a CPU-only machine, download the official CPU LibTorch archive. PyTorch documents this CPU-only package directly: ([PyTorch Docs][1])

```bash
cd ~/Downloads

wget https://download.pytorch.org/libtorch/cpu/libtorch-shared-with-deps-2.13.0%2Bcpu.zip

unzip libtorch-cxx11-abi-shared-with-deps-latest.zip
```

Remember, **`cxx11-abi` does not mean C++11 language mode**. Your Felidae project can remain C++20; this refers to GCC/libstdc++ ABI compatibility. Current LibTorch C++11-ABI builds require GCC 9+ and glibc 2.29+, which modern Debian satisfies. ([PyTorch Docs][1])

I recommend installing it under `/opt`:

```bash
sudo mv libtorch /opt/libtorch
```

Verify:

```bash
ls /opt/libtorch/share/cmake/Torch/TorchConfig.cmake
```

and:

```bash
ls /opt/libtorch/lib/libtorch.so
ls /opt/libtorch/lib/libtorch_cpu.so
ls /opt/libtorch/lib/libc10.so
```

Then configure Felidae with LibTorch enabled:

```bash
cd /home/vishal/Felidae

cmake -S . -B build/debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DFELIDAE_ENABLE_LIBTORCH=ON \
  -DFELIDAE_BUILD_TESTS=ON \
  -DCMAKE_PREFIX_PATH=/opt/libtorch
```

Build:

```bash
cmake --build build/debug \
  --target felidae_compiler felidae_vm felidae_debug felidae_tests \
  --parallel 1
```

You can confirm CMake actually enabled it with:

```bash
grep FELIDAE_ENABLE_LIBTORCH build/debug/CMakeCache.txt
```

Expected:

```text
FELIDAE_ENABLE_LIBTORCH:BOOL=ON
```

Also check Torch discovery:

```bash
grep -E 'Torch_DIR|CMAKE_PREFIX_PATH' build/debug/CMakeCache.txt
```

You should see something similar to:

```text
Torch_DIR=/opt/libtorch/share/cmake/Torch
```

If the executable later complains that `libtorch.so` or `libc10.so` cannot be found, test with:

```bash
export LD_LIBRARY_PATH=/opt/libtorch/lib:$LD_LIBRARY_PATH
```

Then:

```bash
ldd build/debug/felidae_vm | grep -E 'torch|c10'
```

For a permanent development-machine setup, you can register LibTorch with the Linux dynamic linker:

```bash
echo "/opt/libtorch/lib" | sudo tee /etc/ld.so.conf.d/libtorch.conf
sudo ldconfig
```

Then verify:

```bash
ldconfig -p | grep -E 'libtorch|libc10'
```

That is preferable to setting `LD_LIBRARY_PATH` on every shell.

One thing I would **not** do is install a Debian `libtorch-dev` package for this project if you want the same controlled LibTorch version across Windows and Linux. Pinning an official LibTorch distribution under `/opt/libtorch` makes your Felidae CMake environment much more reproducible.

[1]: https://docs.pytorch.org/cppdocs/installing.html?utm_source=chatgpt.com "Installing C++ Distributions of PyTorch — PyTorch main documentation"


## Build on Windows

SentencePiece and LibTorch are C++ dependencies. No Python or deprecated
visualizer runtime is required.

Clone with the pinned third-party sources, or initialize them after cloning:

```powershell
git clone --recurse-submodules <repository-url>
# existing checkout:
git submodule update --init --recursive
```

`third_party/` is tracked exclusively through Git submodules, including
SentencePiece, nlohmann/json, Abseil, Protobuf, Eigen, cpp-httplib, and
rapidcsv.

```powershell
cmake -S . -B build/debug -A x64 -DFELIDAE_ENABLE_LIBTORCH=ON -DCMAKE_PREFIX_PATH=C:\libtorch
cmake --build build/debug --config Debug --target felidae_compiler felidae_vm
```

The default IDE/Debug build contains only the production compiler and VM plus
their required dependencies. Build the optional debugger or complete unit
suite explicitly when needed:

```powershell
cmake --build build/debug --config Debug --target felidae_debug
cmake --build build/debug --config Debug --target felidae_tests
ctest --test-dir build/debug -C Debug --output-on-failure
```

Both executables are written directly to `build/debug/`:

```text
build/debug/felidae_compiler.exe
build/debug/felidae_vm.exe
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
build\debug\felidae_compiler.exe v2_examples\form_core_concepts.fx
build\debug\felidae_vm.exe build\debug\form_core_concepts.bin
```

The binary loader parses and verifies untrusted input once before constructing
the immutable module accepted by the VM. A malformed, truncated, or unverified
`.bin` fails before it can run.

For the long-lived Form daemon mode:

```powershell
build\debug\felidae_vm.exe --serve build\debug\form_core_concepts.bin
# stdin commands: run, facts [type-id], field <symbol-id>, history,
# proof <child-type-id> <ancestor-type-id>, load <other.bin>, modules, quit
```

## Working examples

These examples use the current compiler/VM pipeline and produce `.bin` files
in `build/debug/`.

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
build\debug\felidae_compiler.exe v2_examples\mixfix_deep_ir_nesting.fx
build\debug\felidae_vm.exe build\debug\mixfix_deep_ir_nesting.bin
# {first: 9, second: 36, nested: 756}
```

Fact keys and type names are stored as module-local indexes backed by complete
SentencePiece ID sequences. VM text is also stored as PieceId sequences.
Execution compares those sequences without decoding them; display and other
human-readable boundaries decode them through the matching SentencePiece
model, producing meaningful names rather than numeric hashes.

Fact designations participate in typed hierarchical queries. For example,
`Animal(...) as animals` lets source use
`for_each_fact(animals, callback)`; the compiler emits `QueryFacts` against
the `Animal` type, and the VM includes `Animal` plus all registered descendant
types. `as` remains compiler metadata—it is not a fact field or inheritance
edge. See `v2_examples/hierarchical_designation_filter.fx`.

## Mixfix and recurrent models

Normal syntax and uniquely resolved annotated mixfix stay deterministic and
compile directly to the same internal compiler IR. The compiler-side `MixfixStateModel`
uses a C++ LibTorch GRU and accepts only a finite structural IR vocabulary.
Its output is verifier-gated; missing models, invalid output, or malformed
spans are compile errors and never fall back to AST execution. No trained
mixfix artifact is currently shipped. `REJECT` and `ABSTAIN` produce bounded,
contextual diagnostics containing the source/PieceId span and available
reference counts; the model cannot emit arbitrary diagnostic text.

Training and production artifacts are deliberately separate. Native LibTorch
state is written only to `mixfix-gru.ckpt`; inference loads the C++-exported
TorchScript module `mixfix-gru.pt`. The project does not use Python for model
training, export, loading, or inference.

Train the compiler mixfix GRU from the reusable JSONL corpus. Windows shells
pass the wildcard literally; the compiler expands it itself and treats invalid
records as rejection cases rather than positive targets:

```powershell
build\debug\felidae_compiler.exe --train 'datasets\compiler\*.jsonl' --store-model build --epochs 8 --learning-rate 0.001
```

After changing `datasets/tokenizer/` or regenerating `models/felidae.model`,
regenerate both compiler corpora together. Schema-v3 records carry the exact
SentencePiece model identity and compiler-IR vocabulary version, and training
rejects stale tokenizer or target IDs:

```bash
cmake --build build/debug --target felidae_extract_mixfix_dataset --parallel 1
./build/debug/felidae_extract_mixfix_dataset \
  datasets/compiler/mixfix-v1.jsonl v2_examples \
  --rejections datasets/compiler/mixfix-invalid-v1.jsonl
```

The VM has a different optional recurrent model exclusively for explicit
`SemanticEval` IR operations. It is built from
`src/form/RuntimeStateModel.cpp` and uses
a finite, typed result vocabulary (nil, numeric `0.0`/`1.0` truth values, a bounded Degree lattice, a
validated bounded input reference, or a fact derived from a validated input), never implicit
truthiness. No runtime artifact is shipped until it has been trained and
validated against FELBIR v13. No runtime artifact is currently shipped. The
dataset tool can prepare deterministic unary Identity teachers from verified
examples. Every record carries the permanent semantic operation ID `0x0001`;
source-name hashes are forbidden. Binaries with `SemanticEval` still require
explicit operation-level teachers rather than false whole-program labels.

The runtime trainer likewise keeps native state in `runtime-gru.ckpt` and
exports the production `runtime-gru.pt` as TorchScript. The VM rejects native
training archives at its production model boundary.

Train the reusable VM GRU JSONL baseline explicitly (C++/LibTorch only):

```powershell
build\debug\felidae_vm.exe --train datasets\vm\runtime-context-v1.jsonl --store-model build --epochs 8 --learning-rate 0.001
```

Both commands print held-out structural-family metrics, elapsed epoch seconds,
and training samples per second. These measurements are reported for review;
the README does not label a configuration efficient until a Release run has
been tested on the target machine.

## Validation

Run the focused compiler, binary, VM, SentencePiece, and GRU checks:

```powershell
$env:PATH = 'C:\libtorch\lib;' + $env:PATH
ctest --test-dir build -R 'felidae_(sentencepiece_model|sentencepiece_pipeline|mixfix_state_model|form_binary)' --output-on-failure
```

Create a portable shipment only after a successful Release build. The target
refuses Debug and other non-Release configurations:

```powershell
# Builds, runs focused tests, stages <build-directory>/dist/, starts the staged executables,
# and compiles/runs the nested-mixfix smoke program through them.
cmake --build build --target felidae_beta
# equivalent wrapper:
.\build.ps1 -Beta
```

`<build-directory>/dist/` contains the two executables, models, and required LibTorch DLLs; it
does not contain source or CMake build state. The staged package writes
`SHA256SUMS.txt`; verify it before uploading the beta.

## Beta release checklist

1. Start with the existing `build/` directory and a clean intended source
   revision. Do not create a parallel build directory.
2. Configure a Release LibTorch build, then run `cmake --build build --target
   felidae_beta`.
3. Confirm the gate reports the compiler and VM version
   `0.2.3-beta.1`, all focused tests pass, and the build-local `dist/` contains only the
   approved executables, models, runtime DLLs, `README.txt`, and
   `SHA256SUMS.txt`.
4. Copy `<build-directory>/dist/` to a clean Windows machine, verify checksums, compile a `.fx`
   file, and run its generated `.bin` using only the copied folder.
5. Tag and publish the exact revision only after that clean-machine smoke
   succeeds. Do not publish files directly from `build/`.
