const path = require("path");
const { beautifyValue, factTable } = require(path.resolve(__dirname, "..", "out", "beautify.js"));

let pass = 0, fail = 0;
const check = (name, actual, expected) => {
  const a = JSON.stringify(actual), e = JSON.stringify(expected);
  if (a === e) { pass++; console.log("  ok  ", name); } else { fail++; console.log("  FAIL", name, "\n     exp", e, "\n     act", a); }
};

// Whitespace outside strings removed: beautifying may only change layout.
const squash = (text) => {
  let out = "", quote = "";
  for (let i = 0; i < text.length; i++) {
    const ch = text[i];
    if (quote) { out += ch; if (ch === "\\" && quote === '"') out += text[++i]; else if (ch === quote) quote = ""; }
    else if (ch === '"' || ch === "'") { quote = ch; out += ch; }
    else if (!/\s/.test(ch)) out += ch;
  }
  return out;
};

const rows = '[Employee(id: "e1", role: "dev"), Employee(id: "e2", role: "ops"), Employee(id: "e3", role: "qa")]';

// ---------------------------------------------------------------- beautifyValue
check("a short value is left alone", beautifyValue("42"), "42");
check("a line that fits is left alone", beautifyValue('[1, 2, 3]'), "[1, 2, 3]");
check("a long array of facts is broken one element per line", beautifyValue(rows, 60),
  '[\n  Employee(id: "e1", role: "dev"),\n  Employee(id: "e2", role: "ops"),\n  Employee(id: "e3", role: "qa")\n]');
check("only whitespace changed", squash(beautifyValue(rows, 60)), squash(rows));
check("beautifying twice changes nothing more", beautifyValue(beautifyValue(rows, 60), 60), beautifyValue(rows, 60));

const nested = '{name: "report", items: [Employee(id: "e1", role: "dev"), Employee(id: "e2", role: "ops")], total: 2}';
check("a nested value is broken at the level that is too long", beautifyValue(nested, 50),
  '{\n  name: "report",\n  items: [\n    Employee(id: "e1", role: "dev"),\n    Employee(id: "e2", role: "ops")\n  ],\n  total: 2\n}');
check("a nested value keeps its content", squash(beautifyValue(nested, 50)), squash(nested));

check("commas and brackets inside strings are not split",
  beautifyValue('["a, b, c", "close ) and ] here", "x"]', 20),
  '[\n  "a, b, c",\n  "close ) and ] here",\n  "x"\n]');
check("escaped quotes inside a string are kept", squash(beautifyValue('["say \\"hi, there\\"", "b"]', 10)), squash('["say \\"hi, there\\"", "b"]'));
check("an empty group stays on one line", beautifyValue("[" + "x".repeat(100) + ", []]", 40).includes("[]"), true);
check("each line of a multi-line value is handled on its own", beautifyValue('short\n' + rows, 60).split("\n")[0], "short");
check("unbalanced text is returned exactly as given", beautifyValue("[" + "a, ".repeat(40), 20), "[" + "a, ".repeat(40));
check("a long line with no brackets is left alone (trailing space aside)", beautifyValue("word ".repeat(40), 20), "word ".repeat(40).trimEnd());
check("an indented line keeps its indentation", beautifyValue("    [" + "x, ".repeat(12) + "x]", 30).startsWith("    ["), true);
check("an already beautified value is stable at a narrow width", (() => { const once = beautifyValue(nested, 30); return beautifyValue(once, 30) === once; })(), true);
check("the default width is 80 columns", beautifyValue(rows).includes("\n"), true);

// ---------------------------------------------------------------- factTable
const table = factTable(rows);
check("rows of one fact type become a table", table.split("\n"), [
  "**Employee** · 3 rows",
  "",
  "| id | role |",
  "| --- | --- |",
  '| "e1" | "dev" |',
  '| "e2" | "ops" |',
  '| "e3" | "qa" |'
]);
check("rows given one per line become a table too", factTable('Employee(id: "e1", role: "dev")\nEmployee(id: "e2", role: "ops")').includes("| id | role |"), true);
check("a single fact is a one-row table", factTable('Employee(id: "e1", role: "dev")').startsWith("**Employee** · 1 row\n"), true);
check("rows with different keys share the union of columns", factTable('[T(a: 1), T(b: 2)]').split("\n")[2], "| a | b |");
check("a missing key leaves its cell empty", factTable('[T(a: 1), T(b: 2)]').split("\n").slice(4), ["| 1 |  |", "|  | 2 |"]);
check("a pipe inside a value is escaped", factTable('[T(a: "x|y")]').includes('"x\\|y"'), true);
check("mixed fact types are not a table", factTable('[A(x: 1), B(x: 2)]'), undefined);
check("a nested value in a row is not a table", factTable('[T(a: [1, 2])]'), undefined);
check("a scalar is not a table", factTable("42"), undefined);
check("an array of scalars is not a table", factTable("[1, 2, 3]"), undefined);
check("an empty array is not a table", factTable("[]"), undefined);
check("text that is not a value is not a table", factTable("hello world"), undefined);

console.log(`${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
