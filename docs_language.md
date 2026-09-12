# Felidae language reference

Felidae parses `.fx` source into a `Program` AST and evaluates that AST with
the direct interpreter. A program never needs conversion to a binary file.

## Statements

Facts, rules, methods, globals, imports, and annotations are source-level AST
statements. Use `.` for ordinary declaration boundaries.

```felidae
Person(name: "Ada", active: true).

Greeting(name: string) =>
    return (message: "hello", name: name)

main() =>
    return Greeting(name: "Ada")
```

`:=` is immutable single-assignment inside a goal sequence. `where`, `if`,
`not`, groups, `or` branches, comparisons, and returns are interpreted in
source order. A rule’s fallback branch is evaluated only after the normal
branch cannot solve.

## Classes and explicit blocks

`class` declarations are direct AST schema declarations. Their inheritance is
registered in `FactMemory`, so a child fact participates in queries for its
parent type. `end` explicitly closes a class, method, or nested `if` block;
it has no VM or compiled representation.

```felidae
class Person
    name: string
    index(name)
end

class Student extends Person
    grade: number
end

main() =>
    return Student(name: "Ada", grade: 10)
end
```

## Facts and queries

Facts live in `FactMemory`, which maintains relation indexes, provenance,
temporal metadata, immutable snapshots, and copy-on-write rollback. Queries
unify their fields with candidate facts; there is no second query engine.

The direct fluent fact API is evaluated by that same store:

```felidae
active := School.where(district: "central").AndWhere(active: 1.0)
combined := School.where(district: "central").OrWhere(district: "west")
first := School.where(active: 1.0).limit(records: 1)
inserted := School.insert(values: {name: "Riverside", active: 1.0})
changed := School.where(name: "Riverside").update(values: {active: 0.0})
removed := School.where(name: "Riverside").delete()
first := School.get(pos: 0)
```

`db.sync(path:)` atomically reloads a fact-only `.fx` source file while
preserving unchanged logical identities. It is exposed by `core/db.fx`.

```felidae
Animal(name: "tiger", habitat: "forest").
Animal(name: "otter", habitat: "river").

main() =>
    return Animal(name: "tiger")
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
Reason() => return Explanation(subject: subject, evidence: evidence)

main() => return reason "tiger" with "observed"
```

## Tokenization

The custom BPE model is `models/felidae-bpe/model.txt`. The normal lexer owns
its fixed first 59 syntax IDs, comments, strings, numbers, punctuation,
operators, and reserved words (including `class`, `extends`, `index`, and
`end`). The remaining line-oriented text dictionary
stores identifiers and mixfix anchors only; missing identifiers are appended
in source order without training.

## Reloading source

Run `felidae program.fx --serve` to watch the root module and its loaded
imports. A changed source tree is parsed into a fresh AST interpreter and
swapped in only after successful registration; the previous program remains
available when a save has syntax or semantic errors.
