# Felidae interpreter: gaps, problems and directions

Audit of the interpreter (the Felidae DSL's C++ runtime) done by running about 40
small probes against the built `felidae.exe` and by reading the C++. The exe was
newer than every source file, so it reflects the working tree at the time. The
full ctest suite was not run, and only a debug build was available. Probes lived
under `build/probes/audit/`.

Status column: **open**, **fixed (needs build)** for changes made in source but
not yet built or run by the maintainer, **decision** for items waiting on a
semantics choice.

## 1. Bugs confirmed by running them

| # | Bug | Status |
|---|-----|--------|
| 1.1 | `for` and `while` run only their first iteration | fixed (needs build) |
| 1.2 | `tests/control_flow.fx` does not parse, so loop semantics are untested | fixed (needs build) |
| 1.3 | A typo in a fact pattern binds a fresh variable and gives a wrong answer | fixed as a warning (needs build) |
| 1.4 | A literal in a parameter (`n: 0`) is silently ignored; the first overload wins | fixed (needs build) |
| 1.5 | A function ending in `def x := ...` returns the internal value `fn:tuple(value: true)` | fixed for success (needs build) |
| 1.6 | Numbers are doubles only; large integers print in scientific notation; `length` counts bytes | printing and `length` fixed (needs build); no int type |
| 1.7 | `_` is rejected for a typed field, and `where(address: _)` matches only a stored `_` | open (found while fixing 1.3) |
| 1.8 | `break` cannot be conditional: not allowed in `then/else`, and inside `switch` it only leaves the switch | open (found while fixing 1.2) |

Details:

1. **`for` and `while` stop after their first iteration.**
   `for i in range(0, 4) then console.writeLine(value: i). end` prints only `0`.
   A `for` that inserts facts leaves one inserted.
   Cause: `solveExpressionGoal` sets `ReturnId` for every expression statement
   (and `def x := ...` does the same), and both loops treated a `ReturnId` in the
   iteration environment as an early return. That check dates from when `return`
   was a keyword; `return` has since been removed, so a trailing expression is
   indistinguishable from an early return.
2. **The only loop test is dead.** `tests/control_flow.fx` fails to parse
   (`Expected an expression at line 9`): an orphan `end` is left from the block-`if`
   removal, and `continue` is not an expression, so it cannot be a branch of
   `cond then a else b.`. It was the only file that failed `--check-json` across
   `tests/` (excluding `invalid*`).
3. **A typo in a fact pattern silently gives a wrong answer.**
   `def Person(name: whoo, age: a)` binds a fresh variable `whoo` and returns the
   first Person's age. There is no singleton-variable warning.
4. **A literal in a parameter is silently ignored.**
   `def sumTo(n: 0, acc: number)` is not a value pattern. The first overload
   matches any call, so `sumTo(n: 5, acc: 3)` returned 3.
5. **A function whose last statement is `def x := ...` leaks an internal value:**
   it returns `fn:tuple(value: true)`.
6. **Numbers are doubles only.** `9007199254740993` prints as
   `9.00719925474099e+15`; there is no integer type. `length("héllo")` is 6
   because it counts bytes.

## Work log: section 1

Everything below is in source and compile-checked only (`cl /Zs`); nothing has been
built or run. Build and test commands are at the end.

- **1.1 loops.** Removed the "ReturnId in the iteration environment means stop"
  check from both the `for` and the `while` loop in `solveIterative`
  (`src/Interpreter.cpp`). Only `break` ends a loop early; a `while` ends when its
  condition is false or its body fails. A `while` whose condition never changes now
  runs to the existing 1,000,000-iteration safety limit instead of silently running once.
- **1.2 tests.** `tests/control_flow.fx` now skips an iteration with a `switch` +
  `continue` (the old `cond then continue else nil.` form is not valid). Checked
  against the old exe: it now parses and reports `for_count: 1, while_count: 1`, so
  it fails there and should pass after the rebuild (`for_count: 4, while_count: 2`).
  New `tests/loop_every_iteration.fx` (`loop_every_iteration`) covers a body that
  ends in an insert or a binding.
- **1.3 singleton variables.** New parser warning (`IntegerParser::warnings()`): a name
  a fact pattern introduces, used nowhere else in its function, with the message
  "'x' is bound by a fact pattern but never used again...". `_` and any name starting
  with `_` are exempt, as agreed (`_` means "ignore this field, to be filled later").
  Reported only by `--check-json` (severity `warning`) and only for the entry file,
  because diagnostics carry no file name. A run never prints it. A regex-level scan of
  `core/`, `tests/`, `v2_examples/` and `examples/` found no candidates, but running
  `--check-json` over all 146 files found one false positive: a head variable shared
  with the body (`def eligible(id: x) => def Observation(id: x).`). Head variables now
  count as a mention. Test: `singleton_pattern_variable_warned`.
- **1.4 literal parameters.** A method clause (`=>` with a value expression) whose
  parameter is a literal now fails to parse: "Parameter 'n' of 'sumTo' must name a
  type... a literal value is not a pattern". Relational rules keep literal head
  arguments because they unify. Test: `literal_parameter_rejected`.
- **1.5 rule result.** A rule whose goals all held now returns plain `true` instead of
  `fn:tuple(value: true)` (`successTruthTuple` removed). A failing rule still returns
  its tuple, which records which goals failed: `isMethodTruthTupleWithFalse` (used by
  lambda filtering) depends on it. Making failures a plain `false` too needs a look at
  the lambda code first.
- **1.6 numbers and strings.** `NumberExpr::debug()` (`src/AST.h`) prints whole numbers
  below 2^63 in full digits, so `9007199254740992` no longer prints as
  `9.00719925474099e+15`; precision above 2^53 is still lost (doubles), now documented
  here. `length` and `count` of a string count UTF-8 characters, not bytes. There is
  still no integer type; adding one needs a design (persistence tags, arithmetic rules).
- **1.7 and 1.8** are new findings, not yet worked on:
  - A typed class field rejects `_` (`Class field 'address' expects optional<string>`),
    so the "fill it in later" placeholder only works for undeclared classes. In a fact
    pattern `_` is a wildcard, but `Employee.where(address: _)` matches only facts that
    store a literal `_`.
  - `continue` works inside a `switch` in a loop, but `break` there only leaves the
    switch, and neither is allowed inside `cond then a else b.`.

To build and check (long-running, so run these yourself):

```powershell
.\build.ps1 -Configuration debug -Test
ctest --test-dir build\debug\x64 -C Debug -R "control_flow|loop_every_iteration|literal_parameter|singleton_pattern" --output-on-failure
ctest --test-dir build\debug\x64 -C Debug --output-on-failure
```

Run the whole suite at least once: the loop fix changes the behaviour of every `for`
and `while` that used to stop after one iteration, and the literal-parameter check can
reject a method that relied on the old behaviour.

## Test run after the section 1 fixes (16 failures)

Reported by ctest after building the section 1 changes. Each was re-run on its own
with `--output-on-failure`. There was no baseline build to compare against, so
"not caused by the section 1 changes" means the cause was found in the test or in
code those changes do not touch.

| Test | Cause | Status |
|------|-------|--------|
| singleton_pattern_variable_warned | Mine: the JSON puts `message` before `severity`, the regex had them the other way round. The warning itself was correct (only `whoo`). | fixed (CMakeLists.txt) |
| standalone_repl | Its input script is written in removed syntax throughout (`? query`, `return`, `answer := 7.`, class fields without `def`, block `if`). My change (a rule now returns `true`) also needed its `fn:tuple(value: true)` expectation updated, which is done. | script still needs migrating |
| tail_call_goal_position | Real bug 1.9, see below. | fixed (needs build) |
| analytical_query_language_tour | Parse error at `count >= 400`: a parameter named `count` was read as a whitespace call to the builtin `count`. Real bug in whitespace-call parsing. | fixed (needs build) |
| direct_ast_fact_query, durable_solver_backtracking, relational_rule_binds_output | Pass the removed external `? query` argument. | tests need `--query` |
| syntax_missing_top_level_period_rejected | The fixture `tests/invalid/missing_top_level_period.fx` is `def Node(id: "a").`, which is valid; unchanged since HEAD. | fixture needs the period removed |
| project_configuration | Expects different wording than `Bindings must begin with 'def'; use 'def value := ...'`. | test needs updating |
| rocks_reasoning_facts, rocks_atom_reasoning | `reasoning.prove` rejects `eligible` ("requires a pure relational predicate"): `isTableEligiblePredicate` needs `ClauseKind::Rule`, so the clause is probably being classified as a method now that `return` is gone. Not confirmed. | open |
| non_boolean_fact_query_rejected | Uses `lambda(...)`, which now returns `[]` instead of raising. | open (lambda) |
| failed_value_call_runs_once | Written for the old rule that a failing statement (`x > 5.`) makes the call `false`; now the final expression `x.` is the result and the guard is only a value. | needs a decision |
| fact_namespace_rejected | Expects the message "'Fact' is not a queryable class"; the interpreter says "count expects an array, collection, or string". | open |
| rocks_graph_multiple_links | The test links the same pair twice and now gets `Duplicate Link`. | needs a decision |
| atom_runtime_values | Failed in the user's run, passed on its own and in 3 repeats here. Not reproduced. | flaky? |

### 1.9 A call statement in the middle of a body skips the statements after it

`def outer(x) => side(x). 100. end` returned `side(x)`'s value (2), not 100.
`solveExpressionGoal` jumped (`TailCallSignal`) for any user-function call used as an
expression statement, not only the last one. Before `return` was removed, the
`return` keyword marked the tail position. It also meant a call as the last statement
of a loop body would escape the loop.
Fix: `solveMethodCall` records the last goal of the body it is solving
(`tailGoal_`) and only that goal may jump. Only written and compile-checked.

## 2. Hard limits in the evaluator

- **Non-tail recursion is capped at depth 8** (`kMaxNativeMethodCallDepth`,
  `src/Interpreter.cpp`). `fib(8)` fails. Expression nesting is capped at 64 and
  the parser at 128. The source comment says "until the method-aware frame engine
  lands".
- **Tail calls only work when the call is the whole final expression.**
  `n = 0 then acc else sumTo(...)` fails at n = 7, because the else-branch call is
  not recognised as a tail call.
- **No way to iterate a collection and accumulate.** `xs.map`, `xs.filter`,
  `xs.reduce`, `xs.append`, `xs[0]`, `xs + [9]` and `"abc".len()` all error, and
  rebinding `acc := ...` is rejected by design.

## 3. Syntax that is confusing or error-prone

- **`def` means five things:** a binding, a function, a persistent seed, a fact
  pattern and a class field. Which one applies depends on position, and the
  failure mode is silent (see 1.3).
- **At least six call forms:** `f(a: 1)`, `f(1, 2)`, `f 1 2`, `f 1, 2`, `x.f()` and
  mixfix.
  - Whitespace calls make parsing depend on each function's declared arity.
  - `add 1 -2` and `add 1 - 2` both fail to parse as intended.
  - `add 1 2 + 1` means `(add 1 2) + 1`, and nothing makes that visible.
- **Three output paths:** bare `print "x".`, `console.writeLine` and `system.print`.
- **Unknown names silently become data.** `totl + 1` fails with "Operator '+'
  expects numeric operands". `prnt("hi")` returns `prnt(value: "hi")`, with
  positional arguments auto-named `value`.
- **`cond then a else b.` is the only conditional form.** It reads like a statement,
  and `continue` and `break` cannot be used inside it.
- **`where(nmae: ...)` returns `[]` silently** instead of flagging a field that is
  not in the schema.
- **Imports are implicit.** Calling `console.x` loads `core/console.fx` by name
  prefix (`ensurePredicateLoaded`), so `import` does not mean what it appears to.

## 4. Diagnostics and docs

- **Runtime errors carry no location or call stack.** `inner -> middle -> main`
  reports only `Operator '+' expects numeric operands`. `DivisionByZero` is
  reported with no context at all.
- **`--check-json` does no semantic checks:** no unresolved names, arity, field
  typos or unused variables, which is why the typos above pass.
- **`docs_language.md` is stale:** it still shows `return` (8 uses) and block
  `if ... end`, both removed, and documents `felidae animals.fx '? ...'`, which
  `main.cpp` rejects.

## 5. C++ architecture

- **`src/Interpreter.cpp` is 11,489 lines.** `evalCallAsValueOnce` alone is 1,549
  lines and `evalBuiltinTerm` is 966; four more functions run past 370 lines.
  Control flow is a mix of `bool` returns, exceptions and signal objects.
- **Runtime values are AST nodes.** Arguments are `clone()`d and the environment is
  copied on every call (a sorted vector, so inserts are O(n)).
- **Break, continue and tail calls are C++ throws.** A 100k-iteration tail loop
  would throw 100k times.
- **Threads copy the entire interpreter state** (clauses, globals, operators). They
  take only a function name with no arguments and return the result as a string.
- **The native ABI is JSON over C strings, and shared libraries are `dlopen`ed**
  with no signing or sandboxing.
- **A project cannot run without `init.fx` and an on-disk RocksDB.** There is no
  in-memory mode. State persists across runs, so programs are not re-runnable. One
  process per database directory also makes the editor, REPL and tests collide.

## 6. Suggested order

1. Fix the loop bug and restore `control_flow.fx`, with loop tests that distinguish
   "ran once" from "ran N times".
2. Add a static check pass: unresolved identifier, singleton fact-pattern variable,
   unknown field against a known schema, literal-in-parameter, unreachable
   expression statement. Surface it in `--check-json` so every editor gets it.
3. Add runtime locations and a call stack to errors, including the offending
   operand types.
4. Give the language a way to iterate: `map`/`filter`/`reduce`/`append`/indexing, a
   tail call that works through `then/else`, and a raised or removed depth-8 limit.
   This probably means replacing the native-stack recursion with the frame engine
   the source comment describes.
5. Decide on integers versus doubles, and on whitespace calls (keep them with an
   arity rule printed in errors, or drop them).
6. Add an ephemeral database mode for REPL, notebook and tests.
7. Rewrite `docs_language.md` against the current syntax, then split
   `Interpreter.cpp` along the `eval*`, `solve*` and `thread` seams.
