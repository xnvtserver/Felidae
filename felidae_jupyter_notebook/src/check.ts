// Parse errors for one cell, from `felidae --check-json --stdin`.
//
// The check parses the same assembled program a run would use (the declaration
// cells above the cell, plus the cell, with an expression wrapped in the result
// function), so a cell is judged in its context: an operator declared above
// still parses below it. Positions in that program are mapped back through the
// line map; an error that belongs to another cell is left to that cell's own
// check. Parsing only: nothing is executed and the database is not opened.
//
// No vscode import, so it is unit-testable.

import { LineOrigin, RunPlan } from "./program";

export interface CellDiagnostic {
  // 0-based, within the cell.
  line: number;
  column: number;
  endLine: number;
  endColumn: number;
  severity: "error" | "warning" | "info" | "hint";
  message: string;
  // True for a missing period: the range ends where the period belongs, so a
  // quick fix can insert it at the end of the range.
  missingPeriodAtEnd?: boolean;
}

// The interpreter ends a message with where it happened ("... at line 4, column
// 15"); the squiggle already shows that, so it is dropped. A position inside
// the message, such as "(block starts at line 3, column 1)", is mapped to the
// cell's own lines.
export function tidyMessage(message: string, lineMap: readonly LineOrigin[], cellIndex: number): string {
  const withoutSuffix = message.replace(/\s+at line \d+, column \d+\s*$/, "");
  return withoutSuffix.replace(/line (\d+), column (\d+)/g, (whole, line: string, column: string) => {
    const origin = lineMap[Number(line) - 1];
    return origin && origin.cell === cellIndex ? "line " + (origin.line + 1) + ", column " + column : whole;
  });
}

// `stdout` is the --check-json document. Returns undefined when it is not one.
export function cellDiagnostics(
  stdout: string,
  lineMap: readonly LineOrigin[],
  cellIndex: number,
  cellText = ""
): CellDiagnostic[] | undefined {
  let document: { diagnostics?: unknown };
  try {
    document = JSON.parse(stdout);
  } catch {
    return undefined;
  }
  if (!Array.isArray(document.diagnostics)) return undefined;

  const result: CellDiagnostic[] = [];
  for (const entry of document.diagnostics as Array<Record<string, any>>) {
    // Project and file problems (missing init.fx, unreadable import) are not
    // about this cell's text; running the cell reports them.
    if (entry.code === "infrastructure") continue;
    const start = entry.start as { line?: number; column?: number } | undefined;
    if (!start || typeof start.line !== "number" || typeof start.column !== "number") continue;
    const rawMessage = String(entry.message ?? "Felidae diagnostic");
    let origin = lineMap[start.line - 1];
    let atEndOfLine = !!origin?.after;
    // "Expected '.'" is reported where the parser noticed: the start of the
    // next statement, or of a generated line after the cell. The missing period
    // belongs at the end of the last real line before that.
    if (/^Expected '\./.test(rawMessage) && start.column === 1) {
      let previous = start.line - 2;
      while (previous >= 0 && (lineMap[previous].after || lineMap[previous].generated)) previous--;
      if (previous >= 0) {
        origin = lineMap[previous];
        atEndOfLine = true;
      }
    }
    if (!origin || origin.cell !== cellIndex) continue;

    const end = entry.end as { line?: number; column?: number } | undefined;
    const endOrigin = end && typeof end.line === "number" ? lineMap[end.line - 1] : undefined;
    const sameCellEnd = !atEndOfLine && endOrigin && endOrigin.cell === cellIndex && end && typeof end.column === "number";
    const line = origin.line;
    // An error reported on the separator after the cell is about the end of its last line.
    const lastLine = cellText.replace(/\r\n/g, "\n").replace(/\s+$/, "").split("\n")[line] ?? "";
    const column = atEndOfLine ? Math.max(0, lastLine.length - 1) : Math.max(0, start.column - 1);
    let endLine = sameCellEnd ? (endOrigin as LineOrigin).line : line;
    let endColumn = sameCellEnd ? Math.max(0, (end as { column: number }).column - 1) : atEndOfLine ? Math.max(column + 1, lastLine.length) : column + 1;
    // A zero-width range would draw nothing: widen it by one character.
    if (endLine === line && endColumn <= column) endColumn = column + 1;
    if (endLine < line) endLine = line;

    const severity = entry.severity === "warning" || entry.severity === "info" || entry.severity === "hint" ? entry.severity : "error";
    result.push({
      line,
      column,
      endLine,
      endColumn,
      severity,
      message: tidyMessage(rawMessage, lineMap, cellIndex),
      ...(atEndOfLine && /^Expected '\./.test(rawMessage) ? { missingPeriodAtEnd: true } : {})
    });
  }
  return result;
}

// What to underline when the cell cannot even be turned into a program:
// the whole first line. Markdown and empty cells get nothing.
export function planDiagnostics(plan: RunPlan): CellDiagnostic[] {
  if (plan.ok) return [];
  if (plan.reason === "markdown" || plan.reason === "empty") return [];
  return [{ line: 0, column: 0, endLine: 0, endColumn: 1, severity: "error", message: plan.message }];
}
