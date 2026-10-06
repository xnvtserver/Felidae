// The controller runs cells. It is exercised here end to end with a stand-in for
// VS Code's notebook classes and a stand-in for felidae: node itself is the
// "interpreter", and the notebook's logical program file (<notebook>.fxnb.fx) is
// a small script, so the controller spawns exactly the command line it would
// spawn for the real interpreter.
const fs = require("fs"), os = require("os"), path = require("path"), Module = require("module");

const encoder = new TextEncoder(), decoder = new TextDecoder();
class Item { constructor(data, mime) { this.data = data; this.mime = mime; }
  static text(v, mime = "text/plain") { return new Item(encoder.encode(v), mime); }
  static error(e) { return new Item(encoder.encode(JSON.stringify({ name: e.name, message: e.message })), "application/vnd.code.notebook.error"); } }
class Output { constructor(items) { this.items = items; } }
let settings = {};
let controllerObject;
const vscodeStub = {
  NotebookCellKind: { Markup: 1, Code: 2 },
  NotebookCellOutput: Output,
  NotebookCellOutputItem: Item,
  NotebookCellData: class {}, NotebookData: class {},
  workspace: { getConfiguration: () => ({ get: (key, fallback) => (key in settings ? settings[key] : fallback) }) },
  notebooks: {
    createNotebookController: () => {
      controllerObject = { dispose() {} };
      return controllerObject;
    }
  }
};
const stub = path.resolve(__dirname, "controller-stub.js");
const resolveOriginal = Module._resolveFilename;
Module._resolveFilename = function (request, ...rest) { return request === "vscode" ? stub : resolveOriginal.call(this, request, ...rest); };
require.cache[stub] = { id: stub, filename: stub, loaded: true, exports: vscodeStub };

const out = path.resolve(__dirname, "..", "out");
const { FelidaeNotebookController } = require(path.join(out, "controller.js"));

let pass = 0, fail = 0;
const check = (name, actual, expected) => {
  const a = JSON.stringify(actual), e = JSON.stringify(expected);
  if (a === e) { pass++; console.log("  ok  ", name); } else { fail++; console.log("  FAIL", name, "\n     exp", e, "\n     act", a); }
};

// ---- a project folder whose logical program file is the stand-in felidae
const folder = fs.mkdtempSync(path.join(os.tmpdir(), "fxnb-"));
const standIn = `
const path = require("path");
const args = process.argv.slice(2);
const query = args[args.indexOf("--query") + 1];
let program = "";
process.stdin.setEncoding("utf8");
process.stdin.on("data", (chunk) => (program += chunk));
process.stdin.on("end", () => {
  if (program.includes("MISSING_INIT")) { process.stderr.write("error: Felidae project requires init.fx beside the entry program: C:\\\\x\\\\init.fx\\n"); process.exit(1); }
  if (program.includes("OLD_BUILD")) { process.stderr.write("error: --stdin is valid only with --check-json\\n"); process.exit(1); }
  if (program.includes("BOOM")) { process.stderr.write("error: " + __filename + ": boom at line 3, column 2\\n"); process.exit(1); }
  console.log("file=" + path.basename(__filename) + " cwd=" + path.basename(process.cwd()) + " lines=" + program.split("\\n").length + " query=" + query);
  if (args.includes("--metrics-json")) process.stderr.write('FELIDAE_METRICS {"loadMs":10\\n,"executionMs":0.5\\n,"queryRuns":1\\n,"firstQueryMs":0.4\\n,"repeatedQueryAverageMs":0\\n,"runtime":{"durableStore":true,"clauseAttempts":1,"unificationAttempts":2,"factCandidates":0,"rocksFullScans":1,"rocksFactRowsScanned":50,"solutionMaterializations":1,"moduleLoads":1,"parserTokensLexed":10,"streamedModuleMicros":100,"dispatchCacheHits":1,"dispatchCacheMisses":0}}\\n');
});
`;
fs.writeFileSync(path.join(folder, "demo.fxnb.fx"), standIn);
const notebookFile = path.join(folder, "demo.fxnb");

// ---- fake notebook, cells and executions
const recorded = [];
const makeNotebook = (sources, extra = {}) => {
  const notebook = { uri: { scheme: "file", fsPath: notebookFile, path: notebookFile }, metadata: {}, ...extra };
  notebook.getCells = () => cells;
  const cells = sources.map((spec, index) => {
    const source = typeof spec === "string" ? spec : spec.source;
    const kind = typeof spec === "string" || spec.kind !== "markdown" ? 2 : 1;
    const cell = { kind, index, notebook, document: { getText: () => source }, log: { outputs: [], ended: undefined } };
    return cell;
  });
  notebook.cells = cells;
  return notebook;
};
let offers = { init: [], interpreter: 0 };
const host = (interpreterPath = process.execPath) => ({
  resolveInterpreter: () => ({ path: interpreterPath, source: "setting" }),
  describeMissingInterpreter: () => "no interpreter here",
  offerSelectInterpreter: () => { offers.interpreter++; },
  offerMissingInit: (folderArg) => { offers.init.push(folderArg); },
  log: () => undefined
});
const start = (interpreterPath) => {
  offers = { init: [], interpreter: 0 };
  settings = {};
  new FelidaeNotebookController({ subscriptions: [] }, host(interpreterPath));
  controllerObject.createNotebookCellExecution = (cell) => ({
    set executionOrder(value) { cell.log.order = value; },
    start() { cell.log.started = true; },
    async clearOutput() { cell.log.outputs = []; },
    async replaceOutput(outputs) { cell.log.outputs = outputs; },
    end(success) { cell.log.ended = success; }
  });
  return controllerObject;
};
const run = (notebook, indexes) => controllerObject.executeHandler(indexes.map((i) => notebook.cells[i]), notebook);
const plainOf = (cell) => decoder.decode(cell.log.outputs[0].items.find((item) => item.mime === "text/plain").data);
const errorOf = (cell) => JSON.parse(decoder.decode(cell.log.outputs[0].items[0].data)).message;

(async () => {
  // ---- a query over a declaration above it
  start();
  let notebook = makeNotebook(["def total := 42.", "total + 1."]);
  await run(notebook, [1]);
  let cell = notebook.cells[1];
  check("a query runs as one process and succeeds", cell.log.ended, true);
  check("the process is the notebook's logical program, in the notebook's folder",
    plainOf(cell).startsWith("file=demo.fxnb.fx cwd=" + path.basename(folder)), true);
  check("the declarations above are sent as the program, the query goes through the result function",
    /lines=\d+ query=felidae_cell_result\(\)\./.test(plainOf(cell)) && Number(/lines=(\d+)/.exec(plainOf(cell))[1]) > 3, true);
  check("a second output carries the metrics", [cell.log.outputs.length, decoder.decode(cell.log.outputs[1].items[0].data).startsWith("**Metrics**")], [2, true]);
  check("the metrics output warns about a full scan", decoder.decode(cell.log.outputs[1].items[0].data).includes("Scanned the whole fact store"), true);
  check("the cell got an execution order", typeof cell.log.order, "number");

  // ---- a declaration cell reports what it defined
  start();
  notebook = makeNotebook(["def twice(value: number) =>\n    value * 2.\nend"]);
  await run(notebook, [0]);
  check("a function cell says what it defined", plainOf(notebook.cells[0]), "defined: twice");

  // ---- metrics can be turned off
  start();
  settings = { showMetrics: false };
  notebook = makeNotebook(["total + 1."]);
  await run(notebook, [0]);
  check("with metrics turned off there is only the value", notebook.cells[0].log.outputs.length, 1);

  // ---- failures are mapped back to the cell and line
  start();
  notebook = makeNotebook(["def ok := 1.", "def bad := \"BOOM\"."]);
  await run(notebook, [1]);
  check("a failing cell ends unsuccessfully", notebook.cells[1].log.ended, false);
  check("its error names the cell and line it came from", errorOf(notebook.cells[1]), "boom at cell 2, line 1, column 2");

  // ---- Run All stops at the first failure
  start();
  notebook = makeNotebook(["def a := 1.", "def bad := \"BOOM\".", "a."]);
  await run(notebook, [0, 1, 2]);
  check("cells before the failure ran", notebook.cells[0].log.ended, true);
  check("the failing cell failed", notebook.cells[1].log.ended, false);
  check("cells after the failure did not run", notebook.cells[2].log.started, undefined);

  // ---- markdown is not run
  start();
  notebook = makeNotebook([{ kind: "markdown", source: "# note" }, "def a := 1."]);
  await run(notebook, [0, 1]);
  check("a markdown cell is skipped without an execution", [notebook.cells[0].log.started, notebook.cells[1].log.ended], [undefined, true]);

  // ---- cells that cannot become a program never start a process
  start();
  notebook = makeNotebook(["def a := 1.\na.", "total", 'import "db".\ndb.location("x")']);
  await run(notebook, [0]);
  check("a mixed cell is refused with advice", errorOf(notebook.cells[0]).startsWith("A cell holds either declarations"), true);
  await run(notebook, [1]);
  check("an expression without a period is refused", errorOf(notebook.cells[1]).startsWith("An expression cell must end with"), true);
  await run(notebook, [2]);
  check("init.fx lines in a cell are refused and sent to init.fx", /belongs in init\.fx beside the notebook/.test(errorOf(notebook.cells[2])), true);

  // ---- project problems come with an offer, not just a message
  start();
  notebook = makeNotebook(['def a := "MISSING_INIT".']);
  await run(notebook, [0]);
  check("a missing init.fx offers to create one in the project folder", offers.init, [folder]);
  check("and the error says what to do", /Create an init\.fx there/.test(errorOf(notebook.cells[0])), true);

  start();
  notebook = makeNotebook(['def a := "OLD_BUILD".']);
  await run(notebook, [0]);
  check("an interpreter that predates --stdin is named as such", /predates notebook support/.test(errorOf(notebook.cells[0])), true);

  start(path.join(folder, "no-such-felidae.exe"));
  notebook = makeNotebook(["def a := 1."]);
  await run(notebook, [0]);
  check("a missing interpreter offers the picker", offers.interpreter, 1);
  check("and the cell says so", errorOf(notebook.cells[0]), "no interpreter here");

  // ---- where the notebook lives
  start();
  notebook = makeNotebook(["def a := 1."], { uri: { scheme: "untitled", fsPath: "Untitled-1", path: "Untitled-1" } });
  await run(notebook, [0]);
  check("an unsaved notebook with no project folder says what to do", /not saved in a folder yet/.test(errorOf(notebook.cells[0])), true);

  const other = fs.mkdtempSync(path.join(os.tmpdir(), "fxnb-project-"));
  fs.writeFileSync(path.join(other, "untitled.fxnb.fx"), standIn);
  start();
  notebook = makeNotebook(["total + 1."], { uri: { scheme: "untitled", fsPath: "untitled.fxnb", path: "untitled.fxnb" }, metadata: { projectFolder: other } });
  await run(notebook, [0]);
  check("an unsaved notebook can run in the project folder named in its metadata",
    plainOf(notebook.cells[0]).startsWith("file=untitled.fxnb.fx cwd=" + path.basename(other)), true);

  fs.rmSync(folder, { recursive: true, force: true });
  fs.rmSync(other, { recursive: true, force: true });
  console.log(`${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
})();
