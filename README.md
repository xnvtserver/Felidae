# Felidae

![Felidae — From Facts to Intelligence](Media/Felidae_%20From%20Facts%20to%20Intelligence.png)

> **Knowledge is the runtime.**

**Felidae** is an open-source, deterministic, graph-oriented query and reasoning language for `.fx` files.

It is designed for software where **facts, relationships, queries, and explainable logic** are central to the application.

Instead of treating knowledge as something hidden inside application code, Felidae lets you express it directly as data and logic.

```text
Facts → Relationships → Queries → Reasoning → Results
```

Felidae is **not intended to be a general-purpose programming language**.

It focuses on knowledge-heavy and reasoning-oriented applications where predictable execution and inspectable results matter.

---

## Why Felidae?

Many applications eventually contain two different worlds:

- data stored in a database
- business knowledge buried inside application code

Felidae brings them closer together.

You can describe facts:

```felidae
def Person(name: "Ada", role: developer).
def Person(name: "Grace", role: researcher).
```

Query them:

```felidae
? Person(name: x)
```

and build logic around the same knowledge.

Felidae is built around a few simple ideas:

- **Facts are first-class data**
- **Relationships form a graph**
- **Queries operate directly on knowledge**
- **Reasoning stays deterministic**
- **Persistent knowledge survives program execution**
- **The same language works interactively through the REPL**

---

## A Small Example

```elixir
def Animal(name: "tiger", habitat: "forest").
def Animal(name: "otter", habitat: "river").

def main() =>
    Animal(name: "tiger").
end
```

Felidae can also query stored facts:

```elixir
? Animal(name: x)
```

or filter them programmatically:

```elixir
def main() =>
    def forest_animals := Animal.where(habitat: "forest").
    forest_animals.
end
```

The idea is simple:

> Store knowledge as facts, then ask questions about that knowledge.

---

## Facts

Facts represent things Felidae knows.

```elixir
def Product(
    id: "product-1",
    name: "Laptop",
    available: true
).
```

Facts can represent people, products, events, observations, rules, business entities, or domain knowledge.

Felidae stores persistent facts so they can be queried later instead of disappearing when a program finishes.

---

## Symbolic Values

Felidae supports symbolic **atoms** in addition to normal strings.

```elixir
def status := active.
def message := "active".
```

Here:

```erlang
active
```

is a symbolic value, while:

```elixir
"active"
```

is a string.

This makes it natural to represent states, categories, concepts, and domain vocabulary.

---

## Classes

When you want a defined structure, you can declare a class.

```ruby
class Person
    def name: string.
    def age: number.
end
```

Objects can then follow that structure:

```elixir
Person(
    name: "Ada",
    age: 30
)
```

Felidae also supports inheritance when knowledge naturally forms a hierarchy.

```ruby
class Student extends Person
    def grade: number.
end
```

---

## Relationships

Facts in Felidae can be connected.

For example:

```elixir
def Employee(id: "employee-1", name: "Ada").
def Department(id: "department-1", name: "Research").

Link(
    from: Employee(id: "employee-1"),
    to: Department(id: "department-1"),
    properties: {
        kind: "works_in"
    }
).
```

This allows knowledge to form a graph:

```text
Employee ── works_in ──> Department
```

Relationships can then participate in queries and reasoning.

This is useful for domains such as:

- knowledge systems
- business rules
- recommendation logic
- organizational relationships
- dependency analysis
- expert systems
- explainable decision systems

---

## Functions

Felidae also provides familiar programming constructs for building reusable query and reasoning logic.

```elixir
def Greeting(name: string) =>
        message: "hello",
        name: name.
end
```

Functions are intentionally part of the language so that knowledge can be transformed and queried without moving the reasoning into another programming language.

---

## Interactive REPL

Felidae includes an interactive REPL.

Start Felidae without giving it a program file:

```bash
felidae
```

Then experiment directly:

```elixir
> def Person(name: "Ada", role: developer).

> ? Person(name: x)
```

The REPL is useful for:

- exploring data
- testing queries
- experimenting with language syntax
- inspecting results
- learning Felidae interactively

Run:

```text
:help
```

inside the REPL to see the available commands.

---

## Getting Started

Clone the repository including its submodules:

```bash
git clone --recurse-submodules https://github.com/xnvtserver/Felidae.git
cd Felidae
```

### Linux / macOS

```bash
./build.sh debug --test
```

### Windows

```powershell
.\build.ps1 debug --test
```

Felidae is implemented in **C++20** and built with **CMake**.

After building, run the generated `felidae` executable or start the REPL.

---

## A Felidae Project

Felidae programs use the `.fx` extension.

A project contains an `init.fx` file that configures its persistent knowledge store.

A minimal `init.fx` looks like:

```felidae
import "db".
db.location("./data/felidae.db").
```

You can then create `.fx` files containing your facts, queries, classes, relationships, and reasoning logic.

---

## Where to Learn More

The README intentionally gives only an introduction.

Detailed documentation lives separately:

### Language Reference

See [`docs_language.md`](docs_language.md)

Use it for:

- language syntax
- facts and queries
- classes
- control flow
- atoms
- exceptions
- graph traversal
- mixfix expressions
- database operations

### Interpreter & Architecture

See [`code.md`](code.md)

Use it when you want to understand how Felidae itself works internally.

### Examples

Explore [`v2_examples/`](v2_examples/) for executable Felidae examples.

### Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md) if you want to contribute to the language, interpreter, tooling, documentation, or editor integrations.

---

## Editor Support

The Felidae ecosystem includes integrations for several editors, including:

- Visual Studio Code
- IntelliJ IDEA
- Vim
- Zed
- Sublime Text
- Emacs
- Notepad++
- Nano

Editor integrations live alongside the main project and use Felidae's language tooling.

---

## What Felidae Is Exploring

Felidae is built around a broader question:

> **What would software look like if knowledge itself were a native runtime concept?**

Instead of spreading domain knowledge across database schemas, application services, condition trees, and disconnected rule engines, Felidae explores a model where facts and relationships can be queried and reasoned about directly.

The goal is not to replace existing programming languages.

The goal is to provide a focused language for systems where **knowledge is the application**.

---

## Open Source

Felidae is open source under the MIT License.

If the project interests you:

⭐ **Star the repository**  
🍴 **Fork it and experiment**  
💬 **Join GitHub Discussions**  
🐛 **Report issues**  
🧩 **Contribute features, examples, tooling, or documentation**

Every contribution helps shape the language.

---

## Project

**Felidae**

A deterministic, graph-oriented query and reasoning DSL.

**From facts to intelligence.**

Website: [xnovity.com/felidae](https://www.xnovity.com/felidae)

Built as an open-source project by **Xnovity Softwares Pvt. Ltd.**
```

This version deliberately removes things that currently make the README feel like internal engineering documentation: RocksDB version specifics, AST pipeline details, ByteT5/tokenizer internals, database locking semantics, benchmark flags, cache/SST metrics, debugger internals, cross-compilation rules, source fingerprints, parser implementation details, and similar material. Those belong in `code.md`, `docs_language.md`, `CONTRIBUTING.md`, or dedicated documentation. The current repo already has those destinations. :chatgpt-content-reference{index="3"}

The new media image is also a much better opening than an architecture diagram because it communicates the project concept immediately while the README progressively explains the language. :chatgpt-content-reference{index="4"}