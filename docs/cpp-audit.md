# C++ audit — issues found while implementing the extension/ML/analytics rounds

Scope: the C++ files read across these rounds — `src/debugger/AstAnalyzer.{h,cpp}`,
`src/debugger/main.cpp`, `src/celidae/Visualization.{h,cpp}`,
`src/celidae/main.cpp`, and the `Arg`/`Expr` shapes in `src/AST.h`. Findings are
marked **fixed** (addressed), **open** (real, not yet addressed), or
**note** (design observation worth a decision).

---

## Fixed

### 1. `SymbolDefinition` discarded parameters the analyzer already had — **fixed**
`AstAnalyzer` walks `clause->head.args` for its unused-variable diagnostics, but
`SymbolDefinition` carried only `{name, count, spans}`. Every editor feature that
needed a signature therefore re-derived parameters from source text with its own
regex, and two of those regexes were wrong (see §7). Fixed by adding
`SymbolParameter{name, type}`, populating it from the existing arg walk, and
emitting it from `--symbols-json`. Additive, so older extension builds are
unaffected.

### 2. Parallel colour definitions that could silently drift — **fixed**
`kindColor()` and the JS `COLORS` map in `webui/template.html` defined the same
per-node-kind palette twice, in two languages, with no shared source. Fixed by
making `kindColor()` the single source: every payload now carries a `"palette"`
member built from it, and the page reads that instead of keeping its own table.
A kind added in C++ is now coloured correctly in the browser with no JS change.

### 3. `renderLegend()` hardcoded the first five kinds — **fixed**
It listed `COLORS`' first five keys, so a sixth kind rendered on the graph but
never appeared in the legend. It now lists exactly the kinds present in the
current view, read from the payload.

### 4. `computeLaneLayout()` hardcoded kind ordering — **fixed**
Lanes were keyed to the literal sequence `fact, field, method, global, library`,
so every later kind collapsed into one undifferentiated trailing column — and
the analytics work added four (`event`, `record`, `segment`, `measure`). The
order now comes from `allNodeKinds()`, the same list the palette comes from, and
an unrecognised kind is given its own column rather than sharing one.

### 5. `standaloneHtml()`'s fixed three-payload arity — **fixed**
`standaloneHtml(schemaJson, graphJson, erJson)` meant every new diagram type
forced a signature change, a new token, and a new call-site argument. It now
takes a `std::map<DiagramType, std::string>`, and a type the caller omits is
substituted with a JSON `null` that the page treats as "this view was not
generated". That is exactly what makes `--template=<name>` able to emit a
focused single-view file from the same template. `replaceToken()` still throws
on a missing token, which is the desired behaviour — it fails loudly at export
time rather than shipping a broken page — and `generate-template.js` now checks
the full token list at build time too.

### 6. `FactProfile` discarded every literal value — **fixed**
`consume()` read only `argument.name` when profiling a fact, never
`argument.value`, so **no value-based visualization was possible at all**.
`FactProfile` now carries `samples` (capped at `kMaxFactSamples = 500` per fact
type, with the truncation surfaced on the node and as an insight rather than
silently). This unblocked the timeline, distribution, comparison and cluster
views, and the whole `Analytics` layer.

### 7. Duplicated declaration-scanning regexes across the toolchain — **fixed**
The same "the parser knows, but nobody asks it" pattern as §1: the
declaration-head regex existed in four near-identical variants with different
capture-group numbering, so a fix in one did not reach the others. Consolidated
behind `FelidaeCallResolver` (IntelliJ) and a single `DECLARATION_PATTERN`
(VS Code).

### 8. Hierarchy view double-prefixed its node ids — **fixed**
Introduced during the analytics rewrite and caught by the new test suite:
`consume()` stores a reference's *source* already qualified (`fact:Station`) but
its *target* bare (`Reading`), and the hierarchy builder applied `nodeId()` to
both. The result was an edge pointing at `fact:fact:Station`, which does not
exist — cytoscape throws on a dangling endpoint and abandons the entire view.
It also broke the depth calculation, so every type reported depth 0. Both sides
are now reduced to bare names before `nodeId()` is applied once.

### 9. `-Woverlength-strings` aborted the Windows build — **fixed**
`GeneratedVisualizerAssets.h` is one deliberately enormous string literal, and
`-Wpedantic` warns because the standard only *requires* support for 65536
characters. Harmless in itself, but `build.ps1` runs under
`$ErrorActionPreference = "Stop"`, where a native command writing to stderr
aborts the build. Suppressed explicitly in both build scripts with the reasoning
recorded there.

---

## Open

### 10. The lexer rejects a UTF-8 byte-order mark — **open, not addressed (core language)**
A `.fx` file saved as "UTF-8 with BOM" — which is what Windows PowerShell's
`Set-Content -Encoding utf8`, Notepad, and several editors produce by default —
fails with `error: Unexpected character '' at 1:1`. Found while writing
`scripts/test_celidae.ps1`, whose fixtures were rejected for this reason alone.

This is **not** a Celidae bug: it is in `Lexer::ensureChar`
(`src/Lexer.cpp`), which feeds the interpreter, the debugger and Celidae alike.
The fix is to skip a leading `EF BB BF` on the first read — four lines, purely
additive, with no effect on any file that does not start with one. It is left
open deliberately: whether `.fx` accepts a BOM is a language decision, and this
round's scope was Celidae and the editor tooling, not core tokenization. Worth a
decision, because the failure mode is a confusing error on a file that looks
perfectly normal in every editor.

### 11. Five pre-existing failures in `scripts/test_felidae_examples.ps1` — **open, unrelated**
`streaming file reader smoke`, `stdlib utilities`, `console input number true
branch`, `console input number false branch`, and `repl query global builtin`
fail at the current commit. All five exercise the interpreter's file-I/O and
stdin paths (`csv_text: ""` and `deleted_count: 0` where non-empty values are
expected). No interpreter source was modified in this round, so these predate
it — recorded here so they are not mistaken for regressions.

### 12. Panel types the SVG exporter cannot draw — **open, by design for now**
`standaloneSvg` renders `bar`/`hbar`/`histogram`/`line`/`scatter` panels
natively, and names the fields covered by a `boxplot`. `treemap`, `heatmap` and
`parallel` are HTML-only: each needs a layout algorithm (squarified treemap,
matrix binning, axis normalisation) that would roughly double the exporter for
three charts whose value is largely interactive. The SVG says so rather than
emitting an empty frame.

---

## Notes

### 13. `--type` rejects unknown values by throwing — **note, correct as-is**
`parseType()` throws on an unrecognised diagram name. That is right (a typo
should not silently produce a schema diagram). The error message enumerates the
valid set from `kAllDiagramTypes`, so it now stays correct automatically when a
type is added — it no longer has to be updated by hand alongside the enum.

### 14. `celidaeSources` / `debugSources` are explicit file lists — **note**
`build.ps1` and `build.sh` each enumerate sources, so any new `.cpp` requires
editing both and forgetting one breaks only that platform. This round added
`src/celidae/Analytics.cpp` and therefore had to edit both — the trap is real.
A glob would remove it entirely.

### 15. Hand-rolled JSON emission — **note, now covered by tests**
Both `symbolsJson`/`symbolDefinitionsJson` and Celidae's payload writer build
JSON by string concatenation. This is the class of bug that previously produced
a malformed `publishDiagnostics` notification (one `}` too many) and
`jsonEscape()` being wrongly used for XML in `standaloneSvg()`. It is now
guarded from both ends: `scripts/test_celidae.ps1` parses every payload for
every diagram type across all 151 example programs, checks that no `NaN` or
`Infinity` literal escapes (neither is valid JSON, and either would make the
browser reject the whole document), and feeds through a fixture containing `&`,
`<`, `]]>` and a literal `</script>` to confirm neither the XML nor the
`<script>`-embedded JSON can be broken out of.

---

## Cross-cutting

Findings §1, §6 and §7 shared one root cause: **the C++ side already held
parsed, authoritative information that downstream consumers re-derived, less
accurately, from text.** All three are now fixed, and the same principle drove
this round's main change — `RenderNode` carries `metrics`/`attributes` as
structured values, so the HTML view reads `node.metrics.coverage` instead of
running `/coverage=([\d.]+)%/` over a prose string whose wording was free to
change at any time. `detail` still exists, but it is now *generated from* those
values, so the tooltip and the data cannot disagree.
