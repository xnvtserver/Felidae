const path = require("path");
const { classifyCell, planRun, mapProgramErrors, exportFx, looksLikeManifest, describeCell } =
  require(path.resolve(__dirname, "..", "out", "program.js"));
const { planDiagnostics } = require(path.resolve(__dirname, "..", "out", "check.js"));

let pass = 0, fail = 0;
const check = (name, actual, expected) => {
  const a = JSON.stringify(actual), e = JSON.stringify(expected);
  if (a === e) { pass++; console.log("  ok  ", name); } else { fail++; console.log("  FAIL", name, "\n     exp", e, "\n     act", a); }
};
const code = (source) => ({ kind: "code", source });
const markdown = (source) => ({ kind: "markdown", source });

// ---------------------------------------------------------------- classification
check("a binding is a declaration", classifyCell("def total := 40 + 2."), "declaration");
check("a function is a declaration", classifyCell("def twice(value: number) =>\n    value * 2.\nend"), "declaration");
check("a class is a declaration", classifyCell("class Visit\n    key(id).\n    def id: string.\nend"), "declaration");
check("an import with defs is a declaration", classifyCell('import "helper.fx".\ndef a := 1.'), "declaration");
check("an import alone is a declaration", classifyCell('import "helper.fx".'), "declaration");
check("an annotated def is a declaration, not a stray expression",
  classifyCell("@mixfix(pattern: 'left plus right')\ndef plus(left: number, right: number) =>\n    left + right.\nend"), "declaration");
check("an annotation spanning several lines belongs to its def",
  classifyCell("@mixfix(\n    pattern: 'a b c',\n    note: \"x\"\n)\ndef f() =>\n    1.\nend"), "declaration");
check("annotations before several defs are all declarations",
  classifyCell("@mixfix(pattern: 'a')\ndef f() =>\n    1.\nend\n@mixfix(pattern: 'b')\ndef g() =>\n    2.\nend"), "declaration");
check("an annotated def followed by a query is a mixed cell",
  classifyCell("@mixfix(pattern: 'a')\ndef f() =>\n    1.\nend\nf()."), "mixed");
check("the names an annotated cell declares are listed", describeCell("@mixfix(pattern: 'a')\ndef f() =>\n    1.\nend").text, "declares f");
check("a bare call ending in a period is an expression", classifyCell("twice(value: 21)."), "expression");
check("a query over several lines is one expression", classifyCell("Employee.where(\n    role: \"x\"\n).count()."), "expression");
check("an empty cell is empty", classifyCell(""), "empty");
check("a comment-only cell is empty", classifyCell("# nothing here\n"), "empty");
check("a declaration followed by an expression is mixed", classifyCell("def a := 1.\na."), "mixed");
check("two expressions are mixed", classifyCell("a.\nb."), "mixed");

// ---------------------------------------------------------------- planning a run
const notebook = [
  markdown("# Notes"),                                                    // 0
  code("def total := 40 + 2."),                                           // 1
  code("def twice(value: number) =>\n    value * 2.\nend"),               // 2
  code("twice(value: total)."),                                           // 3 expression
  code("def label := \"x\".\ndef other := 1."),                          // 4 two bindings
  code("def mixed := 1.\nmixed."),                                        // 5 mixed
  code("total."),                                                         // 6 expression
  code("def late := 9.")                                                  // 7 declared below the others
];

const three = planRun(notebook, 3);
check("an expression cell runs through the generated result function", [three.ok, three.query, three.showsValue], [true, "felidae_cell_result().", true]);
check("the result function wraps the cell's expression, after the declarations above it",
  three.program.endsWith("def felidae_cell_result() =>\n    twice(value: total).\nend"), true);
check("the program holds those declaration cells in order", three.program.split("\n\n")[0], "def total := 40 + 2.");
check("the second declaration follows", three.program.includes("def twice(value: number) =>\n    value * 2.\nend"), true);
check("declarations below the cell are not in its program", three.program.includes("late"), false);
check("markdown, mixed and expression cells above add nothing", three.program.includes("mixed") || three.program.includes("Notes"), false);

const total = planRun(notebook, 1);
check("a single binding shows its value", [total.query, total.showsValue, total.defined], ["felidae_cell_result().", true, ["total"]]);
check("a binding's program is itself plus a function that reads it",
  total.program.trim(), "def total := 40 + 2.\n\ndef felidae_cell_result() =>\n    total.\nend");

const twice = planRun(notebook, 2);
check("a function only reports what it defined", [twice.query, twice.showsValue, twice.defined], ["true.", false, ["twice"]]);

const pair = planRun(notebook, 4);
check("several defs report all names", [pair.query, pair.showsValue, pair.defined], ["true.", false, ["label", "other"]]);

check("a repeated fact type is listed once", planRun([code("def Employee(id: \"e1\").\ndef Employee(id: \"e2\").")], 0).defined, ["Employee"]);
check("a fact is shown through its rows", planRun([code("def Employee(id: \"e1\").")], 0).program.endsWith("    Employee.all().\nend"), true);
check("a class is shown through its instances", planRun([code("class Visit\n    key(id).\nend")], 0).program.endsWith("    Visit.all().\nend"), true);

const bad = (cells, index) => { const plan = planRun(cells, index); return plan.ok ? "ok" : plan.message; };
check("markdown is not run", bad(notebook, 0), "Only code cells run.");
check("a mixed cell asks to be split", /Split it/.test(bad(notebook, 5)), true);
check("an empty cell is reported", bad([code("")], 0), "The cell is empty.");
check("an expression must end with a period", /must end with '\.'/.test(bad([code("total")], 0)), true);
check("a comment after the period is fine", planRun([code("total. # the answer")], 0).ok, true);
check("an out-of-range cell is not run", bad(notebook, 99), "Only code cells run.");

// ---------------------------------------------------------------- line map
// Program for cell 3: cell 1 (1 line) + blank, cell 2 (3 lines) + blank.
check("the line map has one entry per program line", three.lineMap.length, three.program.split("\n").length);
check("the first line comes from cell 1", three.lineMap[0], { cell: 1, line: 0 });
const multi = planRun([code("def a := 1."), code("Employee.where(\n    role: \"x\"\n).count().")], 1);
check("a multi-line expression keeps its own lines in the wrapper; the wrapper itself is marked generated",
  multi.lineMap.slice(-5), [
    { cell: 1, line: 0, generated: true },
    { cell: 1, line: 0 }, { cell: 1, line: 1 }, { cell: 1, line: 2 },
    { cell: 1, line: 2, generated: true }
  ]);
check("a binding's whole wrapper is generated, including the name it reads",
  planRun([code("def total := 42.")], 0).lineMap.slice(-3).every((origin) => origin.generated === true), true);
check("the blank line after a cell is marked so errors there can be placed at the cell's end",
  planRun([code("def a := 1.")], 0).lineMap[1].after, true);
check("an error on the expression's third line maps to cell 2, line 3",
  mapProgramErrors("C:\\p\\n.fxnb.fx: Expected ')' at line " + (multi.lineMap.length - 1) + ", column 1", multi.lineMap, "C:\\p\\n.fxnb.fx"),
  "Expected ')' at cell 2, line 3, column 1");
check("the function's lines come from cell 2", [three.lineMap[2], three.lineMap[3], three.lineMap[4]],
  [{ cell: 2, line: 0 }, { cell: 2, line: 1 }, { cell: 2, line: 2 }]);

const logical = "C:\\proj\\analysis.fxnb.fx";
check("an error in the program is rewritten to its cell and line",
  mapProgramErrors(logical + ": Expected '.' after statement at line 4, column 7", three.lineMap, logical),
  "Expected '.' after statement at cell 3, line 2, column 7");
check("every position in a message is rewritten",
  mapProgramErrors(logical + ": Missing end (block starts at line 3, column 1) at line 5, column 1", three.lineMap, logical),
  "Missing end (block starts at cell 3, line 1, column 1) at cell 3, line 3, column 1");
check("the file prefix matches regardless of slashes and case",
  mapProgramErrors("c:/PROJ/analysis.fxnb.fx: oops at line 1, column 2", three.lineMap, logical),
  "oops at cell 2, line 1, column 2");
check("an error about the query text is left as written",
  mapProgramErrors("query went wrong at line 1, column 3", three.lineMap, logical), "query went wrong at line 1, column 3");
check("an error from another file is left as written",
  mapProgramErrors("C:\\proj\\helper.fx: oops at line 2, column 1", three.lineMap, logical), "C:\\proj\\helper.fx: oops at line 2, column 1");
check("a line past the map is left as written",
  mapProgramErrors(logical + ": oops at line 99, column 1", three.lineMap, logical), "oops at line 99, column 1");

// ---------------------------------------------------------------- export
const exported = exportFx([
  markdown("# Title\n\nProse."),
  code("def total := 40 + 2."),
  code("total."),
  code("def a := 1.\na.")
]);
check("export keeps declarations and comments out prose and queries",
  exported,
  "# # Title\n#\n# Prose.\n\ndef total := 40 + 2.\n\n# query: total.\n\n# not exported (mixed cell): def a := 1.\n# a.\n");

// ---------------------------------------------------------------- init.fx lines pasted into a cell
const pasted = 'import "db".\r\ndb.location("build/examples/data")';
check("db.location in a cell is recognised as configuration", looksLikeManifest(pasted), true);
check("db.configure is configuration too", looksLikeManifest("db.configure({a: 1})."), true);
check("a name that merely contains db is not", looksLikeManifest("def mydb := 1.\nxdb.location_of(1)."), false);
const manifestPlan = planRun([code(pasted)], 0);
check("the pasted init.fx cell is refused for the right reason", [manifestPlan.ok, manifestPlan.reason], [false, "manifest"]);
check("and the message says where it belongs", /belongs in init\.fx beside the notebook/.test(manifestPlan.message), true);
check("it is underlined, not ignored", planDiagnostics(manifestPlan).length, 1);

// ---------------------------------------------------------------- what a cell says about itself
const say = (source) => { const d = describeCell(source); return d ? [d.text, d.warning] : d; };
check("a declaration cell lists what it declares", say("def total := 42.\ndef twice(value: number) =>\n    value * 2.\nend"), ["declares total, twice", false]);
check("a long list is shortened", say("def a := 1.\ndef b := 2.\ndef c := 3.\ndef d := 4."), ["declares a, b, c, …", false]);
check("a repeated fact type is listed once", say('def E(id: "1").\ndef E(id: "2").'), ["declares E", false]);
check("an import-only cell says so", say('import "helper.fx".'), ["imports", false]);
check("an expression is called a query", say("total + 1."), ["query", false]);
check("an unterminated expression warns", say("total"), ["$(warning) end with '.'", true]);
check("a mixed cell warns", say("def a := 1.\na."), ["$(warning) mixed cell", true]);
check("init.fx lines warn that they belong elsewhere", say(pasted), ["$(warning) belongs in init.fx", true]);
check("an empty cell says nothing", describeCell(""), undefined);
check("a comment-only cell says nothing", describeCell("# note"), undefined);

console.log(`${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
