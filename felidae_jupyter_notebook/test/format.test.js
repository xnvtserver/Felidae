const path = require("path");
const { parseNotebook, stringifyNotebook, emptyNotebook, NotebookFormatError } =
  require(path.resolve(__dirname, "..", "out", "format.js"));

let pass = 0, fail = 0;
const check = (name, actual, expected) => {
  const a = JSON.stringify(actual), e = JSON.stringify(expected);
  if (a === e) { pass++; console.log("  ok  ", name); } else { fail++; console.log("  FAIL", name, "\n     exp", e, "\n     act", a); }
};
const errorOf = (fn) => { try { fn(); return "no error"; } catch (e) { return e instanceof NotebookFormatError ? e.message : "other: " + e.message; } };

const sample = {
  version: 1,
  metadata: {},
  cells: [
    { kind: "markdown", source: "# Title\n\nSome prose.\n" },
    { kind: "code", source: "def total := 40 + 2.\n", executionOrder: 1, outputs: [{ text: "defined: total", ok: true }] },
    { kind: "code", source: "total.", outputs: [{ text: "42", ok: true }, { text: "", ok: true, metricsMarkdown: "**Metrics**\n" }] },
    { kind: "code", source: "oops(", outputs: [{ text: "cell 1, line 1, column 5", ok: false }] }
  ]
};

// ---- round trip
const text = stringifyNotebook(sample);
check("a notebook survives write then read unchanged", parseNotebook(text), sample);
check("a second write is byte-identical", stringifyNotebook(parseNotebook(text)), text);
check("the file ends with a newline", text.endsWith("\n"), true);

// ---- source is stored as lines, so a notebook diffs line by line
const stored = JSON.parse(text);
check("source is written as an array of lines keeping their newlines", stored.cells[0].source, ["# Title\n", "\n", "Some prose.\n"]);
check("a last line without a newline is kept as it is", stored.cells[2].source, ["total."]);
check("an empty source is an empty array", JSON.parse(stringifyNotebook({ version: 1, metadata: {}, cells: [{ kind: "code", source: "" }] })).cells[0].source, []);
check("CRLF sources round-trip exactly", parseNotebook(stringifyNotebook({ version: 1, metadata: {}, cells: [{ kind: "code", source: "a\r\nb\r\n" }] })).cells[0].source, "a\r\nb\r\n");
check("a cell without outputs writes no outputs key", "outputs" in stored.cells[0], false);

// ---- reading
check("a plain string source is accepted", parseNotebook('{"version":1,"cells":[{"kind":"code","source":"x."}]}').cells[0].source, "x.");
check("a zero-byte file is a new empty notebook", parseNotebook(""), emptyNotebook());
check("a whitespace-only file is a new empty notebook", parseNotebook("  \n"), emptyNotebook());
check("metadata defaults to an empty object", parseNotebook('{"version":1,"cells":[]}').metadata, {});

// ---- bad files are rejected with a reason, never repaired
check("invalid JSON is an error", /invalid JSON/.test(errorOf(() => parseNotebook("{ nope"))), true);
check("an array at the top level is an error", /top level must be an object/.test(errorOf(() => parseNotebook("[]"))), true);
check("a missing version is an error", /Unsupported .fxnb version undefined/.test(errorOf(() => parseNotebook('{"cells":[]}'))), true);
check("a future version is an error", /Unsupported .fxnb version 2/.test(errorOf(() => parseNotebook('{"version":2,"cells":[]}'))), true);
check("cells must be an array", /"cells" must be an array/.test(errorOf(() => parseNotebook('{"version":1,"cells":{}}'))), true);
check("an unknown cell kind names the cell", /cell 2: "kind" must be/.test(errorOf(() => parseNotebook('{"version":1,"cells":[{"kind":"code","source":""},{"kind":"raw","source":""}]}'))), true);
check("a non-text source is an error", /"source" must be a string or an array of strings/.test(errorOf(() => parseNotebook('{"version":1,"cells":[{"kind":"code","source":5}]}'))), true);
check("a malformed output names the cell and output", /cell 1, output 1/.test(errorOf(() => parseNotebook('{"version":1,"cells":[{"kind":"code","source":"","outputs":[{"text":1}]}]}'))), true);
check("a non-numeric executionOrder is an error", /"executionOrder" must be a number/.test(errorOf(() => parseNotebook('{"version":1,"cells":[{"kind":"code","source":"","executionOrder":"1"}]}'))), true);

console.log(`${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
