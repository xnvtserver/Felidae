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

### 16. The ER view was the schema view under another name — **fixed**
`buildGraph` handled `Schema`, `Graph` and `Er` in one branch, differing only in
whether imports were drawn. All three emitted a node per fact type fanned out
into a node per field, so on any program without `extend` — which is most of
them — the ER view was the schema view's picture with a different title and no
relationships in it at all. `examples/data/converted_csv_country.fx` produced
byte-comparable node lists for the two.

The two views answer different questions and now look different. Schema answers
"what is declared": every field, its coverage, its detected type. ER answers
"how do the entities connect": one box per entity with its keys, measures and
labels named on it, and an edge for every join. Since Felidae has no
foreign-key syntax, the joins are inferred — `inferRelationships()` looks for a
field whose values are contained in another type's near-unique field, which is
the definition of a foreign key pointing at a candidate key, and is measurable
without knowing what any field means. On `examples/celidae_heterogeneous_facts.fx`
it recovers all four joins (`Shipment→Warehouse`, `Shipment→Product`,
`Warehouse→Region`, `Sensor→Region`), none of which is declared anywhere. A
join that does not fully resolve is reported as a data-quality finding, which
is a class of problem nothing else in the toolchain can notice.

### 17. Zero-padded codes were treated as measurements — **fixed**
`country_code: "004"` classified as `numeric`, giving mean 433.84, median 434,
stddev 252.98 and skew 0.01 for the ISO 3166 numbering scheme — every figure
arithmetically correct and none of them about countries. It also produced a
histogram of the scheme and, via finding 18, a "timeline" of 249 countries.

`looksLikeSurrogateKey()` did not catch it: these codes are neither ascending in
declaration order nor densely packed (249 values over 4..894), so its density
test correctly declined. Padding is a separate signal and now has its own test.
Fixed width is the decisive form — a quantity spanning 4 to 894 is written "4"
and "894"; only a code is written "004" and "894" — because a share-based test
alone misses it, with just 30 of 249 values actually padded.

### 18. The timeline invented a time axis when the data had none — **fixed**
The ordering field fell back to "any numeric field" when no date was present,
on the reasoning that a version or sequence number orders records just as well.
True, and beside the point: the view is titled "records in date order", buckets
by calendar year, reports spikes "per period" and draws a moving-average trend.
All four statements are false about an axis that is not time. The concrete
result was a timeline of 249 countries ordered by ISO number with "Afghanistan
(004)" first, indistinguishable to a reader from a real one.

The fallback is gone, and a fact type now needs `kMinTimelineRecords` dated
records to appear at all — one dated record drew a single-bar chart that looked
exactly like a chart with a trend in it.

### 19. Rank-deficient least squares produced coefficients of 10¹³ — **fixed**
`explainNumericTargets()` solved with `householderQr()`, and the comment claimed
near-collinear predictors would "degrade the fit rather than produce nonsense".
That holds for *near*-collinear predictors and fails for exactly collinear ones
— which one-hot encoding manufactures every time, since a categorical field's
indicator columns sum to a constant. QR has no defined answer there and returns
an arbitrary point on the solution line.

On a fact type with three sensor units the reported drivers of a temperature
reading were `unit=coastal (+6387033529247.05)` and two further coefficients of
similar magnitude that very nearly cancelled, presented beside "99.91%
explained" as a finding. Now solved with `CompleteOrthogonalDecomposition`,
which is rank-revealing and returns the minimum-norm solution; where the rank is
short, `DriverModel::identifiable` is false, the bar chart is suppressed, and
the finding reports the fit while stating that the share belonging to any single
field cannot be determined. Degrees of freedom for the adjusted R² now come from
the rank rather than the column count.

### 20. Views were scored but nothing was gated on the score — **fixed**
`recommendViews()` measured which views a program's data could support, but the
result was advisory: a view scoring zero was dropped from the ranking while the
page still drew nine tabs, and clicking the inapplicable one reached a view that
had already reached for whatever field it could find so as to have something to
draw. Every view now carries an `applicable` verdict with a reason stated in the
same terms as the positive case, the HTML dims those tabs and explains them
rather than hiding them, and the page opens on the strongest view that applies
instead of on whichever came first in declaration order.

### 21. Findings and charts disagreed about what was worth showing — **fixed**
Four cases where a correct number was attached to a wrong or duplicated story:
Pearson coefficients between one-hot indicators reported as "product=BR-410 and
warehouse=021 move together (r = 0.79)", which states a co-occurrence in the
vocabulary of two quantities rising in step (Cramér's V and the contingency
table already report it properly); a correlation at r = 1.00 listed as a
discovery while the structure report separately called the same pair redundant;
a scatter rationale reading "the two move together, r = -0.84", the opposite of
the number beside it; and correlation heatmaps drawn for three-record fact types
whose every cell was arithmetic rather than evidence, leaving the most vivid
chart on the page as the one carrying the least information.

### 22. A single categorical field presented as multivariate data — **fixed**
Column count was used as the measure of how much a fact type had to say, but one
categorical field one-hot expands into as many columns as it has levels. A
`Product` type whose only non-key field was `material` yielded three columns, a
healthy cluster tendency, and two "segments" that were "brass" and "not brass" —
a grouping a bar chart already showed, carrying the added and false implication
that it had been discovered. `distinctSourceFields()` now gates segmentation,
correlation heatmaps and parallel coordinates on the number of declared fields
rather than encoded columns.

### 23. Two competing visual outputs, one of them untested against what it actually produces — **resolved by removing the second**
Celidae had two ways to leave the process: `--json`/`--inspect-graph`
(raw or `FELIDAE_GRAPH_BEGIN`-wrapped) for one view's payload, and `--html` for
the interactive bundle. `--html`'s payloads are the same JSON, just embedded
in a `<script>` element instead of printed alone - but `scripts/test_celidae.ps1`
tested exclusively through `--json`, meaning 384 assertions verified the data
model while never once parsing the actual `<script>` tag a browser reads. A
templating bug that corrupted one embedded payload while leaving the freestanding
JSON path untouched would have passed every test in the suite.

Resolved by retiring `--json`/`--inspect-graph`/`--visualize-data-json` (each
now a clear error pointing at `--template=<name>`) and rebuilding the test
harness on `Get-CelidaeViews`, which runs `celidae` once per fixture and
extracts every view's payload from its actual embedded `<script>` element. This
closes the gap directly: the suite now checks what ships, not a parallel code
path a user never invokes. It also cut the corpus sweep from roughly 1,350
process spawns (150 examples x 9 types) to 150, since one HTML build already
carries every view - the old per-type `--json` calls were re-deriving the same
graph nine times over.

`graphJson()`/`graphJsonEnvelope()` (the free-function JSON entry point and its
`FELIDAE_GRAPH_BEGIN`/`END` envelope) were unused outside `src/celidae/` and
are deleted rather than kept dead. `SchemaGraphAccumulator::json()`, the
member function `--html` itself calls to build each view's payload, is
unaffected - it was never a CLI output mode, only the plumbing this one is
built from.

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

### 11. Pre-existing failures in `scripts/test_felidae_examples.ps1` — **open, unrelated**
`streaming file reader smoke` and `stdlib utilities` fail at the current commit.
Both exercise the interpreter's file-I/O and stdlib paths (`csv_text: ""` and
`deleted_count: 0` where non-empty values are expected) and neither invokes
Celidae. No interpreter source has been modified in these rounds — the changes
are confined to `src/celidae/`, the two test scripts and `examples/` — so these
predate the work and are recorded here so they are not mistaken for regressions.

One test in this file did need updating, and it was not a regression: `celidae
ER diagram excludes execution nodes` asserted `"kind":"field"` appeared in the
ER payload, which encoded the very duplication finding 16 removes. It now
asserts the view's actual contract — entities and their keys, with no field,
method or global nodes — and a companion test requires the schema view to still
declare every field, so the distinction is pinned from both sides. The harness
grew a `Reject` key to express it, since these views are defined as much by what
they must not contain as by what they must.

### 12. Panel types the SVG exporter cannot draw — **closed, exporter removed**
`standaloneSvg` drew `bar`/`hbar`/`histogram`/`line`/`scatter` natively and
merely named the fields covered by a `boxplot`; `treemap`, `heatmap`, `parallel`
and `tree` it could not draw at all. Each needed its own layout algorithm
(squarified treemap, matrix binning, axis normalisation) to serve charts whose
value is largely interactive.

Resolved by removing SVG output rather than by completing it — HTML is now
Celidae's only visual output. That deleted ~470 lines including four
server-side layout engines and a second escaping path, and it closes finding 15
from one end by leaving one output format to keep well-formed instead of two.

One piece was worth keeping. `spectralLayout()` (Laplacian eigenmaps) existed
only to lay out the static SVG, and is now attached to every node in the
payload and offered as a **Spectral** layout in the interactive network view —
where it is more useful than it ever was in the export. cytoscape's own layouts
are a force simulation, a directed tree and a circle: none places nodes by how
the graph is actually connected, and the force layout settles somewhere
different on every run, so a node the reader located once has moved by the time
they look back.

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
`jsonEscape()` being wrongly used for XML in the since-removed
`standaloneSvg()`. It is now
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
