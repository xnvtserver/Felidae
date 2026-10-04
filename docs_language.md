# Felidae language reference

Felidae parses `.fx` source into a `Program` AST and evaluates that AST with
the direct interpreter. A program never needs conversion to a binary file.

## Statements

Facts, rules, methods, globals, imports, and annotations are source-level AST
statements. Every declaration starts with `def`, except `class`, imports, and
builtin structural calls such as `key(...)` and `index(...)`. Functions close
with `end`; bindings and persistent facts close with `.`.

```felidae
def Person(name: "Ada", active: true).

def Greeting(name: string) =>
    return (message: "hello", name: name).
end

def main() =>
    return Greeting(name: "Ada").
end
```

`:=` is immutable single-assignment and binding declarations use
`def name := value.`. Data atoms are first-class symbolic values: bare names
in value context and raw single-quoted text are atoms, while double-quoted text
is a string. Class and function references use `Name.class` and
`name.function`. Conditions and solver truth are strictly
boolean; fuzzy degrees are ordinary numbers produced by `core/fuzzy.fx`.

`for` accepts a list, `range(...)`, fact selection, or graph selection.
`while` requires a boolean condition. `break` and `continue` are supported,
and `switch` follows Java-style fallthrough until an explicit `break`.

```felidae
for i in range(0, 10) then
    if i = 5 then
        continue.
    end
end

switch status
case "ready" then
    start().
    break.
default then
    wait().
end
```

## Exceptions

`throw(exception: {kind: "name", message: "text"})` raises an exception object.
`kind` must be a string; `message` is optional. `try ... catch e then ... end`
runs the `try` body to completion. A native runtime error (for example
`order_by` over a non-numeric field) or a thrown exception then binds
`{kind, message}` to `e` and runs the catch body; native errors have kind
`"runtime"`. Without an error the catch body is skipped and bindings made in
the `try` body stay visible. A `try` body that merely fails, with no error,
fails the statement like any other goal; only errors are caught. Store writes
made before the error are kept. An uncaught exception ends the program and
prints its message.

```felidae
def main() =>
    try
        def sorted := rows.order_by(field: "name").
    catch e then
        def reason := e.message.
    end
    return reason
end
```

Several `catch` branches form a cascade: the first handles errors from the `try`
body, and an error raised inside it goes to the next `catch` (each variable holds
the exception it received). An error from the last `catch` propagates to the
enclosing `try` or ends the program. Each catch variable is scoped to its branch.

```felidae
try
    risky().
catch e then
    repair(e.message).
catch k then
    fallback(k.message).
end
```

`try` and `catch` are reserved words.

## Classes and explicit blocks

`class` declarations are direct AST schema declarations. Their inheritance is
registered in the runtime type catalog and RocksDB, so a child fact participates in queries for its
parent type. `end` explicitly closes a class, method, or nested control block.

```felidae
class Person
    def name: string.
    index(name).
end

class Student extends Person
    def grade: number.

    def promoted() =>
        return Student(name: this.name, grade: this.grade + 1).
    end
end

def main() =>
    return Student(name: "Ada", grade: 10).
end
```

Class methods use Java-style receiver semantics. The declared parameter list
contains only source parameters; the runtime frame supplies `this` separately.
`this` cannot be declared, assigned, or referenced outside a class method.
Ordinary instance methods must be called through an object, such as
`student.promoted()`, rather than `Student.promoted()`.

Class methods may opt into replacing a built-in database member with
`@override`; a same-named method without that annotation leaves the built-in
operation authoritative.

## Facts and queries

Facts live in RocksDB, which maintains ordered type buckets, indexes,
provenance, temporal metadata, stable identities, and graph adjacency.
Queries unify their fields with records selected directly from that store.

A persistent fact seed is `def name(...).`; casing has no semantic role.
An unprefixed `name(...)` is a function or builtin call, including `Link(...)`.
Inside a rule, `def name(...)` is a fact pattern whose unknown bare field
values introduce immutable unification bindings.

If `class Person ... end` is declared,
`Person(...)` is validated for argument names, types, and arity. If no class is
declared, the constructor is not validated: any field set is accepted, as with
a lambda, and the constructor declares a class internally. The only contract an
undeclared type has is its key, the first field of its first constructor, which
every later fact of that type must supply. Declaring the class later promotes
the stored facts and checks each one against the declaration.

The direct fluent fact API is evaluated by that same store:

```felidae
def active := School.where(district: "central").AndWhere(active: 1.0).
def combined := School.where(district: "central").OrWhere(district: "west").
def first := School.where(active: 1.0).limit(records: 1).
def inserted := School.insert(values: {name: "Riverside", active: 1.0}).
def changed := School.where(name: "Riverside").update(values: {active: 0.0}).
def removed := School.where(name: "Riverside").delete().
def first_row := School.get(pos: 0).
```

Every stored fact is a graph node. `Link` stores one directed edge; `from`
and `to` resolve persisted nodes by primary key, and `properties` is an
optional immutable JSON-like map. No relationship class or foreign-key
declaration is involved.

```felidae
Link(
    from: Employee(id: "employee-1"),
    to: Department(id: "department-1"),
    properties: {kind: "works_in", since: 2024, confidence: 0.9}
).

def rows := Employee().join(
    properties: {kind: "works_in"},
    direction: forward.class
).where(right.id = "department-1").
```

Traversal directions are typed internal class references:
`forward.class`, `backward.class`, and `both.class`.

Omit `properties:` to traverse every Link for the selected nodes. Recursive
joins and shortest paths use the same optional property condition and require
explicit depth bounds. The trailing `where(...)` can compare primary-key fields
on both endpoints, for example `where(left.department_id = right.id)`.

`db.sync(path:)` atomically reloads a fact-only `.fx` source file while
preserving unchanged logical identities. Database operations are built into
the interpreter, so application source files require no library import. The
project manifest still declares `import "db".` explicitly as its storage
capability.

Every entry program requires an `init.fx` in the same directory. It imports
the builtin database capability, selects the RocksDB directory, and optionally
applies supported RocksDB settings before program loading:

```felidae
import "db".
db.location("./data/felidae_db").
db.configure(options: {max_background_jobs: 4, bytes_per_sync: 1048576}).
```

Relative locations resolve from `init.fx`. Missing, empty, duplicate, or
invalid manifests stop execution; there is no temporary database fallback.

Persistent class methods retain a fail-closed RocksDB source locator.
`fx.interpret` requires class, function, object receiver, file, and original
source line; the path, source fingerprint, schema fingerprint, runtime object
type, and method span must all match before the method executes. Additional
named arguments are forwarded to the method. `this` is an implicit receiver
binding, never a declared or passed method parameter, and is invalid outside
class methods:

```felidae
fx.interpret(
    class: Employee.class,
    function: get_data.function,
    object: employee,
    file: "employee.fx",
    line: 30
)
```

```felidae
def Animal(name: "tiger", habitat: "forest").
def Animal(name: "otter", habitat: "river").

def main() =>
    return Animal(name: "tiger").
end
```

The CLI supports an external query as its second argument:

```sh
felidae animals.fx '? Animal(name: x)'
```

## Mixfix

`@mixfix` declarations register deterministic patterns in the interpreter’s
operator registry. Pattern literals are tokenized from source and captures are
resolved as ordinary AST expressions; mixfix never uses a statistical model.

```felidae
@mixfix(pattern: "reason {subject: expr} with {evidence: string}")
def Reason() =>
    return Explanation(subject: subject, evidence: evidence).
end

def main() =>
    return reason "tiger" with "observed".
end
```

## Tokenization

Tokenization is deterministic and training-free. The lexer owns fixed syntax,
comments, strings, numbers, punctuation, operators, and reserved words. Each
identifier or mixfix-anchor byte maps into a fixed range after the grammar
tokens, so the interpreter requires no model file or generated vocabulary.
