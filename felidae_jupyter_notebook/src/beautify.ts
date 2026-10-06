// Beautifying a result for display. The interpreter prints a value on one line,
// so a query over facts comes back as one very long line:
//
//   [Employee(id: "e1", role: "dev"), Employee(id: "e2", role: "ops")]
//
// beautifyValue breaks lines longer than the width one element per line,
// indented, and leaves short lines alone. factTable turns rows of facts into a
// markdown table. Both are cosmetic: only whitespace outside strings changes,
// and anything they cannot parse is returned exactly as given.
//
// No vscode import, so it is unit-testable.

interface Group {
  start: number; // index of the opening bracket
  end: number; // index of the closing bracket
  items: Item[];
}

interface Item {
  start: number;
  end: number; // exclusive, trailing whitespace trimmed
  parts: Part[];
}

type Part = { kind: "text"; start: number; end: number } | { kind: "group"; group: Group };

const OPENERS = "([{";
const CLOSERS = ")]}";

class Unbalanced extends Error {}

// Index just past the string or atom that starts at `start` (a " or ' quote).
function skipQuoted(text: string, start: number): number {
  const quote = text[start];
  for (let i = start + 1; i < text.length; i++) {
    if (text[i] === "\\" && quote === '"') i++;
    else if (text[i] === quote) return i + 1;
  }
  throw new Unbalanced();
}

// Parses one level. A group splits its items on commas; the top level does not.
function parseLevel(text: string, from: number, closer: string | undefined): { items: Item[]; next: number } {
  const items: Item[] = [];
  let parts: Part[] = [];
  let itemStart = -1;
  let itemEnd = -1;

  const addText = (start: number, end: number) => {
    const last = parts[parts.length - 1];
    if (last && last.kind === "text" && last.end === start) last.end = end;
    else parts.push({ kind: "text", start, end });
  };
  const touch = (start: number, end: number) => {
    if (itemStart < 0) itemStart = start;
    itemEnd = end;
  };
  const finishItem = () => {
    if (itemStart >= 0) items.push({ start: itemStart, end: itemEnd, parts });
    parts = [];
    itemStart = -1;
    itemEnd = -1;
  };

  let i = from;
  while (i < text.length) {
    const ch = text[i];
    if (ch === '"' || ch === "'") {
      const next = skipQuoted(text, i);
      addText(i, next);
      touch(i, next);
      i = next;
    } else if (OPENERS.includes(ch)) {
      const expected = CLOSERS[OPENERS.indexOf(ch)];
      const inner = parseLevel(text, i + 1, expected);
      const group: Group = { start: i, end: inner.next - 1, items: inner.items };
      parts.push({ kind: "group", group });
      touch(i, inner.next);
      i = inner.next;
    } else if (closer !== undefined && ch === closer) {
      finishItem();
      return { items, next: i + 1 };
    } else if (CLOSERS.includes(ch)) {
      throw new Unbalanced();
    } else if (ch === "," && closer !== undefined) {
      finishItem();
      i++;
    } else {
      addText(i, i + 1);
      if (!/\s/.test(ch)) touch(i, i + 1);
      i++;
    }
  }
  if (closer !== undefined) throw new Unbalanced();
  finishItem();
  return { items, next: i };
}

const pad = (width: number) => " ".repeat(width);

function layoutGroup(text: string, group: Group, indent: number, column: number, tail: number, width: number): string {
  const flat = text.slice(group.start, group.end + 1);
  if (group.items.length === 0 || column + flat.length + tail <= width) return flat;
  const lines = group.items.map((item, index) =>
    pad(indent + 2) + layoutItem(text, item, indent + 2, index < group.items.length - 1 ? 1 : 0, width)
  );
  return text[group.start] + "\n" + lines.join(",\n") + "\n" + pad(indent) + text[group.end];
}

function layoutItem(text: string, item: Item, indent: number, trailing: number, width: number): string {
  const flat = text.slice(item.start, item.end);
  if (indent + flat.length + trailing <= width || !item.parts.some((part) => part.kind === "group")) return flat;
  let out = "";
  let column = indent;
  for (const part of item.parts) {
    if (part.kind === "text") {
      const piece = text.slice(Math.max(part.start, item.start), Math.min(part.end, item.end));
      const shown = out === "" ? piece.replace(/^\s+/, "") : piece;
      out += shown;
      column += shown.length;
    } else {
      const tail = item.end - (part.group.end + 1) + trailing;
      const laidOut = layoutGroup(text, part.group, indent, column, tail, width);
      out += laidOut;
      column += laidOut.includes("\n") ? laidOut.length - laidOut.lastIndexOf("\n") - 1 : laidOut.length;
    }
  }
  return out;
}

function beautifyLine(line: string, width: number): string {
  if (line.length <= width) return line;
  try {
    // A line that is already indented (part of an earlier layout) keeps its
    // indentation and breaks relative to it, which keeps beautifying idempotent.
    const lead = line.length - line.trimStart().length;
    const { items } = parseLevel(line, 0, undefined);
    if (items.length !== 1) return line;
    return pad(lead) + layoutItem(line, items[0], lead, 0, width);
  } catch {
    return line;
  }
}

// Lines longer than `width` are broken inside their brackets, one element per
// line; each line of a multi-line value is handled on its own.
export function beautifyValue(text: string, width = 80): string {
  return text.split("\n").map((line) => beautifyLine(line, width)).join("\n");
}

// Rows of facts, `[T(k: v, ...), T(...)]` or one `T(...)` per line, as a
// markdown table. Undefined when the value is not that shape (a nested value
// inside a row, mixed fact types, a single scalar, ...).
export function factTable(text: string): string | undefined {
  const rows: string[] = [];
  const trimmed = text.trim();
  try {
    if (trimmed.startsWith("[")) {
      const { items } = parseLevel(trimmed, 0, undefined);
      if (items.length !== 1 || items[0].parts.length !== 1 || items[0].parts[0].kind !== "group") return undefined;
      for (const item of items[0].parts[0].group.items) rows.push(trimmed.slice(item.start, item.end));
    } else {
      for (const line of trimmed.split("\n")) if (line.trim() !== "") rows.push(line.trim());
    }
  } catch {
    return undefined;
  }
  if (rows.length === 0) return undefined;

  const typeNames = new Set<string>();
  const columns: string[] = [];
  const cells: Array<Record<string, string>> = [];
  for (const row of rows) {
    const open = row.indexOf("(");
    if (open <= 0 || !row.endsWith(")") || !/^[A-Za-z_][A-Za-z0-9_.:]*$/.test(row.slice(0, open))) return undefined;
    typeNames.add(row.slice(0, open));
    let args: Item[];
    try {
      const parsed = parseLevel(row.slice(open + 1), 0, ")");
      args = parsed.items;
      if (row.slice(open + 1).length !== parsed.next) return undefined;
    } catch {
      return undefined;
    }
    const body = row.slice(open + 1);
    const record: Record<string, string> = {};
    for (const arg of args) {
      if (arg.parts.some((part) => part.kind === "group")) return undefined;
      const entry = body.slice(arg.start, arg.end);
      const colon = entry.indexOf(":");
      if (colon <= 0) return undefined;
      const key = entry.slice(0, colon).trim();
      if (!columns.includes(key)) columns.push(key);
      record[key] = entry.slice(colon + 1).trim();
    }
    cells.push(record);
  }
  if (typeNames.size !== 1 || columns.length === 0) return undefined;

  const escape = (value: string) => value.replace(/\|/g, "\\|");
  const header = "| " + columns.map(escape).join(" | ") + " |";
  const rule = "| " + columns.map(() => "---").join(" | ") + " |";
  const body = cells.map((record) => "| " + columns.map((column) => escape(record[column] ?? "")).join(" | ") + " |");
  const title = "**" + [...typeNames][0] + "** · " + cells.length + (cells.length === 1 ? " row" : " rows");
  return [title, "", header, rule, ...body].join("\n");
}
