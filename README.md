<div align="center">

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="https://raw.githubusercontent.com/vishalkrishnaag/vs-code-extension/master/icons/fx-dark.svg">
  <source media="(prefers-color-scheme: light)" srcset="https://raw.githubusercontent.com/vishalkrishnaag/vs-code-extension/master/icons/fx-light.svg">
  <img alt="Felidae" src="https://raw.githubusercontent.com/vishalkrishnaag/vs-code-extension/master/icons/fx-light.svg" width="112">
</picture>

# Felidae

### A reasoning language for facts, relationships, degrees, rules, and intelligent software.

**Felidae combines programming, structured knowledge, fuzzy reasoning, and carefully controlled learned behavior in a fast C++ language and runtime.**

<br>

[![Status](https://img.shields.io/badge/status-beta-F59E0B?style=for-the-badge)](#beta-status)
[![Version](https://img.shields.io/badge/version-0.2.3--beta.1-7C3AED?style=for-the-badge)](#beta-status)
[![C++](https://img.shields.io/badge/C%2B%2B-20-00599C?style=for-the-badge\&logo=cplusplus)](#building-felidae)
[![CMake](https://img.shields.io/badge/CMake-3.21%2B-064F8C?style=for-the-badge\&logo=cmake)](#building-felidae)
[![License](https://img.shields.io/badge/license-MIT-22C55E?style=for-the-badge)](./LICENCE)

<br>

[🌐 Website](https://felidae.xnovity.com)
  •  
[📚 Documentation](./docs)
  •  
[🧪 Examples](./v2_examples)
  •  
[🤝 Contributing](./CONTRIBUTING.md)
  •  
[🔐 Security](./SECURITY.md)

</div>

---

> [!IMPORTANT]
> **Felidae is currently beta software.**
>
> It is intended for development, evaluation, experimentation, research, and non-critical workflows.
>
> Production use is **not currently recommended**. Do not make safety-critical, medical, legal, financial, security-critical, or other high-risk decisions solely from Felidae outputs while the project remains in beta.

---

## Welcome to Felidae

Most programming languages are excellent at answering questions such as:

```text
What instruction should execute next?
What value should this function return?
Is this condition true or false?
```

But many real-world systems need to ask broader questions:

```text
What do we know?

How are these facts related?

Does this item belong to this category?

How strongly does something match?

What can be derived from the information available?

What if an answer is partially true rather than simply true or false?

Can different kinds of evidence be combined?

Can a program work with relationships and knowledge directly?
```

**Felidae is an experiment in building a programming language around those kinds of problems.**

It combines familiar programming ideas with:

* structured facts,
* relationships,
* hierarchy,
* degrees,
* fuzzy reasoning,
* queries,
* custom expressions,
* deterministic computation,
* and optional learned semantic evaluation.

The intention is not to make ordinary programming unnecessarily complicated.

The intention is to give developers another way to build software when the problem naturally involves **knowledge and reasoning**, rather than only instructions and data transformations.

---

## ✨ Felidae in one sentence

> **Felidae is a C++ reasoning language and runtime that lets software work directly with facts, relationships, degrees, and rules while keeping ordinary execution predictable.**

---

## Table of contents

* [Why Felidae?](#why-felidae)
* [What makes Felidae different?](#what-makes-felidae-different)
* [What Felidae can be useful for](#what-felidae-can-be-useful-for)
* [Felidae at a glance](#felidae-at-a-glance)
* [A quick feel for the language](#a-quick-feel-for-the-language)
* [Facts](#facts)
* [Relationships and hierarchy](#relationships-and-hierarchy)
* [Degrees and fuzzy reasoning](#degrees-and-fuzzy-reasoning)
* [Queries and fact traversal](#queries-and-fact-traversal)
* [Methods and ordinary programming](#methods-and-ordinary-programming)
* [Flexible expressions and mixfix](#flexible-expressions-and-mixfix)
* [Numeric operations](#numeric-operations)
* [Tensor operations](#tensor-operations)
* [Reasoning and proof-style workflows](#reasoning-and-proof-style-workflows)
* [Deterministic and learned behavior](#deterministic-and-learned-behavior)
* [How Felidae works](#how-felidae-works)
* [Compiler and VM separation](#compiler-and-vm-separation)
* [Verified binary programs](#verified-binary-programs)
* [Beta status](#beta-status)
* [Quick start](#quick-start)
* [Requirements](#requirements)
* [Building Felidae](#building-felidae)
* [Linux and WSL](#linux-and-wsl)
* [Windows](#windows)
* [macOS](#macos)
* [Android and cross-compilation](#android-and-cross-compilation)
* [Running a program](#running-a-program)
* [Long-lived VM mode](#long-lived-vm-mode)
* [Working examples](#working-examples)
* [Repository structure](#repository-structure)
* [Dependencies](#dependencies)
* [Testing](#testing)
* [Debugging](#debugging)
* [Code quality](#code-quality)
* [LibTorch](#libtorch)
* [SentencePiece and the tokenizer](#sentencepiece-and-the-tokenizer)
* [Compiler recurrent model](#compiler-recurrent-model)
* [VM recurrent model](#vm-recurrent-model)
* [Training](#training)
* [Release builds](#release-builds)
* [Documentation](#documentation)
* [Editor support](#editor-support)
* [Development principles](#development-principles)
* [Project maturity](#project-maturity)
* [Security](#security)
* [Contributing](#contributing)
* [FAQ](#frequently-asked-questions)
* [License](#license)
* [Project](#project)

---

# Why Felidae?

Software increasingly works with information that is more complicated than a simple boolean condition.

Consider statements such as:

* this customer is **similar** to another customer,
* this observation has **high confidence**,
* this employee belongs to a broader organizational category,
* this product is **partially relevant** to a request,
* this fact is inherited from a more general category,
* several pieces of evidence support a conclusion,
* one explanation is stronger than another,
* a condition is almost satisfied but does not cross a final threshold.

Traditional programming can represent all of these things.

But developers often have to manually build layers of:

```text
objects
+
rules
+
database queries
+
classification logic
+
scoring systems
+
relationship graphs
+
application-specific conventions
```

Felidae explores whether some of those ideas can live directly inside the language and runtime.

Instead of treating reasoning as an external feature bolted onto an application, Felidae makes concepts such as **facts, hierarchy, degrees, and queries part of the programming model itself**.

---

# What makes Felidae different?

Felidae is not designed around one single feature.

Its identity comes from combining several ideas.

| Capability                   | What it means                                                               |
| ---------------------------- | --------------------------------------------------------------------------- |
| 🐾 **Facts**                 | Structured knowledge can exist directly in the language                     |
| 🌳 **Hierarchy**             | Facts and types can have broader and more specific relationships            |
| 🌗 **Degrees**               | Values do not always need to collapse immediately into `true` or `false`    |
| 🔎 **Queries**               | Programs can work with groups of known facts                                |
| 🧠 **Reasoning**             | Facts, rules, hierarchy and evidence can participate in reasoning workflows |
| 🧩 **Flexible expressions**  | Domain-oriented expressions can be expressed using mixfix forms             |
| ⚙️ **Deterministic runtime** | Ordinary computation remains predictable                                    |
| 🔬 **Controlled learning**   | Learned behavior is limited to explicit, validated parts of the system      |
| 📦 **Compiled programs**     | `.fx` source is compiled before execution                                   |
| 🛡️ **Verification**         | Compiled programs are checked before the VM accepts them                    |
| 🚀 **C++ runtime**           | The compiler and VM are implemented primarily in modern C++                 |

---

# What Felidae can be useful for

Felidae is experimental, but its design is particularly relevant to software involving:

### Expert systems

Applications where knowledge and rules are evaluated together.

Examples:

* diagnosis-like workflows,
* recommendation rules,
* configuration reasoning,
* operational decision support,
* policy evaluation.

### Knowledge-oriented software

Systems where relationships between facts are as important as the facts themselves.

Examples:

* organizational knowledge,
* product hierarchies,
* scientific classifications,
* domain models,
* knowledge graphs.

### Fuzzy decision systems

Applications where there is meaningful information between `false` and `true`.

Examples:

* similarity,
* suitability,
* confidence,
* membership,
* relevance,
* preference,
* quality scoring.

### Rule engines

Systems with domain rules that need more expressive structure than a large collection of application-level `if` statements.

### Contextual evaluation

Programs that evaluate information differently depending on the surrounding facts or state.

### Reasoning research

Experiments involving:

* hierarchical reasoning,
* evidence,
* partial truth,
* alternate proofs,
* semantic evaluation,
* symbolic structures,
* learned assistance.

### Domain-specific languages

Felidae's custom/mixfix expression support can make certain domain concepts more readable than ordinary function calls.

### Analytical systems

Felidae can combine:

* facts,
* numeric operations,
* degree calculations,
* tensors,
* hierarchy,
* and rule execution.

---

# Felidae at a glance

```text
File extension        .fx

Implementation        C++20

Build system          CMake

Main components       Compiler + Form VM

Program output        Verified .bin executable

Reasoning model       Facts + hierarchy + degrees + explicit semantics

ML backend            LibTorch

Tokenizer             SentencePiece

Primary status        Beta

Current beta          0.2.3-beta.1

License               MIT
```

---

# A quick feel for the language

Felidae can still look familiar to someone who has used ordinary programming languages.

For example:

```felidae
Person(name: "unknown", age: 0, active: false)

Employee extend Person(
    name: "unknown",
    age: 0,
    active: true,
    role: "staff"
)

Engineer extend Employee(
    name: "unknown",
    age: 0,
    active: true,
    role: "engineer",
    level: 1
)
```

A function can look like:

```felidae
increment(value: number) =>

    return value + 1
```

Functions may call other functions:

```felidae
double(value: number) =>

    return value * 2

scoreFor(age: number, level: number) =>

    base := double(value: age)

    return base + level
```

Recursion is also represented in the current examples:

```felidae
factorial(value: number) =>

    if value <= 1 then

        return 1

    else

        previous := factorial(value: value - 1)

        return value * previous
```

And programs can return structured values:

```felidae
return {
    name: "Felidae",
    active: true,
    tags: ["facts", "reasoning", "degrees"]
}
```

So Felidae is not only a rule notation.

It remains a programming language while adding reasoning-oriented concepts.

---

# Facts

Facts are one of the central ideas in Felidae.

A declaration such as:

```felidae
Person(
    name: "Ada",
    age: 32,
    active: true
)
```

represents structured information that can participate in runtime operations.

A fact may contain:

* names,
* numbers,
* boolean values,
* strings,
* arrays,
* maps,
* degree values,
* and other supported values.

Facts can then become part of:

* queries,
* hierarchy,
* loops,
* comparisons,
* reasoning,
* reporting,
* semantic evaluation.

This is different from treating a fact as merely an application-specific JSON document.

Felidae understands the fact as part of the language/runtime model.

---

# Relationships and hierarchy

Felidae supports relationships between fact types.

For example:

```felidae
Animal(name: "generic")

Dog extend Animal(name: "fido")

Cat extend Animal(name: "milo")
```

Here:

```text
Animal
├── Dog
└── Cat
```

This allows broader operations to include more specific types.

Instead of manually writing logic such as:

```text
if object is Animal
or object is Dog
or object is Cat
...
```

a hierarchy-aware operation can start from `Animal`.

This becomes especially useful when the hierarchy becomes deeper.

For example:

```text
Person
└── Employee
    └── Engineer
```

A system can work with the broader concept while still preserving the more specific type.

---

# Degrees and fuzzy reasoning

Ordinary boolean logic gives two values:

```text
false
true
```

That is ideal for many problems.

For example:

```text
Is 10 greater than 5? → true
```

But other questions are naturally gradual.

For example:

```text
How similar are these values?

How strongly does this score belong to this category?

How confident is this observation?

How close is a value to the ideal?
```

Felidae allows these kinds of values to remain numeric degrees rather than immediately forcing them into a boolean.

For example:

```felidae
membershipDegree := membership(score, profile)

closenessDegree := similarity(score, 75)
```

A degree can later be converted into a crisp decision when the program explicitly chooses a threshold:

```felidae
if degree >= 75% then

    return "met"

else

    return "not-met"
```

This distinction is important.

Felidae does not try to make every boolean operation fuzzy.

Instead:

```text
graded information remains graded
until
the program explicitly needs a crisp decision
```

---

## Example degree profile

A profile might describe a graded category:

```felidae
RatingProfile(
    name: "Strong / Agree",
    peak: 75,
    fades_in: 50,
    fades_out: 90
)
```

The program can then calculate how strongly another value belongs to that profile.

This is useful for concepts such as:

* low / medium / high,
* poor / acceptable / strong,
* unlikely / possible / likely,
* weak / moderate / strong evidence,
* low / medium / high similarity.

---

# Queries and fact traversal

Fact types own their query operations. There is no separate `Fact` library or
selection-object representation:

```felidae
School(name: "North", district: "central", active: 1.0)
School(name: "West", district: "west", active: 0.0)

main() =>
    allSchools := School.all()
    activeCentral := School.select(district: "central", active: 1.0)
    return (all: allSchools, selected: activeCentral, count: School.count())
end
```

`Type.all()` and `Type.select(...)` compile to the same verified fact-iteration
IR used by `lambda(Type, ...)`; hierarchy traversal includes assignable child
facts. Exact truth values are numeric `0.0` and `1.0`.

CSV rows can enter that same native fact store and be synchronized from the VM:

```felidae
rows := csv.toFacts(data: csvText, type: "School")
saved := db.sync(file: "build/runtime/schools.csv")
```

`db.sync` accepts a fact-only `.fx` file or a homogeneous `.csv` table. CSV
sync rejects mixed types or schemas rather than silently losing fields. See
`v2_examples/csv_fact_database.fx` for the complete import, query, aggregate,
and write-back flow.

Because child types are assignable to their parents, a parent query can work
with the hierarchy rather than only one exact concrete type.

This gives Felidae a natural path for:

* typed fact traversal,
* hierarchical searches,
* fact filtering,
* relationship reasoning,
* group operations.

---

# Methods and ordinary programming

Felidae also supports conventional language constructs.

The current compiler/runtime examples cover areas such as:

* procedures,
* arguments,
* named arguments,
* return values,
* recursion,
* conditions,
* arrays,
* maps,
* arithmetic,
* comparisons,
* methods,
* imports,
* custom operators.

This is intentional.

Felidae is not meant to require a reasoning operation for ordinary computation.

Simple work should remain simple.

For example:

```felidae
double(value: number) =>

    return value * 2
```

does not need machine learning, fuzzy reasoning, or a semantic model.

It is normal deterministic computation.

---

# Flexible expressions and mixfix

Felidae supports **mixfix expressions**.

The term sounds technical, but the underlying idea is simple:

> A language can allow domain-specific expressions that do not always look like ordinary `function(argument)` calls.

For example, some domains are naturally expressed as:

```text
something BETWEEN something AND something
```

or:

```text
IF something THEN something
```

or another custom pattern.

Felidae's mixfix system explores allowing such forms while still compiling them into the same executable runtime representation used by ordinary code.

The repository includes examples covering:

* nested mixfix expressions,
* prefix/infix combinations,
* custom operators,
* overloaded operators,
* typed mixfix expressions,
* fact reasoning with mixfix,
* fuzzy facts with mixfix,
* same-anchor patterns,
* deeply nested patterns.

Representative examples include:

```text
v2_examples/mixfix_ir_roundtrip.fx
v2_examples/mixfix_deep_ir_nesting.fx
v2_examples/mixfix_fuzzy_fact_report.fx
v2_examples/mixfix_operator_overload.fx
v2_examples/mixfix_expr_fact_reasoning.fx
v2_examples/nested_mixfix_overload_resolution.fx
```

---

# Numeric operations

Felidae includes deterministic numeric operations directly in the runtime.

Current operations include functionality such as:

```text
MIN
MAX
ABS
AVG
CLAMP
SQRT
POW
LERP
```

along with range and finite-value checks.

These operations execute as ordinary runtime operations.

They do **not** require the optional semantic model.

This is an important design rule:

> If an operation has a clear deterministic answer, Felidae should normally compute that answer directly.

See:

```text
v2_examples/numeric_operations.fx
```

for examples.

---

# Tensor operations

Supported desktop builds can also use LibTorch-backed tensor functionality.

This allows Felidae to work with numeric arrays using operations such as:

* tensor metadata,
* cloning,
* transpose,
* symmetry checks,
* differences,
* cosine similarity,
* dot product,
* mean squared error,
* sigmoid,
* ReLU.

These operations remain deterministic.

The existence of LibTorch inside Felidae does **not** mean every tensor operation is passed through a learned model.

Tensor mathematics and semantic learning are separate concerns.

See:

```text
v2_examples/tensor_operations.fx
```

for representative functionality.

---

# Reasoning and proof-style workflows

Felidae includes reasoning-oriented examples that go beyond simple arithmetic.

The repository currently includes examples such as:

```text
coffee_vending_theorem_solver.fx
air_conditioner_theorem_solver.fx
contextual_fact_intelligence.fx
deep_fact_reasoning_analysis.fx
selective_relationship_reasoning.fx
temporal_fact_reasoning.fx
sentiment_fact_expert_system.fx
animal_fact_similarity_evidence.fx
```

The reasoning benchmark covers areas including:

* hierarchy,
* explicit unification,
* theorem-like expressions,
* bounded alternative proofs,
* safe failure cases,
* deterministic execution,
* fault tolerance,
* latency,
* held-out learning evaluation.

See:

```text
docs/reasoning_benchmark.md
```

for the benchmark methodology.

Felidae does **not** claim that these benchmarks represent human intelligence or a human IQ score.

The benchmark is designed to measure specific system behaviors independently.

---

# Deterministic and learned behavior

Felidae deliberately separates two categories of work.

## Deterministic work

Examples:

```text
2 + 2

MAX(10, 20)

array lookup

procedure call

fact field access

comparison

hierarchical traversal

tensor dot product
```

When the answer is defined by the language, the result should come from ordinary execution.

---

## Learned or semantic work

Some situations may require choosing between interpretations or evaluating explicitly semantic operations.

Felidae experiments with recurrent neural models for these limited cases.

There are currently **two different model roles**.

### Compiler-side model

This model is associated with constrained compiler situations such as ambiguous mixfix structures.

It does not replace the compiler.

The compiler still owns:

* syntax,
* validation,
* source errors,
* binary generation,
* verification.

Model output must pass validation before it can become part of a compiled program.

### VM-side model

The VM contains a separate experimental recurrent model for explicit semantic evaluation operations.

This model is not used automatically for ordinary arithmetic, boolean logic, fact access, or tensor mathematics.

Semantic evaluation must be explicitly represented.

---

## Why separate them?

Compiler uncertainty and runtime semantic uncertainty are different problems.

Therefore Felidae does not treat them as one universal AI component.

```text
Source ambiguity
      ↓
Compiler model

Runtime semantic operation
      ↓
VM model
```

Each model has its own:

* responsibilities,
* datasets,
* training,
* validation,
* runtime boundary.

---

# How Felidae works

At the highest level, Felidae is straightforward:

```text
             Felidae source
                  .fx
                   │
                   ▼
             ┌──────────┐
             │ Compiler │
             └──────────┘
                   │
                   ▼
          verified binary program
                  .bin
                   │
                   ▼
             ┌─────────┐
             │ Form VM │
             └─────────┘
                   │
                   ▼
                result
```

The compiler understands source code.

The VM executes compiled programs.

This separation is one of the most important architectural principles in Felidae.

---

# Compiler and VM separation

Felidae deliberately keeps the compiler and runtime separate.

The compiler is responsible for understanding `.fx` source.

The runtime should not need to understand source syntax.

Conceptually:

```text
SOURCE SIDE

.fx source
   ↓
token IDs
   ↓
parser
   ↓
compiler
   ↓
verified executable binary


RUNTIME SIDE

.bin
 ↓
loader
 ↓
verification
 ↓
Form VM
 ↓
result
```

The syntax-shaped tree is compiler-only high-level IR (HIR). The VM receives
only verified executable IR and never includes or interprets HIR nodes.

This provides several benefits.

### Cleaner responsibilities

The compiler handles language syntax.

The VM handles execution.

### Smaller runtime boundary

Source-language machinery does not need to be carried into normal execution.

### Better validation

A compiled binary can be checked before it runs.

### Easier evolution

The source language can evolve without forcing every parser detail into the VM.

---

# Verified binary programs

Felidae source files compile to:

```text
.bin
```

files.

The binary contains the information needed by the runtime, including concepts such as:

* instructions,
* operands,
* registers,
* constants,
* procedures,
* symbols,
* source locations,
* fact/type information.

The binary is verified before normal execution.

Malformed or invalid programs should fail at the binary boundary rather than being blindly executed.

---

## Why binary programs?

Compilation provides a clean separation:

```text
Developer machine

source.fx
   ↓
compiler
   ↓
program.bin


Runtime

program.bin
   ↓
VM
```

That separation also makes it possible to reason clearly about:

* compiler correctness,
* binary correctness,
* runtime correctness,
* semantic-model correctness.

---

## Beta binary compatibility

Felidae's binary format is still evolving.

During beta:

> A `.bin` file generated by an older beta build may need to be rebuilt with the current compiler.

Pre-release binary compatibility should not be assumed unless a particular release explicitly documents it.

Keep the `.fx` source as the authoritative program representation during beta.

---

# Beta status

Current supported beta:

```text
0.2.3-beta.1
```

Felidae is under active development.

Beta means the project is real and usable for testing, but major areas are still evolving.

During beta, the following may change:

* syntax,
* compiler behavior,
* binary representation,
* VM instructions,
* APIs,
* model formats,
* training data formats,
* experimental semantic features,
* platform support.

---

## Recommended beta use

Felidae is suitable for:

✅ development

✅ experimentation

✅ research

✅ learning

✅ language exploration

✅ reasoning experiments

✅ proof-of-concept systems

✅ non-critical internal workflows

✅ benchmark development

---

## Not currently recommended

Felidae beta should not be the sole decision-maker for:

❌ medical decisions

❌ legal decisions

❌ financial decisions

❌ safety-critical systems

❌ infrastructure safety controls

❌ irreversible high-impact decisions

❌ security-critical authorization

Production use is not currently recommended.

See [SECURITY.md](./SECURITY.md).

---

# Quick start

## 1. Clone Felidae

Felidae uses Git submodules, so clone recursively:

```bash
git clone --recurse-submodules https://github.com/xnvtserver/Felidae.git
cd Felidae
```

If you already cloned the repository:

```bash
git submodule sync --recursive
git submodule update --init --recursive
```

---

## 2. Build

On a supported Linux development system:

```bash
./build.sh --mode test
```

The normal Linux test build is created under:

```text
build/test/
```

The build script:

1. configures CMake,
2. builds the compiler,
3. builds the VM,
4. builds the tests,
5. runs CTest.

---

## 3. Compile an example

```bash
./build/test/felidae_compiler \
  v2_examples/form_core_concepts.fx
```

---

## 4. Run it

```bash
./build/test/felidae_vm \
  build/test/form_core_concepts.bin
```

You have now executed a Felidae program through the normal:

```text
.fx → compiler → .bin → VM
```

pipeline.

---

# Requirements

The normal project requires:

| Requirement            | Purpose                                             |
| ---------------------- | --------------------------------------------------- |
| C++20-capable compiler | Building Felidae                                    |
| CMake 3.21+            | Build configuration                                 |
| Git                    | Repository/submodules                               |
| SentencePiece          | Token representation                                |
| LibTorch               | Tensor/model functionality on normal desktop builds |
| CTest                  | Test execution                                      |
| Ninja                  | Recommended build backend                           |

Useful development tools include:

```text
GDB
LLDB
clang-tidy
cppcheck
Valgrind
ccache
```

depending on your platform.

---

# Building Felidae

Felidae includes:

```text
build.sh
build.ps1
build.cmd
```

as well as direct CMake support.

The portable Unix-style build script supports options such as:

```bash
./build.sh \
  --mode test \
  --libtorch ON \
  --training OFF \
  --jobs 8
```

Main options include:

| Option       | Values            | Meaning                        |
| ------------ | ----------------- | ------------------------------ |
| `--mode`     | `test`, `release` | Build purpose                  |
| `--training` | `ON`, `OFF`       | Compile training functionality |
| `--libtorch` | `ON`, `OFF`       | Enable LibTorch                |
| `--platform` | platform name     | Select target platform         |
| `--arch`     | architecture      | Select target architecture     |
| `--jobs`     | number / auto     | Parallel build jobs            |
| `--sanitize` | flag              | Enable sanitizer build         |

---

# Linux and WSL

A normal Linux test build is:

```bash
./build.sh --mode test
```

Limit parallelism on machines with less memory:

```bash
./build.sh --mode test --jobs 2
```

or:

```bash
FELIDAE_JOBS=2 ./build.sh --mode test
```

For a manual Debug build:

```bash
cmake -S . -B build/debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DFELIDAE_ENABLE_LIBTORCH=ON \
  -DFELIDAE_BUILD_TESTS=ON
```

Then:

```bash
cmake --build build/debug \
  --target felidae_compiler felidae_vm felidae_debug felidae_tests \
  --parallel 1
```

Run tests:

```bash
ctest \
  --test-dir build/debug \
  --output-on-failure
```

---

# Windows

Felidae supports native Windows development through CMake.

With LibTorch installed at:

```text
C:\libtorch
```

configure:

```powershell
cmake -S . -B build/debug -A x64 `
  -DFELIDAE_ENABLE_LIBTORCH=ON `
  -DCMAKE_PREFIX_PATH=C:\libtorch
```

Build the compiler and VM:

```powershell
cmake --build build/debug `
  --config Debug `
  --target felidae_compiler felidae_vm
```

Optional development targets:

```powershell
cmake --build build/debug `
  --config Debug `
  --target felidae_debug
```

```powershell
cmake --build build/debug `
  --config Debug `
  --target felidae_tests
```

Run tests:

```powershell
ctest `
  --test-dir build/debug `
  -C Debug `
  --output-on-failure
```

If LibTorch DLLs are not available on `PATH`:

```powershell
$env:PATH = 'C:\libtorch\lib;' + $env:PATH
```

---

# macOS

The build system includes native macOS handling for:

```text
x86_64
arm64
```

For Apple Silicon, for example:

```bash
FELIDAE_LIBTORCH_PATH=/path/to/libtorch \
./build.sh \
  --platform macos \
  --arch arm64 \
  --mode release
```

The LibTorch distribution must match the target architecture.

---

# Android and cross-compilation

Felidae contains an Android cross-compilation path using the official Android NDK.

Example:

```bash
ANDROID_NDK_HOME=/opt/android-ndk \
./build.sh \
  --platform android \
  --arch arm64 \
  --android-api 24 \
  --mode release \
  --libtorch OFF
```

Android is not currently treated as the primary production target.

LibTorch is disabled when it is unavailable for the target.

Cross-compiled tests can be built even when they cannot be executed on the host machine.

For another toolchain, Felidae also supports:

```text
FELIDAE_TOOLCHAIN_FILE
```

for custom CMake toolchain configuration.

---

# Running a program

Compile:

```bash
./build/debug/felidae_compiler \
  v2_examples/form_core_concepts.fx
```

Run:

```bash
./build/debug/felidae_vm \
  build/debug/form_core_concepts.bin
```

The compiler writes the generated binary into the active build/output area instead of modifying the source example directory.

---

# Long-lived VM mode

Felidae also contains a long-lived Form VM mode.

Example:

```bash
felidae_vm --serve program.bin
```

The current server mode supports commands around concepts such as:

```text
run

facts

field

history

proof

load

modules

quit
```

This allows the VM to remain alive instead of starting a completely new process for every interaction.

The feature is particularly relevant to future reasoning and fact-oriented workflows.

---

# Working examples

`v2_examples/` is one of the best places to understand the current language.

The examples are executable development artifacts rather than only conceptual documentation.

---

## Core language

| Example                         | Covers                                                   |
| ------------------------------- | -------------------------------------------------------- |
| `form_core_concepts.fx`         | Core types, functions, recursion, arrays, maps and facts |
| `ast_typed_methods.fx`          | Typed methods                                            |
| `dotless_method_syntax.fx`      | Alternative method syntax                                |
| `list_data_structure.fx`        | List structures                                          |
| `standard_search_algorithms.fx` | Search algorithms                                        |
| `parser_fold.fx`                | Parser/fold-oriented behavior                            |

---

## Facts and hierarchy

| Example                                | Covers                             |
| -------------------------------------- | ---------------------------------- |
| `hierarchical_fact_filter.fx`          | Concrete parent-type hierarchy query |
| `direct_ancestor_analysis.fx`          | Ancestor relationships             |
| `fact_degree_loop.fx`                  | Fact traversal with degree values  |
| `fact_similarity_requires_ancestry.fx` | Similarity constrained by ancestry |
| `selective_fact_query.fx`              | Fact selection                     |
| `selective_relationship_reasoning.fx`  | Relationship reasoning             |
| `temporal_fact_reasoning.fx`           | Time-related fact reasoning        |

---

## Degree and fuzzy behavior

| Example                              | Covers                                 |
| ------------------------------------ | -------------------------------------- |
| `degree_profiles.fx`                 | Membership, similarity and thresholds  |
| `graded_evidence_profiles.fx`        | Graded evidence                        |
| `animal_fact_similarity_evidence.fx` | Similarity with facts                  |
| `mixfix_fuzzy_fact_report.fx`        | Fuzzy degrees combined with mixfix     |
| `sentiment_fact_expert_system.fx`    | Degree-oriented expert-system behavior |

---

## Reasoning

| Example                               | Covers                            |
| ------------------------------------- | --------------------------------- |
| `coffee_vending_theorem_solver.fx`    | Theorem-style reasoning           |
| `air_conditioner_theorem_solver.fx`   | Rule/reasoning workflow           |
| `deep_fact_reasoning_analysis.fx`     | Deeper fact analysis              |
| `contextual_fact_intelligence.fx`     | Context-sensitive fact processing |
| `selective_relationship_reasoning.fx` | Relationship-oriented reasoning   |

---

## Mixfix and custom operators

| Example                                | Covers                         |
| -------------------------------------- | ------------------------------ |
| `mixfix_ir_roundtrip.fx`               | Mixfix compilation             |
| `mixfix_deep_ir_nesting.fx`            | Deeply nested mixfix           |
| `mixfix_deep_chain_stress.fx`          | Long expression chains         |
| `mixfix_operator_overload.fx`          | Operator overloads             |
| `mixfix_expr_fact_reasoning.fx`        | Mixfix with facts              |
| `mixfix_shape_inference.fx`            | Pattern shapes                 |
| `mixfix_shape_matrix.fx`               | Mixfix shape combinations      |
| `mixfix_same_anchor.fx`                | Same-anchor expressions        |
| `mixfix_same_anchor_long.fx`           | Longer same-anchor expressions |
| `nested_mixfix_overload_resolution.fx` | Nested overload resolution     |
| `symbolic_operator_overload.fx`        | Symbolic operators             |
| `user_defined_operator_types.fx`       | User-defined operator types    |
| `typed_builtin_operator_overload.fx`   | Typed operator overloads       |

---

## Runtime semantic model

| Example                           | Covers                          |
| --------------------------------- | ------------------------------- |
| `runtime_ssm_e2e.fx`              | Runtime semantic model pipeline |
| `runtime_ssm_identity_array.fx`   | Array identity case             |
| `runtime_ssm_identity_boolean.fx` | Boolean identity case           |
| `runtime_ssm_identity_degree.fx`  | Degree identity case            |
| `runtime_ssm_identity_fact.fx`    | Fact identity case              |
| `runtime_ssm_identity_text.fx`    | Text identity case              |
| `runtime_training_smoke.fx`       | Training workflow smoke case    |

---

## Native and imported functionality

Representative examples include:

```text
import_smoke.fx
imported_math.fx
imported_public_operator.fx
native_lazy_import.fx
native_packages_smoke.fx
set_group_native.fx
third_party_native_smoke.fx
```

---

# Repository structure

The project currently contains areas such as:

```text
Felidae/
│
├── cmake/
│   └── CMake support
│
├── core/
│   └── Core project components
│
├── datasets/
│   ├── Compiler data
│   ├── Runtime data
│   └── Tokenizer data
│
├── docs/
│   └── Detailed documentation
│
├── examples/
│   └── Additional examples
│
├── ml/
│   └── Model-related implementation
│
├── models/
│   └── Model/tokenizer assets
│
├── native_modules/
│   └── Native integration modules
│
├── scripts/
│   └── Project utilities
│
├── src/
│   ├── Compiler
│   ├── Integer parser
│   ├── IR generation
│   └── Form VM/runtime
│
├── tests/
│   └── Test suites
│
├── third_party/
│   └── Pinned external dependencies
│
├── tools/
│   └── Development tools
│
├── v2_examples/
│   └── Current production-pipeline examples
│
├── tree-sitter-felidae/
│   └── Tree-sitter support
│
├── emacs-extension/
├── intellij-idea-extension/
├── nano-extension/
├── notepad-plus-plus-extension/
├── sublime-text-extension/
├── vim-extension/
├── vs-code-extension/
└── zed-extension/
```

The editor-related repositories are maintained as Git submodules.

---

# Dependencies

Felidae intentionally uses a relatively small set of C++ dependencies.

Current submodule dependencies include:

### SentencePiece

Used for Felidae's integer/token representation.

### nlohmann/json

Used for JSON handling in project tooling/data paths.

### Abseil

Provides selected C++ utility components.

### Protocol Buffers

Available for structured serialization-related needs.

### Eigen

Provides mathematical functionality.

### cpp-httplib

Lightweight HTTP functionality.

### rapidcsv

CSV functionality.

### LibTorch

PyTorch's native C++ distribution.

LibTorch is not kept as an ordinary source submodule in the same way as the lightweight dependencies; developers provide an appropriate platform package.

---

# Testing

Felidae uses CTest for the primary native test workflow.

After a Debug build:

```bash
ctest \
  --test-dir build/debug \
  --output-on-failure
```

With the normal build script:

```bash
./build.sh --mode test
```

tests are built and executed automatically on native supported hosts.

---

## Example tests

The test suite also runs representative `.fx` examples through:

```text
compiler
   ↓
binary
   ↓
VM
```

This is important because a parser-only or compiler-only test cannot demonstrate that the final runtime program actually works.

Run example-labelled tests:

```bash
ctest \
  --test-dir build/debug \
  -L examples \
  --output-on-failure
```

---

# Debugging

Compiler debugging:

```bash
gdb --args \
  ./build/debug/felidae_compiler \
  v2_examples/form_core_concepts.fx
```

VM debugging:

```bash
gdb --args \
  ./build/debug/felidae_vm \
  build/debug/form_core_concepts.bin
```

---

## Runtime trace

Debug builds support an optional trace:

```bash
FELIDAE_TRACE=1 \
./build/debug/felidae_compiler \
v2_examples/form_core_concepts.fx
```

and:

```bash
FELIDAE_TRACE=1 \
./build/debug/felidae_vm \
build/debug/form_core_concepts.bin
```

Trace output goes to:

```text
stderr
```

while normal program results remain on:

```text
stdout
```

This keeps diagnostic information separate from application output.

---

## Source-analysis tool

Felidae also includes:

```text
felidae_debug
```

This is a source-analysis/debugging tool.

It is **not** the production Form VM.

Examples:

```bash
./build/debug/felidae_debug \
  v2_examples/form_core_concepts.fx \
  --check
```

Structured diagnostics:

```bash
./build/debug/felidae_debug \
  v2_examples/form_core_concepts.fx \
  --check-json
```

---

# Code quality

The project supports additional C++ analysis tools.

These are intentionally not run during every normal build because they are substantially slower.

---

## clang-tidy

```bash
clang-tidy \
  -p build/debug \
  src/form/RegisterVm.cpp \
  src/form/LibTorchTensorRuntime.cpp \
  src/IrCodeGenerator.cpp \
  src/IntegerParser.cpp
```

---

## cppcheck

```bash
cppcheck \
  --enable=warning,performance,portability \
  --std=c++20 \
  --project=build/debug/compile_commands.json \
  --suppress=missingIncludeSystem
```

---

## Valgrind

```bash
valgrind \
  --error-exitcode=1 \
  --leak-check=full \
  ./build/debug/felidae_libtorch_smoke_test
```

---

## Sanitizers

Create a separate sanitizer build:

```bash
cmake -S . -B build/asan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DFELIDAE_ENABLE_SANITIZERS=ON \
  -DFELIDAE_ENABLE_LIBTORCH=ON \
  -DFELIDAE_BUILD_TESTS=ON
```

Build:

```bash
cmake --build build/asan \
  --target felidae_tests \
  --parallel 1
```

Run:

```bash
ctest \
  --test-dir build/asan \
  --output-on-failure
```

---

# LibTorch

Felidae uses **LibTorch**, the native C++ distribution of PyTorch.

Python is not required for the current normal compiler/runtime training path.

LibTorch is used for areas including:

* tensor operations,
* compiler recurrent-model support,
* VM recurrent-model support.

For normal desktop development, install an official LibTorch package matching:

```text
operating system
+
architecture
+
compiler ABI
+
CPU/GPU requirement
```

A CPU-only package is appropriate for machines without a supported GPU.

A common Linux installation location is:

```text
/opt/libtorch
```

The build script will use:

```text
FELIDAE_LIBTORCH_PATH
```

when an explicit location is required.

Example:

```bash
FELIDAE_LIBTORCH_PATH=/opt/libtorch \
./build.sh \
  --mode test \
  --libtorch ON
```

---

# SentencePiece and the tokenizer

Felidae intentionally represents source language tokens using integer IDs.

SentencePiece provides the project's tokenizer representation.

A simplified view is:

```text
source text

   ↓

SentencePiece IDs

   ↓

integer parser

   ↓

compiler

   ↓

verified binary
```

The runtime itself is not designed around repeatedly parsing human-readable source text.

Human-readable text is decoded when a presentation boundary requires it.

---

## Fixed tokenizer model

The tokenizer is generated from the project's versioned tokenizer corpus:

```text
datasets/tokenizer/
```

The corpus covers representative syntax families such as:

* facts,
* hierarchy,
* methods,
* control flow,
* operators,
* queries,
* mixfix,
* semantic operations,
* Unicode,
* strings,
* temporal fields.

Tokenizer regeneration is a maintainer operation.

It should not occur during every normal build.

---

<details>
<summary><strong>Advanced: tokenizer regeneration</strong></summary>

<br>

After intentionally changing the authoritative tokenizer corpus or grammar inputs:

```bash
cmake --build build/debug \
  --target felidae_regenerate_sentencepiece_model \
  --parallel 8
```

Generated tokenizer changes should only be committed when:

* an authoritative input actually changed,
* tokenizer tests pass,
* compiler pipeline tests pass,
* generated IDs are intentionally updated.

Routine builds should consume the checked-in model rather than rewrite it.

</details>

---

# Compiler recurrent model

Felidae contains an optional compiler-side recurrent model for constrained mixfix ambiguity.

Its purpose is **not**:

```text
source → AI → executable
```

Instead, the normal compiler remains responsible for the language.

The model is only allowed to participate at an explicit ambiguity boundary.

Conceptually:

```text
normal expression
      ↓
deterministic compiler
      ↓
compiled


ambiguous supported mixfix span
      ↓
compiler model
      ↓
validated structural result
      ↓
compiler verification
      ↓
compiled or rejected
```

If the model cannot produce an acceptable result, compilation should fail safely rather than secretly executing source through an alternate interpreter.

---

## Compiler model principles

The model:

* uses a bounded structural vocabulary,
* works with integer-oriented structures,
* does not own the entire parser,
* does not replace source validation,
* does not directly execute source,
* must produce output accepted by compiler verification.

The repository currently does not treat an unvalidated model output as executable truth.

---

# VM recurrent model

Felidae's Form VM contains a **different** optional recurrent model for explicit runtime semantic evaluation.

This model is separate from the compiler model because runtime meaning and source ambiguity are different problems.

Conceptually:

```text
verified runtime state
        +
explicit semantic operation
        ↓
runtime model
        ↓
bounded typed result
        ↓
validation
        ↓
VM value
```

---

## What the VM model does not replace

It is not intended to replace:

* addition,
* subtraction,
* multiplication,
* division,
* comparisons,
* ordinary boolean logic,
* procedure calls,
* deterministic fact access,
* deterministic numeric operations,
* tensor mathematics.

Those operations execute directly.

---

## Runtime semantic results

The runtime semantic path is intentionally constrained to supported result categories rather than unrestricted arbitrary memory or instruction generation.

This keeps learned behavior behind a defined runtime boundary.

---

# Training

Training support is explicit.

It is not started automatically by an ordinary Felidae build.

Training requires:

```text
LibTorch = ON
Training = ON
```

For example:

```bash
./build.sh \
  --mode test \
  --libtorch ON \
  --training ON
```

---

<details>
<summary><strong>Advanced: compiler-model training</strong></summary>

<br>

The compiler can train from reusable JSONL datasets.

Representative Windows command:

```powershell
build\debug\felidae_compiler.exe `
  --train 'datasets\compiler\*.jsonl' `
  --store-model build `
  --epochs 8 `
  --learning-rate 0.001
```

Compiler datasets are tied to the tokenizer and structural vocabulary.

When the tokenizer changes, compiler training data may need to be regenerated.

Dataset extraction can be run with the relevant CMake target and extraction utility.

Invalid training records are intended to remain rejection cases rather than being silently accepted as positive examples.

</details>

---

<details>
<summary><strong>Advanced: runtime-model training</strong></summary>

<br>

The runtime uses its own dataset.

Representative command:

```powershell
build\debug\felidae_vm.exe `
  --train datasets\vm\runtime-context-v1.jsonl `
  --store-model build `
  --epochs 8 `
  --learning-rate 0.001
```

The runtime model is trained independently from the compiler model.

This prevents compiler ambiguity data from being treated as runtime semantic knowledge.

</details>

---

# Release builds

A normal release build can be created with:

```bash
./build.sh --mode release
```

Release mode builds optimized binaries and creates the distribution through the project's distribution target.

On normal Linux:

```text
build/release/
```

and the generated distributable package is staged under:

```text
build/release/dist/
```

The distribution folder is generated.

It is not intended to be a manually maintained source directory.

---

## Release philosophy

A release package should come from:

```text
clean intended source revision
       ↓
Release configuration
       ↓
tests / validation
       ↓
distribution target
       ↓
staged package
       ↓
clean-machine smoke test
       ↓
release
```

Do not treat a successful compilation alone as complete release evidence.

---

# Documentation

The README is intentionally the **front page** of Felidae.

It should explain:

* what Felidae is,
* why it exists,
* what it can do,
* how to try it,
* where to learn more.

Detailed documentation lives under:

```text
docs/
```

Current documentation includes areas such as:

```text
docs/README.md

docs/about.fx
docs/basics.fx
docs/getting_started.fx
docs/language_reference.fx
docs/syntax.fx

docs/facts.fx
docs/queries.fx
docs/probability.fx
docs/methods.fx

docs/native_modules.fx
docs/libraries.fx
docs/stdlib.fx

docs/testing.fx
docs/debugging.fx

docs/executable_ir.md
docs/expression_inventory.md
docs/compiler_form_audit.md
docs/reasoning_benchmark.md

docs/server.fx
docs/server_features.fx

docs/milestones.fx
docs/version.fx
```

There are also root-level technical references including:

```text
docs_language.md
docs_native_modules.md
docs_github_linguist.md
```

---

# Editor support

Felidae includes or links dedicated editor integrations.

Current editor-related repositories include:

| Editor             | Component                      |
| ------------------ | ------------------------------ |
| Visual Studio Code | `vs-code-extension/`           |
| IntelliJ IDEA      | `intellij-idea-extension/`     |
| Vim                | `vim-extension/`               |
| Emacs              | `emacs-extension/`             |
| Zed                | `zed-extension/`               |
| Sublime Text       | `sublime-text-extension/`      |
| Notepad++          | `notepad-plus-plus-extension/` |
| Nano               | `nano-extension/`              |

Felidae also contains:

```text
tree-sitter-felidae/
```

for Tree-sitter language support.

Because syntax can evolve during beta, editor integrations may occasionally lag behind the compiler.

Compiler behavior remains authoritative.

---

# Development principles

Felidae is guided by several architectural principles.

---

## 1. Keep simple things simple

Normal arithmetic should be arithmetic.

```text
2 + 2
```

should not require a neural model.

---

## 2. Keep deterministic work deterministic

If the runtime can calculate an exact result directly, it should normally do so.

---

## 3. Make reasoning explicit

Reasoning should be visible in the language/runtime model rather than hidden inside unrelated application code.

---

## 4. Represent knowledge directly

Facts and relationships should be first-class concepts.

---

## 5. Preserve graded information

If something is naturally a degree, Felidae should not force it into a boolean too early.

---

## 6. Separate compilation from execution

The compiler understands source.

The VM executes verified programs.

---

## 7. Validate learned behavior

A model output should not automatically become an executable instruction or trusted result.

---

## 8. Keep learned components focused

Felidae does not aim to make the whole runtime probabilistic.

Learning is used only at explicit boundaries.

---

## 9. Prefer inspectable systems

Reasoning behavior should be testable and measurable.

---

## 10. Benchmark claims carefully

Different qualities should be measured separately.

For example:

```text
correctness
determinism
proof success
failure behavior
learning quality
latency
```

should not be merged into one impressive-looking but meaningless score.

---

# Project maturity

Felidae currently contains several different maturity levels.

### Established project foundations

Areas with significant existing implementation include:

* C++ compiler infrastructure,
* `.fx` source compilation,
* verified binary execution,
* register-based Form VM,
* facts,
* hierarchy,
* procedures,
* arrays/maps,
* degree operations,
* numeric operations,
* example programs,
* tests,
* editor integrations.

### Active beta areas

Areas that continue to evolve include:

* binary format,
* advanced fact reasoning,
* mixfix coverage,
* recurrent-model integration,
* runtime semantic evaluation,
* training datasets,
* platform packaging,
* performance stabilization.

### Experimental areas

Some examples intentionally explore future-facing behavior.

An example existing in the repository does not necessarily mean that its API is stable.

Always check the current documentation before building long-term integrations against a beta feature.

---

# What Felidae is not

Felidae is **not currently**:

### A replacement for C++, Rust, Java, Python, or JavaScript

General-purpose languages remain better choices for many ordinary applications.

### A general-purpose LLM

Felidae's learned components are constrained and task-specific.

### A chatbot framework

Conversational interaction is not the central language design goal.

### An excuse to make every operation fuzzy

Crisp deterministic logic stays crisp.

### A system where machine learning controls every instruction

Ordinary runtime execution remains deterministic.

### Production-certified decision software

Felidae is still beta.

---

# Security

Please read:

[SECURITY.md](./SECURITY.md)

before using Felidae in an important environment.

Current beta guidance includes:

* production use is not recommended,
* important outputs should be independently verified,
* older beta versions may not receive fixes,
* vulnerabilities should be reported privately.

Do **not** open a public issue containing security-sensitive vulnerability details.

Security reports can be sent to:

```text
info@xnovity.com
```

or:

```text
support@xnovity.com
```

---

# Contributing

Felidae welcomes contributions involving:

* compiler development,
* VM development,
* language design,
* fact reasoning,
* degree/fuzzy functionality,
* tests,
* documentation,
* examples,
* performance,
* code quality,
* security,
* portability,
* editor integrations,
* Tree-sitter support,
* datasets,
* development tooling.

Before contributing, read:

[CONTRIBUTING.md](./CONTRIBUTING.md)

and:

[CODE_OF_CONDUCT.md](./CODE_OF_CONDUCT.md)

---

## A good first contribution

Useful first contributions include:

* fixing a reproducible bug,
* improving an existing example,
* adding a regression test,
* correcting documentation,
* improving an error message,
* validating a platform build,
* reducing a compiler warning,
* improving editor syntax coverage.

Large architectural changes should normally begin with an issue or discussion.

---

# Frequently asked questions

## Is Felidae a programming language?

Yes.

Felidae has source files, syntax, functions, data structures, a compiler, executable binaries, and a VM.

Its focus on facts and reasoning differentiates it from conventional general-purpose languages.

---

## Is Felidae an AI model?

No.

Felidae is a programming language and runtime.

It contains optional learned components for specific compiler/runtime tasks, but those components are not the entire system.

---

## Does Felidae require machine learning to run programs?

No.

Normal deterministic programs should execute without semantic model involvement.

---

## Is fuzzy logic used everywhere?

No.

Felidae supports graded values where they are useful.

Normal boolean conditions remain boolean.

---

## Can Felidae work with facts?

Yes.

Facts, fact types, hierarchy, filtering, and traversal are central project areas.

---

## Can one fact type inherit from another?

The language currently includes hierarchical fact/type relationships using forms such as:

```felidae
Employee extend Person(...)
```

---

## Can Felidae perform ordinary calculations?

Yes.

Felidae includes normal arithmetic and deterministic numeric operations.

---

## Does Felidae support recursion?

Current examples include recursive procedures such as factorial.

---

## Does Felidae support arrays and maps?

Yes.

Current examples exercise arrays and structured maps.

---

## Does Felidae support custom operators?

The repository contains multiple examples of custom and overloaded operator behavior.

---

## What is mixfix?

Mixfix allows an expression to use a custom arrangement of words/operators instead of always looking like:

```text
function(a, b)
```

Felidae uses this to experiment with more domain-oriented expression forms.

---

## Why use SentencePiece in a programming language?

Felidae's architecture explores representing language input as stable integer IDs before parsing and compilation.

SentencePiece provides that token-to-integer layer.

The VM then works with compiled binary structures rather than source strings.

---

## Does the VM contain the parser?

No.

The source parser belongs to the compiler side.

The Form VM executes compiled programs.

---

## Why use a VM?

The VM provides a dedicated execution environment for Felidae programs.

It also creates a clear boundary between:

```text
language compilation
```

and:

```text
program execution
```

---

## What is Form VM?

Form VM is Felidae's register-based execution runtime.

It runs verified compiled Felidae binaries.

---

## Is Felidae interpreted?

The primary supported path is compiled:

```text
.fx
 ↓
compiler
 ↓
.bin
 ↓
VM
```

---

## Does Felidae use Python for training?

The current project training path is implemented around C++ and LibTorch.

Python is not required for the normal current training/export/inference path.

---

## Can I disable LibTorch?

Certain target configurations can disable LibTorch.

However tensor and model-dependent functionality will then be unavailable and should fail clearly when requested.

---

## Does Felidae support Windows?

Yes, native CMake/Visual Studio style builds are part of the current development path.

---

## Does Felidae support Linux?

Yes.

Linux is a primary development environment.

---

## Does Felidae support macOS?

The build script includes macOS x86-64 and ARM64 handling.

---

## Does Felidae support Android?

An Android NDK cross-compilation path exists, but Android should currently be regarded as an experimental/non-primary target.

---

## Is Felidae production ready?

Not yet.

Current releases are beta.

---

## Can I use Felidae for critical decisions?

Not as the sole decision source during beta.

Important outputs should be independently verified.

---

## Are beta binaries guaranteed to remain compatible?

No.

Recompile `.fx` source when the beta executable format changes.

---

## Where should I start?

For a developer:

```text
1. Clone recursively
2. Build in test mode
3. Run form_core_concepts.fx
4. Explore degree_profiles.fx
5. Explore hierarchical_fact_filter.fx
6. Read docs/
```

---

# Development workflow

A healthy development cycle looks like:

```text
change
  ↓
build
  ↓
unit tests
  ↓
example tests
  ↓
static analysis where relevant
  ↓
review
  ↓
release validation
```

Before committing:

```bash
git status --short
```

Build outputs should remain inside their build directories.

Routine builds should not unexpectedly rewrite:

* tokenizer models,
* generated tokenizer IDs,
* training datasets,
* release artifacts.

Those are explicit maintainer operations.

---

# Dependency updates

Several dependencies are Git submodules.

After intentionally updating them:

```bash
git submodule sync --recursive
git submodule update --init --recursive
git submodule status --recursive
```

A clean recursive clone should also succeed:

```bash
git clone \
  --recurse-submodules \
  https://github.com/xnvtserver/Felidae.git
```

Dependency updates should be reviewed carefully because C++ dependency API or ABI changes can affect the compiler and VM.

---

# Performance

Felidae is written in C++ partly to keep close control over:

* memory,
* execution cost,
* runtime boundaries,
* compilation,
* tensor operations.

However:

> Performance claims should be measured on real Release builds.

Debug performance should not be used as evidence of production speed.

Training throughput and runtime latency should also be measured independently.

---

# Reasoning quality

Reasoning systems are easy to evaluate poorly.

Felidae therefore aims to separate measurements such as:

```text
Does the proof succeed?

Does an invalid proof fail safely?

Is the result deterministic?

How long did execution take?

Did a learned model generalize?

Does the model simply memorize a dataset?
```

These are different questions.

A single "accuracy" number can hide serious problems.

The reasoning benchmark exists to keep those concerns visible.

---

# Project direction

Felidae is exploring a model where software can combine:

```text
structured knowledge
      +
deterministic computation
      +
hierarchy
      +
graded values
      +
reasoning
      +
carefully bounded learning
```

without making the entire system opaque.

The long-term value of Felidae depends less on adding fashionable AI features and more on maintaining a clear answer to:

> **Why did the system produce this result, and which part of the system was responsible?**

That principle influences the separation between:

* compiler and VM,
* deterministic and learned work,
* facts and presentation,
* training and production artifacts,
* beta experiments and supported behavior.

---

# Design philosophy

Felidae is built around a simple belief:

> Not every intelligent system needs to be a giant black box.

Some problems benefit from explicit:

* facts,
* types,
* rules,
* degrees,
* relationships,
* deterministic calculations.

Other problems benefit from learned behavior.

Felidae experiments with putting both in the same system while keeping the boundary visible.

```text
Know what can be calculated.

Represent what is known.

Learn only where learning adds value.

Validate what learning produces.
```

---

# Community

Felidae is developed as an open-source project.

We welcome:

* bug reports,
* technical discussions,
* design criticism,
* documentation improvements,
* benchmarks,
* examples,
* code contributions,
* platform testing.

Please keep discussion respectful and constructive.

See:

[CODE_OF_CONDUCT.md](./CODE_OF_CONDUCT.md)

---

# License

Felidae is distributed under the **MIT License**.

See:

[LICENCE](./LICENCE)

for the complete license text.

---

# Project

Felidae is developed by **Xnovity Softwares** together with project contributors and supporters.

### Project website

https://felidae.xnovity.com

### Source repository

https://github.com/xnvtserver/Felidae

### Issues

https://github.com/xnvtserver/Felidae/issues

### Documentation

[docs/](./docs)

### Examples

[v2_examples/](./v2_examples)

---

<div align="center">

<br>

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="https://raw.githubusercontent.com/vishalkrishnaag/vs-code-extension/master/icons/fx-dark.svg">
  <source media="(prefers-color-scheme: light)" srcset="https://raw.githubusercontent.com/vishalkrishnaag/vs-code-extension/master/icons/fx-light.svg">
  <img alt="Felidae" src="https://raw.githubusercontent.com/vishalkrishnaag/vs-code-extension/master/icons/fx-light.svg" width="64">
</picture>

### Build software that can work with more than just `true` and `false`.

**Facts. Relationships. Degrees. Reasoning.**

<br>

If Felidae's direction interests you:

⭐ **Star the repository**

🧪 **Try the examples**

📚 **Explore the documentation**

🐛 **Report reproducible problems**

🤝 **Contribute**

<br>

**Felidae — reasoning as part of the language.**

</div>
