// Pairs each block opener with its `end`. A copy of endBlockPairs from the
// Felidae extension (vs-code-extension/src/extension.ts), kept here so this
// extension needs nothing from it at runtime beyond the interpreter path.
//
// class, `def ... =>`, for, while, switch and try own an explicit `end`. A def
// header may continue over several lines, and the stdlib's native declarations
// are written `def f(...) => ()` followed by `end`.

export interface EndBlockPair {
  openerLine: number;
  openerStart: number;
  openerLength: number;
  endLine: number;
  endStart: number;
}

export function endBlockPairs(lines: readonly string[]): EndBlockPair[] {
  const defOpensBlock = (lineIndex: number): boolean => {
    if (!/^\s*def\s+[A-Za-z_][A-Za-z0-9_:.]*\s*\(/.test(lines[lineIndex])) return false;
    let header = "";
    for (let index = lineIndex; index < lines.length; index++) {
      header += ` ${lines[index].replace(/#.*$/, "")}`;
      if (/=>\s*(?:\(\s*\))?\s*$/.test(header)) return true;
      if (/\.\s*$/.test(header)) return false;
      if (index > lineIndex + 32) return false;
    }
    return false;
  };
  const stack: Array<{ line: number; start: number; length: number }> = [];
  const pairs: EndBlockPair[] = [];
  for (let line = 0; line < lines.length; line++) {
    const text = lines[line];
    const opener = /^\s*(class|def|for|while|switch|try)\b/.exec(text);
    const opens = !!opener && (opener[1] !== "def" || defOpensBlock(line));
    if (opener && opens) {
      stack.push({ line, start: opener.index + opener[0].lastIndexOf(opener[1]), length: opener[1].length });
      continue;
    }
    const closer = /^\s*(end)\b/.exec(text);
    if (!closer) continue;
    const start = stack.pop();
    if (!start) continue;
    pairs.push({
      openerLine: start.line,
      openerStart: start.start,
      openerLength: start.length,
      endLine: line,
      endStart: closer.index + closer[0].lastIndexOf(closer[1])
    });
  }
  return pairs;
}
