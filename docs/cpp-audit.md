# C++ audit — issues found while implementing the extension/ML round

Scope: the C++ files read during this round — `src/debugger/AstAnalyzer.{h,cpp}`,
`src/debugger/main.cpp`, `src/celidae/Visualization.{h,cpp}`,
`src/celidae/main.cpp`, and the `Arg`/`Expr` shapes in `src/AST.h`. Findings are
marked **fixed** (addressed this round), **open** (real, not yet addressed), or
**note** (design observation worth a decision).

---

## Fixed this round

### 1. `SymbolDefinition` discarded parameters the analyzer already had — **fixed**
`AstAnalyzer` walks `clause->head.args` at `AstAnalyzer.cpp:494` for its
unused-variable diagnostics, but `SymbolDefinition` carried only
`{name, count, spans}`. Every editor feature that needed a signature therefore
re-derived parameters from source text with its own regex, and two of those
regexes were wrong (see §5). Fixed by adding `SymbolParameter{name, type}`,
populating it from the existing arg walk, and emitting it from
`--symbols-json`. Additive, so older extension builds are unaffected.

---

## Open

### 2. Parallel colour definitions that can silently drift — **open**
`kindColor()` (`Visualization.cpp:471`) and the JS `COLORS` map in
`webui/template.html` define the same per-node-kind palette twice, in two
languages, with no shared source. A new node kind, or a palette tweak in one
place, diverges silently: SVG export and HTML export would disagree about what
colour a "method" node is. Worth generating one from the other (the template is
already produced by `generate-template.js`, which could substitute a
`__KIND_COLORS__` token emitted from the C++ table).

### 3. `renderLegend()` hardcodes the first five kinds — **open**
It lists `COLORS`' first five keys, so any sixth node kind renders on the graph
but never appears in the legend — a silent omission rather than a visible bug.

### 4. `computeLaneLayout()` hardcodes kind ordering — **open**
Lanes are keyed to the literal sequence `fact, field, method, global, library`;
unknown kinds all collapse into one undifferentiated trailing column. Fine
today, but it is the second place (with §2 and §3) that must be edited in
lockstep whenever a kind is added — three edits, none of which fail loudly if
forgotten.

### 5. `standaloneHtml()`'s fixed three-payload arity — **open**
`standaloneHtml(schemaJson, graphJson, erJson)` plus a `replaceToken()` that
*throws* on a missing token means every new diagram type forces a signature
change, a new token, and a new call-site argument. `replaceToken` throwing is
good (it fails at build time rather than emitting a broken page), but the arity
should be a `DiagramType`-keyed payload instead of positional parameters. This
is the blocker for the selectable-template work that remains outstanding.

### 6. `FactProfile` discards every literal value — **open**
`consume()` reads only `argument.name` when profiling a fact
(`Visualization.cpp`, `FactProfile{records, fields}`), never `argument.value`.
`AST.h`'s `Arg` does carry `shared_ptr<Expr> value`, and `StringExpr`/
`NumberExpr`/`BoolExpr` expose their literals, so the data is available — it is
simply dropped. Consequence: **no value-based visualization is possible at
all** (a timeline needs a date field's value; a distribution chart needs the
values). Any such view requires extending `FactProfile` with sampled values
first — with a cap, since a large fact set would otherwise retain every literal.

### 7. Duplicated declaration-scanning regexes across the toolchain — **open (non-C++, same root cause)**
Worth recording because it is the same "the parser knows, but nobody asks it"
pattern as §1: the declaration-head regex existed in four near-identical
variants (VS Code `DECLARATION_PATTERN`, IntelliJ's completion contributor,
IntelliJ's goto-declaration handler, the run-gutter manager), with different
capture-group numbering, so a fix in one did not reach the others. Two were
consolidated this round; the goto-declaration and gutter variants remain.

---

## Notes

### 8. `--type` rejects unknown values by throwing — **note, correct as-is**
`parseType()` throws on an unrecognised diagram name. That is the right
behaviour (a typo should not silently produce a schema diagram), just worth
knowing when adding types: the error message enumerates the valid set and must
be updated alongside the enum.

### 9. `celidaeSources` / `debugSources` are explicit file lists — **note**
`build.ps1` and `build.sh` each enumerate sources. Any new `.cpp` requires
editing both, and forgetting one breaks only that platform. Keeping additions
inside existing translation units avoids the trap; a glob would remove it
entirely.

### 10. Hand-rolled JSON emission has no escaping test — **note**
`symbolsJson`/`symbolDefinitionsJson` build JSON by string concatenation with a
`jsonEscape()` helper. It is correct for the current fields, but the parameter
`type` values now flowing through it come from source text. A round-trip test
(emit → parse) over the example corpus would cheaply guard this; note the
precedent that the *previous* round found `jsonEscape()` being wrongly used for
XML in `standaloneSvg()`, which is exactly this class of bug.

---

## Cross-cutting

Findings §1, §6 and §7 share one root cause: **the C++ side already holds
parsed, authoritative information that downstream consumers re-derive, less
accurately, from text.** §1 is now fixed and demonstrably worth it — the
regex-derived parameter list for builtins was returning the `:=` assignment
target as a parameter (`Fact.all(` suggested `rows`), a bug that disappeared
once the data came from one correct place. §6 is the same fix waiting to
happen for fact values.
