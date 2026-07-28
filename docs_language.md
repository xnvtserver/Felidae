# Felidae Logic Programming Language Notes

Felidae is a functional logic language for typed facts, explicit dataflow, and
native stdlib calls. This document describes the current ("v2") grammar and
runtime behavior, verified against the interpreter in `src/`. The reference
programs for this grammar live in `v2_examples/`. The `examples/` directory
still contains legacy programs written for the older dot-terminated grammar
and is being migrated separately; don't copy syntax from it.

## Statement Termination

Statements end at a newline, not at a trailing `.`:

```Felidae
import "math"

Person(name: "Alice", role: "Engineer")
Person(name: "Bob", role: "Manager")

Greeting(name: string) =>
    return (message: name)
```

`.` still means two things, both scoped to a single line:

```Felidae
pi := 3.01                 # decimal point
result := a.b.c            # member access, only valid when target and field share a line
```

A trailing `.` at the end of a fact, rule, import, or global binding is no
longer valid syntax and raises a parse error (`Expected a newline after
fact/rule ..., found .`). Blank lines between statements are fine.

## Goal Separators

Inside a rule or method body, goals can be separated by a comma, a newline,
or both — they're interchangeable:

```Felidae
HotAdd(value: int) =>
    doubled := value + value,
    return (result: doubled)

HotAddNoCommas(value: int) =>
    doubled := value + value
    return (result: doubled)
```

Existing comma-separated code keeps working; commas are simply optional now.

## Memory Model

Facts and rules are stored by predicate name:

```text
predicate name -> list of ClauseStmt facts/rules
```

Imported `.fx` statements are parsed and registered incrementally and remain
available for the interpreter lifetime; modules are not evicted and reparsed.
Native package libraries remain demand-loaded at the first native method call.
The runtime also keeps hash maps for predicate-to-clause lookup, compatible
fact indexes, and bounded repeated-query results. These caches are invalidated
whenever new program state is registered.

Variables bound with `:=` are immutable and single-assignment: a name already
declared as a rule-head field or a prior `:=` cannot be rebound in the same
scope. Arrays are intended for bounded, in-memory collections; use a
linked-list style structure (see `core/list.fx` and the List/ListItem facts it
declares) for larger logical data sets so the C++ runtime can back storage with
vector chunks without growing one large mutable array.

## Imports

```felidae
import "file.fx"
import "directory"
import "directory/*"
import ("one.fx", "two.fx")
import "math"
import ("file", "math", "ml", "db", "probability")
import "system" # optional; system is available automatically
```

The VS Code extension provides document links for import strings.
Bare library imports resolve to declaration files under `core/`. These files
contain only native method heads such as `math.abs(value: number) => ()`; the
actual body is the matching C++ builtin implementation. Imports are resolved
lazily: the declaration file is only parsed and loaded the first time
something in it is actually needed, so an unused `import` never pays the cost
(or surfaces a parse error in) of the module it names.
`system` is auto-imported by the interpreter, so `system.print(value: data)` can
be called even without an explicit `import "system"`. A handful of pure
expression builtins (`count`, `sum`, `average`, `sort`, `search`, `contains`,
`lower`, `upper`, `length`, `type`, `instanceof`, and basic `math.*` arithmetic)
are always available without any import, because they're dispatched through a
fixed builtin table rather than a declared `core/*.fx` signature. Import the
owning module explicitly anyway for clarity and portability.

## Running Programs

Felidae supports three execution modes:

```powershell
build\felidae.exe program.fx
build\felidae.exe program.fx '? Query(name: x)'
build\felidae.exe --repl program.fx
```

Direct execution calls `main(...)` when present. `main` may declare zero
arguments (`main() =>`) or accept `arguments: system.stdin` to read CLI args:

```Felidae
main(arguments: system.stdin) =>
    return (args: arguments.args)
```

The `system.stdin` object currently contains `args` and deterministic empty
`text`. If no `main` method exists, the program loads successfully and prints
a helpful message.

## Module And Field Syntax

Fact rows may be declared directly and should use explicit field names so the
stored data shape is clear:

```Felidae
Person(
    name: "Default",
    age: 0,
    country: "India"
)
```

Rule and method heads may use named fields, typed fields, or positional
parameters depending on the contract:

```Felidae
ArrayLiteralTest(input: value) =>
    array:get(data: [1, 2, 3, 4], position: 2, access: value)
```

Inline fact values are also valid and evaluate to typed map values:

```Felidae
Artists() =>
    return (result: Person(name: "Ramesh", age: 20))
```

Direct fact declarations such as `Employee(name: "Alice")` are supported.
Named fact goals such as `Employee(name: name)` are also supported. A single
positional fact goal inside a method body, such as `Employee(e)`, is not an
iterator and is rejected because Felidae does not implicitly scan a fact type
as though it were a list. Use `lambda(Employee, e => ...)` for explicit fact
iteration, or read from an explicit array/list.

Facts may repeat a named field to represent multi-valued data. At runtime the
field is materialized as an array for method-body access, and direct fact
queries enumerate each repeated value:

```Felidae
Cat1(name: "kitten", name: "tiger", name: "lilly")
```

Use `.` for top-level package/module calls:

```Felidae
proofs := provenance.BuildFromRecording(rec: rec, store: store, goal: goal, options: {})
```

Use `.` for map/object field access. Use `:` for named arguments and local namespaces:

```Felidae
Employee(name: "Alice")
x == a.z.w
array:get(data: [1, 2, 3], position: 0, access: value)
```

`::` is not supported. Use `.` for top-level package/module calls.

## Logic Operators

`,` is conjunction, same as a plain newline between goals. It means AND and
every goal in the sequence must hold:

```Felidae
EngineerInSEA(employee: e, name: Name) =>
    Name == e.name
    e.role == "Engineer"
    e.office == "SEA"
```

`|` is disjunction. It means OR between goal branches:

```Felidae
TechnicalOrManager(name: name) =>
    Employee(name: name, role: "Engineer") |
    Employee(name: name, role: "Manager")
```

Parentheses isolate complex goal expressions:

```Felidae
TechnicalArchitectManager(name: name) =>
    (Employee(name: name, role: "Engineer") |
    Employee(name: name, role: "Architect")),
    Employee(name: name, role: "Manager")
```

## Method-Style Rules

Typed method-style rule heads use the field name as the local input variable
and the field value as the accepted fact/type:

```Felidae
isAdult(input: Person) =>
    p := input
    where p.age >= 18
    return (
        name: p.name
    )
```

Facts can extend a base fact/type. Child fields override parent fields:

```Felidae
Person(name: "Default", age: 0, country: "India")
Employee extend Person(name: "Ravi", age: 30, role: "Engineer")
```

## Contextual Fact Comparison

`Relation.compare(left: ..., right: ...)` is a built-in fact-runtime operation;
ordinary in-memory expert-system programs do not import `db.fx` for it. It
returns a structured `Comparison` fact rather than a Boolean. The left fact
provides contextual membership knowledge and the right fact family owns the
rule that interprets that knowledge.

```Felidae
Dog.membership(input: Dog, against: Animal) =>
    return {living: input.living, breathes: input.breathes}

Animal.compareMembership(context: Fact) =>
    return {
        state: "animal-member",
        evidence: context.structuralEvidence
    }

result := Relation.compare(left: dog, right: animal)
where result.state == "animal-member"
```

Dispatch is directional and resolves one method only: the most-specific source
`membership` method compatible with the target, followed by the target's exact
or inherited `compareMembership` method on a concrete target family. If no
target-owned method applies, the runtime produces a resolved-field micro-fact
and structural result (`exact-member`, `subset-member`, `partial-member`,
`conflicting`, or `unknown`). A membership method must return a map/fact or
`nil`; `nil` becomes an explicit `incomparable` comparison result.

Use `.depends(on: Fact(...))` for hard existential requirements and
`.relate(to: Fact(...), as: Relationship(...), degree: ..., confidence: ...)`
for generic relationship evidence. Dependencies are AND requirements for a
stored fact and produce an `unresolved` comparison result when absent.
Relationship names, degree combination, and domain judgment remain data and
target-family rule decisions; the runtime does not impose similarity meaning.
`Relation.find(input: context.relationships, name: "...")` returns a direct
relationship fact or `nil`, and `Dependency.satisfied(input: fact)` returns an
explicit Boolean for guards.

### Dynamic method references

`.references(by: owner::method, factor: ..., as: Reference(...))` attaches a
pure executable derivation to one stored fact. It is neither a hard dependency
nor a semantic relationship. Evaluate it explicitly with
`Fact.references(input: fact)`; this returns ordered `ReferenceResult` values
whose `result` field is a typed fact. A temporary `factor:` supplied to
`Fact.references` is an ephemeral override and never replaces the attachment's
default-factor canonical result.

Referenced methods must declare compatible typed `input` and `factor`
parameters, return `ReferenceResult(result: TypedFact(...))`, and use only
pure operations (typed exceptions may propagate). Reference results are kept
outside ordinary fact lookup, unification, dependencies, and
`Relation.compare`; user rules must retrieve and consume them deliberately.

```logic
motion.references(
    by: Physics::velocity,
    factor: Time(seconds: 2.0),
    as: Reference(name: "velocity")
)
velocity := Fact.references(input: motion, as: Reference(name: "velocity"))
```

The callable must be declared or imported before the attachment. Constructors,
normal rule matching, Celidae, and debugger inspection never evaluate
references.

Comparison values can be passed through `then` and compared again. The built-in
core `Comparison.membership` method provides the standard `previous*`
micro-fact projection and returns `nil` for `unresolved` or `incomparable`
results. A domain may declare its own normal `Comparison.membership` method
before execution when it needs a different chained projection. Comparison
facts, maps, arrays, strings, and numbers are not implicitly Boolean: use an
explicit field comparison in `where` or `if`.

Use `type(value: item, name: TypeName)` to read a value's concrete type, and
`instanceof(value: item, type: Person)` to check whether a value is an instance
of a fact/type or one of its parents through `extend`. Both are goal-style
builtins meant to be used as a guard/condition in a rule body, not assigned
directly with `:=`:

```Felidae
CheckPerson(input: any) =>
    instanceof(value: input, type: Person)
    return (ok: true)
else
    return (ok: false)
```

`lambda(Type, item => condition)` filters facts of a type, and
`lambda(sourceArray, item => expression)` maps arrays:

```Felidae
Adults := lambda(Person, p => isAdult(input: p))
Names := lambda(Person, p => p.name)
```

Method calls do not implicitly iterate over compatible facts. A method processes
one provided input at a time, and `celidae --check-json` reports this before
run/debug execution:

```Felidae
isAdult(input: Person) =>
    where input.age >= 18
    return (
        name: input.name
    )
```

`isAdult(name: name)` does not scan all `Person` facts. Likewise,
`Person(p)` in a method body does not iterate over `Person` facts. Use lambda
when you want iteration:

```Felidae
Adults := lambda(Person, p => isAdult(input: p))
```

### Returning A Value

A method's `return` goal accepts a tuple of named fields, a single bare
expression, `nil`, or nothing at all:

```Felidae
WithFields(x: number) =>
    return (x: x, doubled: x * 2)

WithExpression(x: number) =>
    return x + 1

WithNil() =>
    return nil

WithNoValue() =>
    system.print(value: "done")
    return
```

### Ordered Fallback Branches

Method-style rules may use ordered fallback `else` branches. This is not
procedural `if/else`; each branch is tried in order, and the first branch that
returns a result wins. Later branches are skipped. `where` is optional because a
plain comparison goal can also act as the branch guard:

```Felidae
RoleAccess(input: Employee) =>
    e := input
    e.role == "Engineer"
    return (
        name: e.name,
        access: "engineering"
    )
else
    e.role == "Manager"
    return (
        name: e.name,
        access: "management"
    )
else
    return (
        name: e.name,
        access: "default"
    )
```

### if/else

`if` also works as an inline conditional goal, comparing an expression directly
and branching without a separate `where`:

```Felidae
main() =>
    x := 10
    if x == 10
        return (ok: true)
    else
        return (ok: false)
```

### Calling Conventions

A method or native module call can be used two ways. The safest and most
uniform is direct assignment, capturing the whole return tuple (or the single
returned value, for a bare `return expr`):

```Felidae
s := math.sin(value: 0)
result := HotAdd(value: 1)
```

User-defined predicates (and native module calls resolved by name rather than
by a fixed builtin token) also accept an output-binding convenience: add a
named argument whose name matches a field of the callee's `return` tuple and
whose value is a fresh, not-yet-declared variable. That variable is bound to
the matching field after the call:

```Felidae
HotAdd(value: int) =>
    doubled := value + value
    return (result: doubled)

main() =>
    HotAdd(value: 1, result: a)
    return (result: a)
```

Note that this only works when the return-field name is *not* also declared as
a head parameter of the callee — a head parameter and a same-named `:=` target
inside the body would collide, since head parameters are already bound at call
time. Keep output-only fields out of the head parameter list, as `HotAdd`
does above.

A small set of builtins with a fixed dispatch token (`array:get`, `str:*`,
`json:*`, and similar fast-path natives — see `src/BuiltinRegistry.cpp` for the
full list) require the output variable to already be declared (typically as a
head parameter of the enclosing rule) before it's used this way. The most
portable pattern for these is to wrap the call in a small predicate and invoke
it through a query, or to prefer the plain `:=` assignment form documented
above wherever the native also supports it (most of `math.*`, `str.*`, `csv.*`,
`json.*`, `ml.*`, `http.*`, and `process.*` do).

## Anonymous Variables

`_` is anonymous and is not printed:

```Felidae
Employee(name: "Alice", role: "Engineer", office: "SEA")
Employee(name: "Bob", role: "Manager", office: "LAX")

# Query:
? Employee(name: Name, role: _, office: "SEA")
```

prints only `Name`.

## Tuple Destructuring

Tuple-returning methods can be destructured into immutable locals:

```Felidae
a, b, c := someMethod()
a: string, b: bool, c: float := someMethod()
```

The right-hand side is evaluated once and must return a genuine tuple/array
value with the same number of values as the target list — an array literal
(`return [x, y]`), or `fn:tuple(...)` / `fn:pair(...)`. A method's named-field
`return (a: 1, b: 2)` produces a map, not a positional tuple, and is not a
valid right-hand side for this form; use `array1 := someMethod()` and access
fields with `.` instead. Optional target types support builtin types such as
`string`, `bool`, `number`, `int`, `float`, and `array`. A type mismatch raises
a `ProgrammingError` instead of silently binding the wrong value.

## Arrays

Both forms are accepted:

```Felidae
array:get(data: [1, 2, 3, 4], position: 2, access: value)
array1 := fn:array(data: [1, 2, 3, 4])
```

`array:get`'s `access:` output binding follows the fixed-builtin rule above:
`value` must already be declared (for example as a rule-head field) before
this goal runs. `fn:array` is a plain `:=` assignment and always works.

## Exceptions

Felidae does not use `try`/`catch`. Recoverable operations return the standard
`Result` value from `core/exception.fx` instead: `{ok, value, error}`. A caller
checks `ok` and passes a failed result to an ordinary source-owned method when
it wants recovery. `core/exception.fx` defines only the `Exception` and
`Result` shapes plus `exception.ok`, `exception.failure`, and
`exception.from`; application error kinds remain in application source.

```Felidae
import "exception"

CalculatorActions.handle(result: Result) =>
    if result.error.kind == "DivisionByZero" then
        return exception.ok(value: {quotient: 0, recovered: true})
    else
        return result

SafeDivide(dividend: number, divisor: number) =>
    if divisor == 0 then
        rejected := exception.failure(
            kind: "DivisionByZero",
            message: "Cannot divide by zero",
            source: "calculator"
        )
        return CalculatorActions.handle(result: rejected)
    else
        return exception.ok(value: math.div(lhs: dividend, rhs: divisor))
```

Use `throw(exception: ..., target: Owner::method)` only for an unrecoverable
path that must immediately stop the current method. The target is one exact
callable reference, never a string. `kind` is the stable programmatic
discriminator while `message` remains user-facing context.

## Built-In Expression Functions

The runtime supports simple expression builtins for direct execution, REPL, and
method bodies:

```Felidae
count(Adults)
average(lambda(Person, p => p.age))
sort(lambda(Person, p => p.name))
search(lambda(Person, p => p.name), "Ra")
contains("Ravi", "Ra")
lower("HELLO")
upper("hello")
length("hello")
ParseDoc("Primary")
```

These expression builtins are auto-available from the runtime and documented in
`core/prelude.fx`. Do not model them as empty Felidae methods, because that
would shadow the runtime expression builtin during import.

Native standard-library calls are dispatched to C++ implementations, keeping
heavy work out of the interpreter loop:

```felidae
import ("file", "math", "ml")

file.readFile(path: "data.txt")
file.readLines(path: "data.txt")
file.writeFile(path: "data.txt", data: "hello", mode: "write")
file.writeFile(path: "data.txt", data: "hello", mode: "append")
file.exists(path: "data.txt")
csv.parse(data: "student,class\nJohn,10c")
csv.toFacts(data: "student,class\nJohn,10c", type: "School")
csv.toText(data: rows)
factText := csv.toFelidaeFacts(data: filteredRows, type: "School")
file.writeFile(path: "converted_csv_school.fx", data: factText, mode: "write")
console.writeLine(value: "hello")
system.print(value: "hello")
name := "Felidae"
system.printf("Hello {name}\n")
math.sqrt(value: 81)
math.pow(base: 2, exponent: 8)
probability.mean(data: [2, 4, 6, 8])
probability.binomialPmf(trials: 10, successes: 3, p: 0.5)
ml.dot(left: [1, 2, 3], right: [4, 5, 6])
ml.meanSquaredError(left: [1, 2, 3], right: [1, 2, 5])
http.get(url: "https://example.com")
http.post(url: "http://127.0.0.1:8080/", body: "hello")
http.put(url: "http://127.0.0.1:8080/", body: "hello")
http.delete(url: "http://127.0.0.1:8080/")
http.serveStatic(host: "127.0.0.1", port: 8080, response: "Hello World")
process.platform()
process.exec(command: "echo hello")
process.sleep(milliseconds: 800)
```

HTTP client/server functions use the vendored header-only cpp-httplib module.
HTTPS client calls require an OpenSSL-enabled build of cpp-httplib; otherwise
the runtime raises a clear error. `examples/web_server.fx` starts a blocking
static-response server for GET, POST, PUT, and DELETE `/` routes.
`process.fx` is separate from `console.fx`: console handles stdin/stdout, while
process handles explicit trusted test commands, OS detection, and sleeps.
`csv.parse`, `csv.toFacts`, and `csv.toText` use the vendored header-only
rapidcsv parser/writer. `csv.toFacts` adds a `__type` field so rows can behave
like typed fact values during explicit lambda processing. Use Felidae code to
filter/project rows, `csv.toFelidaeFacts` to create declaration text, and
`file.writeFile` to persist declarations such as
`School(student: "John", class: "10c")` for later import or querying.
The interpreter itself is the in-memory OLAP fact store. Import ordinary `.fx`
fact sources and rule libraries as needed, then use named fact goals,
unification, inheritance-aware methods, and `lambda(FactType, ...)` for normal
fact reasoning; use `lambda("model", ...)` when a dynamically named or
lowercase `.fx` model must be selected. None of these require importing
`db.fx`. Facts are built-in
language values: use typed goals and `lambda(FactType, ...)` to query and
filter them, then normal array operations when one selected value is needed.
`Fact.select(...)` remains the low-level lazy-selection API for code that
specifically needs a reusable cursor. Import `db` only for explicit `.fx`
fact-file connection, mutation, save, and reload workflows such as
`db.connect`, `db.insert`, `db.updateOne`, `db.deleteOne`, `db.save`, and
`db.sync`. It deliberately provides no collection query, filter, sort, or
aggregate API: reload with `db.sync`, then use Felidae goals, `lambda`, set,
group, and aggregate expressions against the in-memory fact runtime.
Command-line `? Fact(...)` queries remain available for ad hoc inspection.

Facts returned by `Fact.all`, `Fact.first`, or `Fact.materialize` retain an
internal stable identity for `.depends(...)` and `.relate(...)`. This identity
is not serialized and does not change structural equality. When two stored
facts have identical visible fields, retrieve the intended row through one of
these APIs before attaching knowledge; a reconstructed `Type(...)` map is
intentionally rejected as ambiguous.

`probability.fx` adds common probability and statistics helpers including
mean, variance, standard deviation, normalization, entropy, covariance,
correlation, Bernoulli, binomial, Poisson, normal, uniform, sample, and weighted
choice.
`ml.dot` and `ml.meanSquaredError` use Eigen when it is available at build time,
with a portable C++ fallback for MVP builds.

`thread.fx` declares the cooperative concurrency API. `thread.createThread`,
`thread.start`, `thread.status`, and `thread.result` run methods on immutable
interpreter snapshots. `thread.pause` and `thread.stop` intentionally report an
unsupported-operation error until cancellation semantics are implemented safely.

`list.fx` declares a linked-list-style structure over facts (`List`,
`ListItem`) with `list.get`, `list.first`, and `list.pop` helpers, for
bounded, explicit iteration over ordered data without growing one large
mutable array.

Native libraries and runtime support should stay behind module boundaries
instead of growing `Interpreter.cpp`. `Memory.cpp` owns fact/type storage and
compatible-fact caching, `Env.cpp` keeps the environment type boundary explicit,
and CSV/HTTP support lives under `native_modules/` so the interpreter only
dispatches to module interfaces. If native modules become dynamic later, the
loader must map platform extensions explicitly
(`.dll` on Windows, `.so` on Linux, `.dylib` on macOS) rather than assuming one
operating system.

`felidae.exe` is the optimized execution runtime and must not link AST analysis
or editor diagnostics. `celidae.exe` is the debugger, analytics, and
visualization product: it owns AST traversal, diagnostics, debugger analytics,
fact-database profiling, and optimization suggestions. Editor extensions call
`celidae.exe --check-json` on file changes and use `felidae.exe` only for normal
Run/Query execution. `celidae.exe --lsp` starts the native JSON-RPC language
server for clients that want a long-lived diagnostics process.
`felidae_debug.exe` is kept as a legacy compatibility name.

Celidae enables an in-memory AST cache for repeated diagnostics, debugger, and
visualization requests. Cached file ASTs are keyed by normalized path, file size,
and modification time, so unchanged imports can be reused across repeated
checks. Unsaved LSP document text is cached by URI, length, and content hash.
The normal `felidae.exe` interpreter keeps this cache disabled for now; batch
data-analysis optimization should layer query/fact indexes and visualization
profile caches on top of the runtime once profiling identifies the hot paths.
Source and file reads use a pre-sized read for normal files and a chunked
streaming read for files larger than 10 MB.

## Visual Data Analysis

Felidae is useful for log-shaped data because rules can clean, filter, project,
and query records before visualization. Use `lambda(...)`, named fact queries,
CSV/JSON helpers, and method-style guards to shape noisy rows into explicit
facts or globals, then open `Felidae: Visualize`.

The VS Code visualizer reads the Celidae data snapshot from
`celidae --visualize-data-json --load-imports` and builds multiple views.
Use `celidae --inspect-graph` for lightweight source/file inspection; add
`--load-imports` or use the `--visualize-data-*` commands when the analysis
scenario must include imported fact databases. `celidae --visualize-data-html --load-imports`
emits a standalone HTML visualization for sharing or quick
inspection outside the IDE:

- Graph: interactive data-flow/fact/method/library diagram with SVG export.
- Profile: charts and counts for node kinds and relationship labels.
- Quality: warnings for isolated data, duplicate labels, sparse metadata,
  unlabeled relationships, and high fan-out hubs common in noisy logs.
- Data Table: searchable runtime entities with degree counts and quality
  signals.

This keeps analysis tied to the same runtime data contract used by queries and
debugging instead of relying on a separate editor-only parser.

Felidae programs can create the same visualization artifacts programmatically
through interpreter built-ins:

```felidae
graphJson := visualize.dataJson(loadImports: "true")
html := visualize.dataHtml(loadImports: "true")
file.writeFile(path: "build/report.html", data: html, mode: "write")
```

The `felidae.exe` interpreter also accepts `--visualize-data-json` and
`--visualize-data-html` for scripted use. Both interpreter and Celidae routes use
the same runtime graph and HTML renderer, so generated output stays consistent.
