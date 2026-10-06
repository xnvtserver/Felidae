// How a notebook becomes programs. There is no kernel: running a cell starts
// one felidae process whose program text is assembled from the notebook.
//
//   declaration cell   top-level def / class / import statements
//   expression cell    exactly one expression ending in '.', a query
//
// Running cell N: the program is every declaration cell above N (plus N when it
// is a declaration), in order. A cell that produces a value (an expression, or
// a declaration of one binding, fact or class) is wrapped in a generated
// function, `felidae_cell_result`, appended to the program and called with
// `--query`. The wrapper matters: a bare query is parsed on its own, where a
// name bound by an earlier `def` is just an atom (`total.` prints `total`),
// while inside the program the same name resolves to its value. It also means
// an error in the cell carries a file and line like any other program error.
// A declaration cell that defines functions (or several things) only reports
// what it defined. State between cells lives only in the database, as in the
// language.
//
// No vscode import, so it is unit-testable.

import { endBlockPairs } from "./blocks";
import { Cell, bracketDelta, buildCellExpression, splitCells, stripComment } from "./cells";

export interface CellInput {
  kind: "code" | "markdown";
  source: string;
}

export type CellClass = "empty" | "declaration" | "expression" | "mixed";

// `db.location(...)` and `db.configure(...)` are project configuration. They
// belong in init.fx beside the notebook; in a cell they would do nothing and
// the database would still come from init.fx.
export function looksLikeManifest(source: string): boolean {
  return /(^|\n)[ \t]*db\.(location|configure)\s*\(/.test(source);
}

const MANIFEST_MESSAGE =
  "db.location(...) is project configuration. It belongs in init.fx beside the notebook, not in a cell: run 'Felidae: Create init.fx for Notebook' (or edit init.fx), then delete this cell.";

interface Analysis {
  cells: Cell[];
  // import statements and annotations such as @mixfix(...): declarations that
  // are not a def or class themselves.
  imports: number;
  expressions: number;
}

const linesOf = (source: string): string[] => source.replace(/\r\n/g, "\n").split("\n");

// Walks the top-level statements of a cell: defs and classes (via splitCells),
// imports, and anything else, which is an expression.
function analyze(source: string): Analysis {
  const lines = linesOf(source);
  const cells = splitCells(lines, endBlockPairs(lines));
  const covered = new Set<number>();
  for (const cell of cells) for (let line = cell.startLine; line <= cell.endLine; line++) covered.add(line);

  let imports = 0;
  let expressions = 0;
  let line = 0;
  while (line < lines.length) {
    const code = stripComment(lines[line]);
    if (covered.has(line) || code.trim() === "") {
      line++;
      continue;
    }
    // An annotation (`@mixfix(...)`) sits directly above the def it applies to
    // and has no closing period: it ends with its last bracket, not at a '.'.
    if (/^\s*@/.test(code)) {
      let end = line;
      let depth = bracketDelta(code);
      while (end + 1 < lines.length && depth > 0) {
        end++;
        depth += bracketDelta(stripComment(lines[end]));
      }
      imports++;
      line = end + 1;
      continue;
    }
    // A statement ends at the first line ending in '.' with every bracket closed.
    let end = line;
    let depth = bracketDelta(code);
    while (end + 1 < lines.length && !(depth <= 0 && /\.\s*$/.test(stripComment(lines[end])))) {
      end++;
      depth += bracketDelta(stripComment(lines[end]));
    }
    if (/^\s*import\b/.test(code)) imports++;
    else expressions++;
    line = end + 1;
  }
  return { cells, imports, expressions };
}

export function classifyCell(source: string): CellClass {
  const { cells, imports, expressions } = analyze(source);
  const declarations = cells.length + imports;
  if (declarations === 0 && expressions === 0) return "empty";
  if (expressions === 0) return "declaration";
  if (declarations === 0 && expressions === 1) return "expression";
  return "mixed";
}

// Where a line of the assembled program came from.
export interface LineOrigin {
  cell: number;
  line: number;
  // True for the blank line inserted after a cell. The interpreter reports
  // "expected '.'" at the start of the line after a cell's last token, which
  // lands here; it belongs to the end of the cell's last line.
  after?: boolean;
  // True for lines the extension wrote itself (the result function around a
  // cell's value). An error is never about these; the nearest real line is.
  generated?: boolean;
}

// The generated function that returns a cell's value.
export const RESULT_FUNCTION = "felidae_cell_result";

export type RunPlan =
  | {
      ok: true;
      program: string;
      lineMap: LineOrigin[];
      // The expression passed to --query: the result function, or `true.` when
      // loading the program is the whole check.
      query: string;
      // Names a declaration cell defines (empty for an expression cell).
      defined: string[];
      // True when the query's value is the result to show.
      showsValue: boolean;
    }
  // `reason` lets the editor underline a cell that is wrong (mixed, unterminated)
  // and stay quiet about one that is just not runnable (markdown, empty).
  | { ok: false; message: string; reason: "markdown" | "empty" | "mixed" | "unterminated" | "manifest" };

const MIXED_MESSAGE =
  "A cell holds either declarations (def, class, import) or a single expression ending in '.'. Split it into separate cells.";

export function planRun(cells: readonly CellInput[], index: number): RunPlan {
  const target = cells[index];
  if (!target || target.kind !== "code") return { ok: false, message: "Only code cells run.", reason: "markdown" };
  const targetClass = classifyCell(target.source);
  if (targetClass === "empty") return { ok: false, message: "The cell is empty.", reason: "empty" };
  if (looksLikeManifest(target.source)) return { ok: false, message: MANIFEST_MESSAGE, reason: "manifest" };
  if (targetClass === "mixed") return { ok: false, message: MIXED_MESSAGE, reason: "mixed" };

  // The program: declaration cells above, then the target when it declares.
  const included: number[] = [];
  for (let j = 0; j < index; j++) {
    if (cells[j].kind === "code" && classifyCell(cells[j].source) === "declaration") included.push(j);
  }
  if (targetClass === "declaration") included.push(index);

  const lines: string[] = [];
  const lineMap: LineOrigin[] = [];
  // Appends `def felidae_cell_result() => <expression> end`, every line mapped
  // to the cell line it came from (a generated expression maps to line 0).
  // `synthesized` is true when the expression is ours (a binding's name), false
  // when it is the cell's own text.
  const wrap = (expressionLines: string[], synthesized: boolean) => {
    lines.push("def " + RESULT_FUNCTION + "() =>");
    lineMap.push({ cell: index, line: 0, generated: true });
    expressionLines.forEach((text, line) => {
      lines.push("    " + text);
      lineMap.push(synthesized ? { cell: index, line, generated: true } : { cell: index, line });
    });
    lines.push("end");
    lineMap.push({ cell: index, line: Math.max(0, expressionLines.length - 1), generated: true });
  };
  for (const j of included) {
    const own = linesOf(cells[j].source.replace(/\s+$/, ""));
    own.forEach((text, line) => {
      lines.push(text);
      lineMap.push({ cell: j, line });
    });
    // A blank line between cells keeps the map one entry per line.
    lines.push("");
    lineMap.push({ cell: j, line: own.length - 1, after: true });
  }

  if (targetClass === "expression") {
    const expression = target.source.trim();
    const lastCode = linesOf(expression).map(stripComment).filter((text) => text.trim() !== "").pop() ?? "";
    if (!/\.\s*$/.test(lastCode)) {
      return { ok: false, message: "An expression cell must end with '.', for example: total.", reason: "unterminated" };
    }
    wrap(linesOf(expression), false);
    return { ok: true, program: lines.join("\n"), lineMap, query: RESULT_FUNCTION + "().", defined: [], showsValue: true };
  }

  const own = analyze(target.source);
  // A fact type is declared once per row; list each name once.
  const defined = [...new Set(own.cells.map((cell) => cell.name))];
  const single = own.cells.length === 1 && own.imports === 0 ? own.cells[0] : undefined;
  if (single && (single.kind === "binding" || single.kind === "fact" || single.kind === "class")) {
    wrap([buildCellExpression(single)], true);
    return { ok: true, program: lines.join("\n"), lineMap, query: RESULT_FUNCTION + "().", defined, showsValue: true };
  }
  // Loading the program is the check; `true.` is the cheapest query to run.
  return { ok: true, program: lines.join("\n"), lineMap, query: "true.", defined, showsValue: false };
}

// One line about a cell for its status bar: what it is, or what is wrong with it.
// Undefined for an empty cell, which has nothing to say.
export function describeCell(source: string): { text: string; tooltip: string; warning: boolean } | undefined {
  const kind = classifyCell(source);
  if (kind === "empty") return undefined;
  if (looksLikeManifest(source)) {
    return { text: "$(warning) belongs in init.fx", tooltip: MANIFEST_MESSAGE, warning: true };
  }
  if (kind === "mixed") {
    return { text: "$(warning) mixed cell", tooltip: MIXED_MESSAGE, warning: true };
  }
  if (kind === "expression") {
    const lastCode = linesOf(source.trim()).map(stripComment).filter((text) => text.trim() !== "").pop() ?? "";
    if (!/\.\s*$/.test(lastCode)) {
      return { text: "$(warning) end with '.'", tooltip: "An expression cell must end with '.', for example: total.", warning: true };
    }
    return { text: "query", tooltip: "One expression: its value is shown when the cell runs.", warning: false };
  }
  const names = [...new Set(analyze(source).cells.map((cell) => cell.name))];
  const shown = names.length > 3 ? names.slice(0, 3).join(", ") + ", …" : names.join(", ");
  return {
    text: names.length > 0 ? "declares " + shown : "imports",
    tooltip: "Declarations build the program the cells below run in.",
    warning: false
  };
}

// Interpreter errors about the assembled program name the logical file and a
// line in it ("<file>: ... at line 5, column 2"). Rewrite them to the cell the
// line came from. An error without that prefix is about the query text, whose
// lines are already the cell's own, so it is left as written.
export function mapProgramErrors(message: string, lineMap: readonly LineOrigin[], logicalPath: string): string {
  const same = (text: string) => text.replace(/\\/g, "/").toLowerCase();
  const prefix = logicalPath + ": ";
  if (!same(message).startsWith(same(prefix))) return message;
  return message.slice(prefix.length).replace(/line (\d+), column (\d+)/g, (whole, line: string, column: string) => {
    const origin = lineMap[Number(line) - 1];
    return origin ? "cell " + (origin.cell + 1) + ", line " + (origin.line + 1) + ", column " + column : whole;
  });
}

// The notebook as a plain .fx file: declarations as written, prose and
// queries as comments (a bare top-level expression would run when the file loads).
export function exportFx(cells: readonly CellInput[]): string {
  const comment = (text: string, label = "") =>
    linesOf(text.replace(/\s+$/, ""))
      .map((line, i) => (i === 0 && label ? "# " + label + line : line === "" ? "#" : "# " + line))
      .join("\n");
  const parts = cells.map((cell) => {
    if (cell.kind === "markdown") return comment(cell.source);
    const kind = classifyCell(cell.source);
    if (kind === "declaration") return cell.source.replace(/\s+$/, "");
    if (kind === "expression") return comment(cell.source, "query: ");
    if (kind === "mixed") return comment(cell.source, "not exported (mixed cell): ");
    return "";
  });
  return parts.filter((part) => part !== "").join("\n\n") + "\n";
}
