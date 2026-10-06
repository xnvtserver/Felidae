// Copied from vs-code-extension/src/cells.ts (same cell rules as the Felidae extension).
// The only change: stripComment and bracketDelta are exported for program.ts.

// Cells: the units a file is run in, one top-level declaration at a time.
//
// Pure text analysis (no vscode import) so it can be unit-tested. A cell is a
// column-0 `def` or `class`; everything indented belongs to the cell above it.
//
//   def f(a: number) => ... end   function  (block, ends at its `end`)
//   def Name(...).                fact      (persistent seed, ends at its '.')
//   def x := expr.  def x: T.     binding   (ends at its '.')
//   class Name ... end            class
//   def main() => ... end         entry     (run through the normal Run/Debug)

export type CellKind = "function" | "fact" | "binding" | "class" | "entry";

export interface CellParam {
  name: string;
  type?: string;
}

export interface Cell {
  kind: CellKind;
  name: string;
  params: CellParam[];
  // The line holding "def name": where the lens and the inline result go.
  headerLine: number;
  // First line of the cell including a comment block directly above it.
  startLine: number;
  endLine: number;
}

// Only the block boundaries are needed, so this module does not depend on the
// extension's own pair type.
export interface BlockPair {
  openerLine: number;
  endLine: number;
}

// A ':' or '.' belongs to a name only when an identifier follows it, so
// "def label: string" names `label`, not `label:`.
const NAME = "[A-Za-z_][A-Za-z0-9_]*(?:[:.][A-Za-z_][A-Za-z0-9_]*)*";
const DEF_HEAD = new RegExp("^def\\s+(" + NAME + ")");
const CLASS_HEAD = /^class\s+([A-Za-z_][A-Za-z0-9_.]*)/;

// The line without its trailing `# comment`, ignoring a # inside a string.
export function stripComment(line: string): string {
  let inString = false;
  for (let i = 0; i < line.length; i++) {
    const ch = line[i];
    if (inString) {
      if (ch === "\\") i++;
      else if (ch === '"') inString = false;
    } else if (ch === '"') {
      inString = true;
    } else if (ch === "#") {
      return line.slice(0, i);
    }
  }
  return line;
}

// Net open brackets on a line, ignoring brackets inside strings.
export function bracketDelta(code: string): number {
  let depth = 0;
  let inString = false;
  for (let i = 0; i < code.length; i++) {
    const ch = code[i];
    if (inString) {
      if (ch === "\\") i++;
      else if (ch === '"') inString = false;
    } else if (ch === '"') {
      inString = true;
    } else if ("([{".includes(ch)) {
      depth++;
    } else if (")]}".includes(ch)) {
      depth--;
    }
  }
  return depth;
}

// "(a: number, b)" -> [{name:"a",type:"number"},{name:"b"}]
function parseParams(head: string): CellParam[] {
  const open = head.indexOf("(");
  if (open < 0) return [];
  let depth = 0;
  let close = -1;
  for (let i = open; i < head.length; i++) {
    if (head[i] === "(") depth++;
    else if (head[i] === ")" && --depth === 0) {
      close = i;
      break;
    }
  }
  if (close < 0) return [];
  const inside = head.slice(open + 1, close);
  const parts: string[] = [];
  let level = 0;
  let current = "";
  for (const ch of inside) {
    if ("([{<".includes(ch)) level++;
    else if (")]}>".includes(ch)) level--;
    if (ch === "," && level === 0) {
      parts.push(current);
      current = "";
    } else {
      current += ch;
    }
  }
  if (current.trim()) parts.push(current);
  return parts
    .map((part) => part.trim())
    .filter((part) => part.length > 0)
    .map((part) => {
      const colon = part.indexOf(":");
      return colon < 0
        ? { name: part }
        : { name: part.slice(0, colon).trim(), type: part.slice(colon + 1).trim() || undefined };
    });
}

// The header of a block-opening def, which may continue over several lines
// until the `=>`.
function blockHead(lines: readonly string[], line: number): string {
  let head = "";
  for (let i = line; i < lines.length && i <= line + 32; i++) {
    head += " " + stripComment(lines[i]);
    if (/=>\s*(?:\(\s*\))?\s*$/.test(head)) break;
  }
  return head;
}

export function splitCells(lines: readonly string[], pairs: readonly BlockPair[]): Cell[] {
  const endOfBlock = new Map<number, number>();
  for (const pair of pairs) endOfBlock.set(pair.openerLine, pair.endLine);

  const cells: Cell[] = [];
  let line = 0;
  while (line < lines.length) {
    const text = lines[line];
    const code = stripComment(text);
    // Only a column-0 statement starts a cell; indented lines belong to one.
    if (code.trim() === "" || /^\s/.test(text)) {
      line++;
      continue;
    }

    let cell: Cell | undefined;
    let next = line + 1;
    const classMatch = CLASS_HEAD.exec(code);
    const defMatch = DEF_HEAD.exec(code);
    if (classMatch) {
      const end = endOfBlock.get(line) ?? line;
      cell = { kind: "class", name: classMatch[1], params: [], headerLine: line, startLine: line, endLine: end };
      next = end + 1;
    } else if (defMatch) {
      const blockEnd = endOfBlock.get(line);
      if (blockEnd !== undefined) {
        const name = defMatch[1];
        cell = {
          kind: name === "main" ? "entry" : "function",
          name,
          params: parseParams(blockHead(lines, line)),
          headerLine: line,
          startLine: line,
          endLine: blockEnd
        };
        next = blockEnd + 1;
      } else {
        // A statement: it ends at the first line that ends in '.' with every
        // bracket closed (a persistent fact may span several lines).
        let end = line;
        let depth = bracketDelta(code);
        while (end + 1 < lines.length && !(depth <= 0 && /\.\s*$/.test(stripComment(lines[end])))) {
          end++;
          depth += bracketDelta(stripComment(lines[end]));
        }
        const statement = lines.slice(line, end + 1).map(stripComment).join(" ");
        const afterName = statement.slice(defMatch[0].length).trimStart();
        cell = {
          kind: afterName.startsWith("(") ? "fact" : "binding",
          name: defMatch[1],
          params: [],
          headerLine: line,
          startLine: line,
          endLine: end
        };
        next = end + 1;
      }
    }

    if (cell) {
      // A comment block directly above (no blank line between) belongs to it.
      let start = cell.headerLine;
      while (start > 0 && lines[start - 1].trimStart().startsWith("#")) start--;
      cell.startLine = start;
      cells.push(cell);
    }
    line = next;
  }
  return cells;
}

// The expression a cell runs, passed to `felidae file.fx --query`.
//   function  f(a: 1).        binding  x.        fact/class  Name.all().
export function buildCellExpression(cell: Cell, args = ""): string {
  switch (cell.kind) {
    case "function":
      return cell.name + "(" + args.trim() + ").";
    case "binding":
      return cell.name + ".";
    case "fact":
    case "class":
      return cell.name + ".all().";
    default:
      throw new Error("The entry function runs through Run/Debug, not as a cell.");
  }
}
