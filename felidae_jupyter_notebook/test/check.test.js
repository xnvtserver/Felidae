const path = require("path");
const { planRun } = require(path.resolve(__dirname, "..", "out", "program.js"));
const { cellDiagnostics, planDiagnostics, tidyMessage } = require(path.resolve(__dirname, "..", "out", "check.js"));

let pass = 0, fail = 0;
const check = (name, actual, expected) => {
  const a = JSON.stringify(actual), e = JSON.stringify(expected);
  if (a === e) { pass++; console.log("  ok  ", name); } else { fail++; console.log("  FAIL", name, "\n     exp", e, "\n     act", a); }
};
const code = (source) => ({ kind: "code", source });
// A --check-json document with one diagnostic, as the interpreter prints it.
const report = (message, line, column, code = "syntax") => JSON.stringify({
  diagnostics: [{ code, end: { column, line }, message, severity: "error", start: { column, line } }], path: "x", symbols: [], version: 1
});
const spans = (list) => list.map((d) => [d.line + ":" + d.column + "-" + d.endLine + ":" + d.endColumn, d.message]);

// ---- a missing period: reported at the start of whatever comes next
const two = [code("def a := 1"), code("def twice(value: number) =>\n    value * 2.\nend")];
const first = planRun(two, 0);   // program: def a := 1 / blank / wrapper...
check("a missing period underlines the last character of the cell's last line",
  spans(cellDiagnostics(report("Expected '.' after global binding at line 5, column 1", 5, 1), first.lineMap, 0, two[0].source)),
  [["0:9-0:10", "Expected '.' after global binding"]]);
const second = planRun(two, 1);  // program: cell 0 / blank / cell 1 / blank
check("a missing period is marked so the quick fix can insert it at the end of the range",
  cellDiagnostics(report("Expected '.' after global binding at line 5, column 1", 5, 1), first.lineMap, 0, two[0].source).map((d) => d.missingPeriodAtEnd),
  [true]);
check("other errors are not marked",
  cellDiagnostics(report("Expected ')' after grouped expression at line 4, column 15", 4, 15), planRun([code("def total := 42."), code("total + (1.")], 1).lineMap, 1, "total + (1.").map((d) => d.missingPeriodAtEnd),
  [undefined]);
check("the same error is not blamed on the cell that follows",
  cellDiagnostics(report("Expected '.' after global binding at line 3, column 1", 3, 1), second.lineMap, 1, two[1].source), []);
check("it is blamed on the cell above, where the period is missing",
  spans(cellDiagnostics(report("Expected '.' after global binding at line 3, column 1", 3, 1), second.lineMap, 0, two[0].source)),
  [["0:9-0:10", "Expected '.' after global binding"]]);

// ---- an error inside an expression cell (wrapped in the result function)
const withQuery = [code("def total := 42."), code("total + (1.")];
const query = planRun(withQuery, 1);
check("an error in a query points at the right column of its own line",
  spans(cellDiagnostics(report("Expected ')' after grouped expression at line 4, column 15", 4, 15), query.lineMap, 1, withQuery[1].source)),
  [["0:14-0:15", "Expected ')' after grouped expression"]]);
check("an error in the cell's own text is not shown on another cell",
  cellDiagnostics(report("Expected ')' after grouped expression at line 4, column 15", 4, 15), query.lineMap, 0, withQuery[0].source), []);

// ---- what is not a cell problem, and what is not a check result
check("a missing init.fx is not underlined in the cell",
  cellDiagnostics(report("Felidae project requires init.fx beside the entry program: C:\\p\\init.fx", 1, 1, "infrastructure"), query.lineMap, 1, withQuery[1].source), []);
check("output that is not a check result is reported as such", cellDiagnostics("not json", query.lineMap, 1, ""), undefined);
check("no problems means no diagnostics", cellDiagnostics(JSON.stringify({ diagnostics: [] }), query.lineMap, 1, ""), []);
check("a position past the program is ignored", cellDiagnostics(report("oops at line 99, column 1", 99, 1), query.lineMap, 1, ""), []);

// ---- messages
check("the trailing position is dropped, the squiggle shows it", tidyMessage("Expected ')' at line 4, column 15", query.lineMap, 1), "Expected ')'");
const block = planRun([code("def f() =>\n    1.")], 0);
check("a position inside a message is mapped to the cell's own lines",
  tidyMessage("Missing end (block starts at line 1, column 1) at line 4, column 1", block.lineMap, 0), "Missing end (block starts at line 1, column 1)");

// ---- cells that cannot become a program
const messageOf = (cells, index) => planDiagnostics(planRun(cells, index)).map((entry) => entry.message);
check("a mixed cell is underlined with what to do", messageOf([code("def a := 1.\na.")], 0).map((m) => m.startsWith("A cell holds either declarations")), [true]);
check("an expression without a period is underlined", messageOf([code("total")], 0).map((m) => m.startsWith("An expression cell must end with")), [true]);
check("an empty cell is left alone", planDiagnostics(planRun([code("")], 0)), []);
check("a markdown cell is left alone", planDiagnostics(planRun([{ kind: "markdown", source: "# x" }], 0)), []);
check("a good cell has nothing to underline", planDiagnostics(planRun([code("def a := 1.")], 0)), []);

console.log(`${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
