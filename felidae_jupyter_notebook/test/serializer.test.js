// The serializer is the code that reopens a notebook with its outputs and saves
// it again. It talks to VS Code's notebook classes, so it is run here against a
// minimal stand-in for them (same constructors and fields it uses).
const path = require("path"), Module = require("module");

const ERROR_MIME = "application/vnd.code.notebook.error";
const encoder = new TextEncoder(), decoder = new TextDecoder();
class NotebookCellOutputItem {
  constructor(data, mime) { this.data = data; this.mime = mime; }
  static text(value, mime = "text/plain") { return new NotebookCellOutputItem(encoder.encode(value), mime); }
  static error(error) { return new NotebookCellOutputItem(encoder.encode(JSON.stringify({ name: error.name, message: error.message })), ERROR_MIME); }
}
class NotebookCellOutput { constructor(items) { this.items = items; } }
class NotebookCellData { constructor(kind, value, languageId) { Object.assign(this, { kind, value, languageId }); } }
class NotebookData { constructor(cells) { this.cells = cells; } }
const vscodeStub = { NotebookCellKind: { Markup: 1, Code: 2 }, NotebookCellOutput, NotebookCellOutputItem, NotebookCellData, NotebookData };

const stubPath = path.resolve(__dirname, "serializer-stub.js");
const originalResolve = Module._resolveFilename;
Module._resolveFilename = function (request, ...rest) { return request === "vscode" ? stubPath : originalResolve.call(this, request, ...rest); };
require.cache[stubPath] = { id: stubPath, filename: stubPath, loaded: true, exports: vscodeStub };

const out = path.resolve(__dirname, "..", "out");
const { FelidaeNotebookSerializer, valueOutput } = require(path.join(out, "serializer.js"));
const { stringifyNotebook, parseNotebook } = require(path.join(out, "format.js"));
const { beautifyValue } = require(path.join(out, "beautify.js"));

let pass = 0, fail = 0;
const check = (name, actual, expected) => {
  const a = JSON.stringify(actual), e = JSON.stringify(expected);
  if (a === e) { pass++; console.log("  ok  ", name); } else { fail++; console.log("  FAIL", name, "\n     exp", e, "\n     act", a); }
};

const rows = '[Employee(id: "e1", role: "dev"), Employee(id: "e2", role: "ops"), Employee(id: "e3", role: "qa")]';
const stored = {
  version: 1,
  metadata: { projectFolder: "../shared" },
  cells: [
    { kind: "markdown", source: "# Title\n\nSome prose.\n" },
    { kind: "code", source: "def total := 42.", executionOrder: 1,
      outputs: [{ text: "42", ok: true }, { text: "", ok: true, metricsMarkdown: "**Metrics**\n\n- run 0.4 ms\n" }] },
    { kind: "code", source: "Employee.all().", executionOrder: 2, outputs: [{ text: beautifyValue(rows), ok: true }] },
    { kind: "code", source: "oops(", executionOrder: 3, outputs: [{ text: "cell 4, line 1, column 5", ok: false }] },
    { kind: "code", source: "total." }
  ]
};
const bytes = encoder.encode(stringifyNotebook(stored));
const serializer = new FelidaeNotebookSerializer();
const data = serializer.deserializeNotebook(bytes);

// ---- opening a file
check("cells keep their kind and get the right language",
  data.cells.map((c) => [c.kind, c.languageId]), [[1, "markdown"], [2, "felidae"], [2, "felidae"], [2, "felidae"], [2, "felidae"]]);
check("source text is restored exactly", data.cells[0].value, "# Title\n\nSome prose.\n");
check("notebook metadata, including the project folder, is kept", data.metadata, { projectFolder: "../shared" });

const mimes = (cell) => (cell.outputs ?? []).map((output) => output.items.map((item) => item.mime));
check("a value is two renderings of the same output: highlighted markdown and plain text", mimes(data.cells[1])[0], ["text/markdown", "text/plain"]);
check("metrics are a separate markdown-only output", mimes(data.cells[1])[1], ["text/markdown"]);
check("an error output uses VS Code's error mime type", mimes(data.cells[3]), [[ERROR_MIME]]);
check("a cell that never ran has no outputs", data.cells[4].outputs, undefined);
check("the value is shown highlighted as Felidae", decoder.decode(data.cells[1].outputs[0].items[0].data), "```felidae\n42\n```");
const factMarkdown = decoder.decode(data.cells[2].outputs[0].items[0].data);
check("rows of facts also get a table", factMarkdown.includes("| id | role |") && factMarkdown.includes("**Employee** · 3 rows"), true);
check("the error's message is what VS Code shows", JSON.parse(decoder.decode(data.cells[3].outputs[0].items[0].data)).message, "cell 4, line 1, column 5");
check("execution order is restored", data.cells.map((c) => c.executionSummary && c.executionSummary.executionOrder), [undefined, 1, 2, 3, undefined]);
check("a cell with an error output is marked as failed, the others as succeeded",
  data.cells.map((c) => c.executionSummary && c.executionSummary.success), [undefined, true, true, false, undefined]);

// ---- saving it again
const saved = serializer.serializeNotebook(data);
check("opening and saving a notebook changes nothing", parseNotebook(decoder.decode(saved)), stored);
check("a second round is byte-identical", decoder.decode(serializer.serializeNotebook(serializer.deserializeNotebook(saved))), decoder.decode(saved));

// ---- editing then saving
data.cells[4].value = "total + 1.";
data.cells[4].outputs = [valueOutput("43")];
data.cells[4].executionSummary = { executionOrder: 4, success: true };
const edited = parseNotebook(decoder.decode(serializer.serializeNotebook(data)));
check("a new output is saved as plain text", edited.cells[4].outputs, [{ text: "43", ok: true }]);
check("and its order", edited.cells[4].executionOrder, 4);
check("the edited source is saved", edited.cells[4].source, "total + 1.");
data.cells[1].outputs = [];
check("clearing outputs removes them from the file", parseNotebook(decoder.decode(serializer.serializeNotebook(data))).cells[1].outputs, undefined);

// ---- odd input
check("an output VS Code adds that we do not know is dropped, not saved as garbage",
  (() => { data.cells[4].outputs = [new NotebookCellOutput([NotebookCellOutputItem.text("{}", "application/json")])]; return parseNotebook(decoder.decode(serializer.serializeNotebook(data))).cells[4].outputs; })(), undefined);
check("an empty file opens as an empty notebook", serializer.deserializeNotebook(encoder.encode("")).cells.length, 0);
check("a damaged file is refused with its reason",
  (() => { try { serializer.deserializeNotebook(encoder.encode("{ nope")); return "opened"; } catch (e) { return /invalid JSON/.test(e.message); } })(), true);
check("a value containing a code fence is shown in a longer fence",
  decoder.decode(valueOutput("a ``` b").items[0].data).startsWith("````felidae"), true);

console.log(`${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
