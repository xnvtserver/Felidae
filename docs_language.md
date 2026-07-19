# Felidae Logic Programming Language Notes

Felidae is a functional logic language for typed facts, explicit dataflow, and
native stdlib calls.

## Memory Model

Facts and rules are stored by predicate name:

```text
predicate name -> list of ClauseStmt facts/rules
```

Lazy modules are tracked separately with source-file ownership so cold module clauses can be evicted.
The runtime also keeps hash maps for predicate-to-clause lookup, compatible fact
indexes, and repeated query results. These caches are invalidated whenever new
program state is loaded.

Variables bound with `:=` are immutable. Arrays are intended for bounded,
in-memory collections; use a linked-list style structure for larger logical data
sets so the C++ runtime can back storage with vector chunks without growing one
large mutable array.

Tuple-returning methods can be destructured into immutable locals:

```Felidae
a, b, c := someMethod().
a: string, b: bool, c: float := someMethod().
```

The right-hand side is evaluated once and must return a tuple or array with the
same number of values as the target list. Optional target types support builtin
types such as `string`, `bool`, `number`, `int`, `float`, and `array`. A type
mismatch raises a `ProgrammingError` instead of silently binding the wrong value.

## Imports

```felidae
import "file.fx".
import "directory".
import "directory/*".
import ("one.fx", "two.fx").
import "math".
import ("file", "math", "ml", "db", "probability").
import "system". # optional; system is available automatically
```

The VS Code extension provides document links for import strings.
Bare library imports resolve to declaration files under `core/`. These files
contain only native method heads such as `math.abs(value: number) => ().`; the
actual body is the matching C++ builtin implementation.
`system` is auto-imported by the interpreter, so `system.print(value: data)` can
be called even without an explicit `import "system".`.

## Running Programs

Felidae supports three execution modes:

```powershell
build\felidae.exe program.fx
build\felidae.exe program.fx '? Query(name: x)'
build\felidae.exe --repl program.fx
```

Direct execution calls `main(arguments: system.stdin)` when present. The
`system.stdin` object currently contains `args` and deterministic empty `text`.
If no `main` method exists, the program loads successfully and prints a helpful
message.

## Module And Field Syntax

Fact rows may be declared directly and should use explicit field names so the
stored data shape is clear:

```Felidae
Person(
    name: "Default",
    age: 0,
    country: "India"
).
```

Rule and method heads may use named fields, typed fields, or positional
parameters depending on the contract:

```Felidae
ArrayLiteralTest(input: value) =>
    array:get(data: [1, 2, 3, 4], position: 2, access: value).
```

Inline fact values are also valid and evaluate to typed map values:

```Felidae
Artists() =>
    return (result: Person(name: "Ramesh", age: 20)).
```

Direct fact declarations such as `Employee(name: "Alice").` are supported.
Named fact goals such as `Employee(name: name)` are also supported. A single
positional fact goal inside a method body, such as `Employee(e)`, is not an
iterator and is rejected because Felidae does not implicitly scan a fact type
as though it were a list. Use `lambda(Employee, e => ...)` for explicit fact
iteration, or read from an explicit array/list.

Facts may repeat a named field to represent multi-valued data. At runtime the
field is materialized as an array for method-body access, and direct fact
queries enumerate each repeated value:

```Felidae
Cat1(name: "kitten", name: "tiger", name: "lilly").
```

Use `.` for top-level package/module calls:

```Felidae
proofs := provenance.BuildFromRecording(rec: rec, store: store, goal: goal, options: {}).
```

Use `.` for map/object field access. Use `:` for named arguments and local namespaces:

```Felidae
Employee(name: "Alice").
x == a.z.w.
array:get(data: [1, 2, 3], position: 0, access: value).
```

`::` is not supported. Use `.` for top-level package/module calls.

## Logic Operators

`,` is conjunction. It means AND and every goal in the sequence must hold:

```Felidae
EngineerInSEA(employee: e, name: Name) =>
    Name == e.name,
    e.role == "Engineer",
    e.office == "SEA".
```

`|` is disjunction. It means OR between goal branches:

```Felidae
TechnicalOrManager(name: name) =>
    Employee(name: name, role: "Engineer") |
    Employee(name: name, role: "Manager").
```

Parentheses isolate complex goal expressions:

```Felidae
TechnicalArchitectManager(name: name) =>
    (Employee(name: name, role: "Engineer") |
    Employee(name: name, role: "Architect")),
    Employee(name: name, role: "Manager").
```

## Method-Style Rules

Typed method-style rule heads use the field name as the local input variable
and the field value as the accepted fact/type:

```Felidae
isAdult(input: Person) =>
    p := input,
    where p.age >= 18,
    return (
        name: p.name
    ).
```

Facts can extend a base fact/type. Child fields override parent fields:

```Felidae
Person(name: "Default", age: 0, country: "India").
Employee extend Person(name: "Ravi", age: 30, role: "Engineer").
```

Use `type(value: item, name: TypeName)` to read a value's concrete type, and
`instanceof(value: item, type: Person)` to check whether a value is an instance
of a fact/type or one of its parents through `extend`.

`lambda(Type, item => condition)` filters facts of a type, and
`lambda(sourceArray, item => expression)` maps arrays:

```Felidae
Adults := lambda(Person, p => isAdult(input: p)).
Names := lambda(Person, p => p.name).
```

Method calls do not implicitly iterate over compatible facts. A method processes
one provided input at a time, and `celidae --check-json` reports this before
run/debug execution:

```Felidae
isAdult(input: Person) =>
    where input.age >= 18,
    return (
        name: input.name
    ).
```

`isAdult(name: name)` does not scan all `Person` facts. Likewise,
`Person(p)` in a method body does not iterate over `Person` facts. Use lambda
when you want iteration:

```Felidae
Adults := lambda(Person, p => isAdult(input: p)).
```

Method-style rules may use ordered fallback `else` branches. This is not
procedural `if/else`; each branch is tried in order, and the first branch that
returns a result wins. Later branches are skipped. `where` is optional because a
plain comparison goal can also act as the branch guard:

```Felidae
RoleAccess(input: Employee) =>
    e := input,
    e.role == "Engineer",
    return (
        name: e.name,
        access: "engineering"
    )
else
    e.role == "Manager",
    return (
        name: e.name,
        access: "management"
    )
else
    return (
        name: e.name,
        access: "default"
    ).
```

## Anonymous Variables

`_` is anonymous and is not printed:

```Felidae
Employee(name: "Alice", role: "Engineer", office: "SEA").
Employee(name: "Bob", role: "Manager", office: "LAX").

# Query:
? Employee(name: Name, role: _, office: "SEA")
```

prints only `Name`.

## Arrays

Both forms are accepted:

```Felidae
array:get(data: [1, 2, 3, 4], position: 2, access: value).
array1 := fn:array(data: [1, 2, 3, 4]).
```

## Exceptions

`throw(msg: reason)` signals an in-language exception reason by binding `error_reason`.
Rules can then check the reason with normal comparisons:

```Felidae
DivideFailure(error_reason: error_reason) =>
    throw(msg: "DivisionByZero"),
    error_reason == "DivisionByZero".
```

`throw` can also route directly to a handler rule by name:

```Felidae
DivideFailureHandler(msg: msg) =>
    HandledFailure(type: "division", msg: msg).

RoutedFailure(msg: msg) =>
    throw(msg: "thrown from module a", target: DivideFailureHandler),
    HandledFailure(type: "division", msg: msg).
```

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
import ("file", "math", "ml").

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
math.sqrt(value: 81)
math.pow(base: 2, exponent: 8)
db.all(type: "Customer")
db.find(type: "Customer", field: "city", equals: "SEA")
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
`School(student: "John", class: "10c").` for later import or querying.
`db.all`, `db.find`, `db.count`, `db.first`, `db.types`, and `db.fields` expose
the loaded no-SQL fact store as method-body values. Command-line `? Fact(...)`
queries still use the parallel external solver path for ad hoc inspection.
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
graphJson := visualize.dataJson(loadImports: "true"),
html := visualize.dataHtml(loadImports: "true"),
file.writeFile(path: "build/report.html", data: html, mode: "write")
```

The `felidae.exe` interpreter also accepts `--visualize-data-json` and
`--visualize-data-html` for scripted use. Both interpreter and Celidae routes use
the same runtime graph and HTML renderer, so generated output stays consistent.
