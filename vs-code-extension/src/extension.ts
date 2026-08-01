import * as vscode from "vscode";
import * as childProcess from "child_process";
import * as fs from "fs";
import * as path from "path";

type TokenKind =
  | "ident"
  | "string"
  | "number"
  | "import"
  | "lparen"
  | "rparen"
  | "lbrace"
  | "rbrace"
  | "lbracket"
  | "rbracket"
  | "comma"
  | "colon"
  | "dot"
  | "pipe"
  | "question"
  | "bind"
  | "doubleColon"
  | "arrow"
  | "plus"
  | "comparison";

interface Token {
  kind: TokenKind;
  text: string;
  line: number;
  start: number;
  end: number;
}

interface LexResult {
  tokens: Token[];
  diagnostics: vscode.Diagnostic[];
}

interface PositionedString {
  value: string;
  line: number;
  start: number;
  end: number;
}

interface BuiltinDoc {
  heading: string;
  description: string;
  example: string;
}

interface DapRequest extends vscode.DebugProtocolMessage {
  type: "request";
  seq?: number;
  command: string;
  arguments?: unknown;
}

interface FelidaeGraph {
  nodes: Map<string, "fact" | "method" | "library" | "field">;
  edges: Array<{ from: string, to: string, label?: string }>;
}

interface RuntimeGraphNode {
  id: string;
  label: string;
  kind: "fact" | "method" | "library" | "field" | "global" | "value";
  detail?: string;
}

interface RuntimeGraphEdge {
  from: string;
  to: string;
  label: string;
}

interface RuntimeGraph {
  nodes: RuntimeGraphNode[];
  edges: RuntimeGraphEdge[];
}

const semanticLegend = new vscode.SemanticTokensLegend(["variable", "method"], ["readonly"]);

const FELIDAE_BUILTIN_TYPE_NAMES = new Set([
  "any", "array", "bool", "boolean", "decimal", "double", "float", "int", "number", "string"
]);

function loadBuiltinDocs(): Record<string, BuiltinDoc> {
  try {
    const docsPath = path.join(__dirname, "..", "resources", "builtin-docs.json");
    const parsed = JSON.parse(fs.readFileSync(docsPath, "utf8")) as Record<string, BuiltinDoc>;
    return parsed && typeof parsed === "object" ? parsed : {};
  } catch {
    return {};
  }
}

const builtinDocs: Record<string, BuiltinDoc> = loadBuiltinDocs();


function builtinSourceName(name: string): string {
  const legacyColonBuiltins = new Set([
    "math:add", "math:sub", "math:mul", "math:div", "math:mod",
    "str:len", "str:contains", "str:concat", "str:lower", "str:upper",
    "str:trim", "str:split", "str:replace", "str:startsWith", "str:endsWith",
    "array:get", "array:len", "array:push",
    "fn:array", "fn:pair", "fn:tuple",
    "pair:first", "pair:second",
    "json:parse", "json:get", "json:has", "json:keys", "json:set", "json:remove", "json:toText",
    "csv:parse", "csv:toFacts", "csv:toText", "csv:toFelidaeFacts",
    "csv:addRow", "csv:findRows", "csv:updateRows", "csv:deleteRows",
    "file:readFile", "file:readLines", "file:readLine", "file:writeFile", "file:writeLines", "file:appendFile", "file:exists", "file:deleteFile"
  ]);
  return legacyColonBuiltins.has(name) ? name : name.replace(/:/g, ".");
}

function quotePowerShell(value: string): string {
  return `'${String(value).replace(/'/g, "''")}'`;
}

function felidaeTerminalCommand(executablePath: string, args: string[]): string {
  return `& ${quotePowerShell(executablePath)} ${args.map(quotePowerShell).join(" ")}`.trim();
}

function documentRange(document: vscode.TextDocument, line: number, start: number, end: number): vscode.Range {
  return new vscode.Range(
    new vscode.Position(line, start),
    new vscode.Position(line, Math.max(end, start + 1))
  );
}

function makeDiagnostic(
  document: vscode.TextDocument,
  line: number,
  start: number,
  end: number,
  message: string,
  severity: vscode.DiagnosticSeverity
): vscode.Diagnostic {
  return new vscode.Diagnostic(documentRange(document, line, start, end), message, severity);
}

function lexDocument(document: vscode.TextDocument): LexResult {
  const tokens: Token[] = [];
  const diagnostics: vscode.Diagnostic[] = [];

  for (let lineIndex = 0; lineIndex < document.lineCount; lineIndex++) {
    const text = document.lineAt(lineIndex).text;
    let i = 0;

    while (i < text.length) {
      const ch = text[i];

      if (/\s/.test(ch)) {
        i++;
        continue;
      }

      if (ch === "#") {
        break;
      }

      if (ch === "\"") {
        const start = i;
        i++;
        while (i < text.length && text[i] !== "\"") {
          if (text[i] === "\\") {
            i++;
          }
          i++;
        }
        if (i >= text.length) {
          diagnostics.push(makeDiagnostic(document, lineIndex, start, text.length, "Unterminated string literal.", vscode.DiagnosticSeverity.Error));
          break;
        }
        i++;
        tokens.push({ kind: "string", text: text.slice(start + 1, i - 1), line: lineIndex, start, end: i });
        continue;
      }

      if (/[A-Za-z_]/.test(ch)) {
        const start = i;
        i++;
        while (i < text.length && /[A-Za-z0-9_]/.test(text[i])) {
          i++;
        }
        const word = text.slice(start, i);
        tokens.push({ kind: word === "import" ? "import" : "ident", text: word, line: lineIndex, start, end: i });
        continue;
      }

      if (/\d/.test(ch)) {
        const start = i;
        i++;
        while (i < text.length && /\d/.test(text[i])) {
          i++;
        }
        if (text[i] === "." && /\d/.test(text[i + 1] ?? "")) {
          i++;
          while (i < text.length && /\d/.test(text[i])) {
            i++;
          }
        }
        tokens.push({ kind: "number", text: text.slice(start, i), line: lineIndex, start, end: i });
        continue;
      }

      const two = text.slice(i, i + 2);
      if (two === ":=") {
        tokens.push({ kind: "bind", text: two, line: lineIndex, start: i, end: i + 2 });
        i += 2;
        continue;
      }
      if (two === "::") {
        tokens.push({ kind: "doubleColon", text: two, line: lineIndex, start: i, end: i + 2 });
        i += 2;
        continue;
      }
      if (two === "=>") {
        tokens.push({ kind: "arrow", text: two, line: lineIndex, start: i, end: i + 2 });
        i += 2;
        continue;
      }
      if (["==", "!=", "<=", ">="].includes(two)) {
        tokens.push({ kind: "comparison", text: two, line: lineIndex, start: i, end: i + 2 });
        i += 2;
        continue;
      }
      if (ch === "<" || ch === ">") {
        tokens.push({ kind: "comparison", text: ch, line: lineIndex, start: i, end: i + 1 });
        i++;
        continue;
      }

      const singleKinds: Record<string, TokenKind> = {
        "(": "lparen",
        ")": "rparen",
        "{": "lbrace",
        "}": "rbrace",
        "[": "lbracket",
        "]": "rbracket",
        ",": "comma",
        ":": "colon",
        ".": "dot",
        "+": "plus",
        "|": "pipe",
        "?": "question"
      };
      const kind = singleKinds[ch];
      if (kind) {
        tokens.push({ kind, text: ch, line: lineIndex, start: i, end: i + 1 });
        i++;
        continue;
      }

      diagnostics.push(makeDiagnostic(document, lineIndex, i, i + 1, `Unexpected character '${ch}'.`, vscode.DiagnosticSeverity.Error));
      i++;
    }
  }

  return { tokens, diagnostics };
}

function validateImports(document: vscode.TextDocument, tokens: Token[], diagnostics: vscode.Diagnostic[]): void {
  const documentDir = path.dirname(document.uri.fsPath);

  for (let i = 0; i < tokens.length; i++) {
    const token = tokens[i];
    if (token.kind !== "import") {
      continue;
    }

    const pathToken = tokens[i + 1];
    if (!pathToken) {
      diagnostics.push(makeDiagnostic(document, token.line, token.start, token.end, "Import must be followed by a string path or parenthesized string list.", vscode.DiagnosticSeverity.Error));
      continue;
    }

    if (pathToken.kind === "lparen") {
      let cursor = i + 2;
      let sawPath = false;
      while (cursor < tokens.length && tokens[cursor].kind !== "rparen") {
        const item = tokens[cursor];
        if (item.kind !== "string") {
          diagnostics.push(makeDiagnostic(document, item.line, item.start, item.end, "Import lists can only contain string paths.", vscode.DiagnosticSeverity.Error));
          cursor++;
          continue;
        }
        sawPath = true;
        validateImportPath(document, documentDir, item, diagnostics);
        cursor++;
      }
      if (!sawPath) {
        diagnostics.push(makeDiagnostic(document, pathToken.line, pathToken.start, pathToken.end, "Import list must contain at least one path.", vscode.DiagnosticSeverity.Error));
      }
      if (cursor >= tokens.length || tokens[cursor].kind !== "rparen") {
        diagnostics.push(makeDiagnostic(document, pathToken.line, pathToken.start, pathToken.end, "Import list must end with ')'.", vscode.DiagnosticSeverity.Error));
        continue;
      }
      const dotToken = tokens[cursor + 1];
      if (!dotToken || dotToken.kind !== "dot") {
        diagnostics.push(makeDiagnostic(document, tokens[cursor].line, tokens[cursor].end, tokens[cursor].end + 1, "Import statement must end with '.'.", vscode.DiagnosticSeverity.Error));
      }
      continue;
    }

    const dotToken = tokens[i + 2];
    if (pathToken.kind !== "string") {
      diagnostics.push(makeDiagnostic(document, token.line, token.start, token.end, "Import must be followed by a string path or parenthesized string list.", vscode.DiagnosticSeverity.Error));
      continue;
    }
    if (!dotToken || dotToken.kind !== "dot") {
      diagnostics.push(makeDiagnostic(document, pathToken.line, pathToken.end, pathToken.end + 1, "Import statement must end with '.'.", vscode.DiagnosticSeverity.Error));
    }

    validateImportPath(document, documentDir, pathToken, diagnostics);
  }
}

function validateImportPath(
  document: vscode.TextDocument,
  documentDir: string,
  pathToken: Token,
  diagnostics: vscode.Diagnostic[]
): void {
  const rawPath = pathToken.text.trim();
  if (resolveCoreImport(document, rawPath)) {
    return;
  }
  const isWildcard = rawPath.endsWith("/*");
  const checkPath = isWildcard ? rawPath.slice(0, -2) : rawPath;
  const importPath = path.resolve(documentDir, checkPath);
  if (!fs.existsSync(importPath)) {
    diagnostics.push(makeDiagnostic(document, pathToken.line, pathToken.start, pathToken.end, `Import path not found: ${rawPath}`, vscode.DiagnosticSeverity.Warning));
    return;
  }
  if (!isWildcard && !fs.statSync(importPath).isDirectory() && path.extname(importPath) !== ".fx") {
    diagnostics.push(makeDiagnostic(document, pathToken.line, pathToken.start, pathToken.end, "Import files must use the .fx extension.", vscode.DiagnosticSeverity.Error));
  }
}

function isValueStart(token: Token | undefined): boolean {
  return !!token && ["ident", "string", "number", "lbrace", "lbracket"].includes(token.kind);
}

function isMapKey(tokens: Token[], index: number): boolean {
  let depth = 0;
  for (let i = index - 1; i >= 0; i--) {
    const kind = tokens[i].kind;
    if (kind === "rbrace" || kind === "rbracket" || kind === "rparen") depth++;
    if (kind === "lbrace" || kind === "lbracket" || kind === "lparen") {
      if (depth === 0) return kind === "lbrace";
      depth--;
    }
    if (depth === 0 && kind === "dot") return false;
  }
  return false;
}

function isNamedArgument(tokens: Token[], index: number): boolean {
  let depth = 0;
  for (let i = index - 1; i >= 0; i--) {
    const kind = tokens[i].kind;
    if (kind === "rbrace" || kind === "rbracket" || kind === "rparen") depth++;
    if (kind === "lbrace" || kind === "lbracket") {
      if (depth === 0) return false;
      depth--;
    }
    if (kind === "lparen") {
      if (depth === 0) return true;
      depth--;
    }
    if (depth === 0 && kind === "dot") return false;
  }
  return false;
}

function findMatchingParen(tokens: Token[], lparenIndex: number): number | undefined {
  let depth = 0;
  for (let i = lparenIndex; i < tokens.length; i++) {
    if (tokens[i].kind === "lparen") depth++;
    if (tokens[i].kind === "rparen") {
      depth--;
      if (depth === 0) return i;
    }
  }
  return undefined;
}

function validateClauseHeadFields(document: vscode.TextDocument, tokens: Token[], diagnostics: vscode.Diagnostic[]): void {
  const globalBindings = collectGlobalBindings(tokens);
  const importedModules = collectImportedModuleNames(document);
  for (let i = 0; i < tokens.length - 1; i++) {
    if (tokens[i].kind !== "ident" || tokens[i + 1].kind !== "lparen") continue;

    const close = findMatchingParen(tokens, i + 1);
    if (close === undefined) continue;

    const after = tokens[close + 1];
    if (!after || (after.kind !== "arrow" && after.kind !== "dot")) continue;
    const isRuleHead = after.kind === "arrow";
    const bodyEnd = statementEndIndex(tokens, close + 2);
    const methodStyle = isRuleHead && (headLooksMethodStyle(tokens, i + 2, close) || tokens[i].text === "main");
    const declared = collectHeadDeclaredVars(tokens, i + 2, close, methodStyle);
    for (const name of globalBindings) declared.add(name);
    for (const name of importedModules) declared.add(name);

    let depth = 0;
    let argStart = i + 2;
    let argValueStart: number | undefined;
    for (let cursor = i + 2; cursor < close; cursor++) {
      const token = tokens[cursor];
      if (token.kind === "lparen" || token.kind === "lbrace" || token.kind === "lbracket") depth++;
      if (token.kind === "rparen" || token.kind === "rbrace" || token.kind === "rbracket") depth--;
      if (depth !== 0) continue;

      if (token.kind === "comma") {
        const valueStart = argValueStart ?? argStart;
        if (isRuleHead && valueStart < cursor && containsMemberAccess(tokens, valueStart, cursor)) {
          diagnostics.push(makeDiagnostic(document, tokens[valueStart].line, tokens[valueStart].start, token.end, "Rule head fields cannot use member access. Bind a head variable in the body, e.g. Name == e.name.", vscode.DiagnosticSeverity.Error));
        }
        argStart = cursor + 1;
        argValueStart = undefined;
        continue;
      }

      if (cursor === argStart && token.kind === "ident" && tokens[cursor + 1]?.kind === "colon") {
        argValueStart = cursor + 2;
      }
    }
    const finalValueStart = argValueStart ?? argStart;
    if (isRuleHead && finalValueStart < close && containsMemberAccess(tokens, finalValueStart, close)) {
      diagnostics.push(makeDiagnostic(document, tokens[finalValueStart].line, tokens[finalValueStart].start, tokens[close - 1]?.end ?? tokens[finalValueStart].end, "Rule head fields cannot use member access. Bind a head variable in the body, e.g. Name == e.name.", vscode.DiagnosticSeverity.Error));
    }
    if (isRuleHead) {
      validateBodyDeclaredVars(document, tokens, close + 2, bodyEnd, declared, diagnostics);
    }
  }
}

function collectGlobalBindings(tokens: Token[]): Set<string> {
  const globals = new Set<string>();
  for (let i = 0; i + 1 < tokens.length; i++) {
    if (tokens[i].kind === "ident" && tokens[i + 1]?.kind === "bind") {
      globals.add(tokens[i].text);
    }
  }
  return globals;
}

function containsMemberAccess(tokens: Token[], start: number, end: number): boolean {
  for (let i = start; i + 2 < end; i++) {
    if (
      tokens[i].kind === "ident" &&
      (tokens[i + 1].kind === "dot" || tokens[i + 1].kind === "colon") &&
      tokens[i + 2].kind === "ident"
    ) {
      return true;
    }
  }
  return false;
}

function statementEndIndex(tokens: Token[], start: number): number {
  for (let i = start; i < tokens.length; i++) {
    const token = tokens[i];
    const next = tokens[i + 1];
    if (token.kind === "dot" && !(next?.kind === "ident" && next.line === token.line)) {
      return i;
    }
  }
  return tokens.length;
}

function collectVariableNames(tokens: Token[], start: number, end: number): Set<string> {
  const vars = new Set<string>();
  for (let i = start; i < end; i++) {
    const token = tokens[i];
    if (token.kind !== "ident") continue;
    if (token.text === "_") continue;
    if (token.text === "nil") continue;
    if (["else", "extend", "where", "return", "lambda", "then"].includes(token.text)) continue;

    const prev = tokens[i - 1];
    const next = tokens[i + 1];
    const nextNext = tokens[i + 2];
    const prevPrev = tokens[i - 2];

    if ((next?.kind === "dot" || next?.kind === "colon") && isLibraryNamespace(token.text)) continue;
    if (next?.kind === "arrow") continue;
    if (
      /^[A-Z]/.test(token.text) &&
      prev?.kind === "colon" &&
      tokens[i - 2]?.kind === "ident" &&
      ["type", "parent", "of"].includes(tokens[i - 2].text)
    ) {
      const callName = enclosingCallName(tokens, i);
      if (callName === "instanceof") continue;
    }
    if (next?.kind === "colon") continue;
    if (prev?.kind === "lparen" && prevPrev?.kind === "ident" && builtinDocs[prevPrev.text] && /^[A-Z]/.test(token.text)) continue;
    if (next?.kind === "dot" && nextNext?.kind === "ident") {
      vars.add(token.text);
      continue;
    }
    if (next?.kind === "lparen") continue;
    if (prev?.kind === "dot") continue;
    if (prev?.kind === "colon" && /^[A-Z]/.test(token.text)) continue;

    vars.add(token.text);
  }
  return vars;
}

function enclosingCallName(tokens: Token[], index: number): string | undefined {
  let depth = 0;
  for (let i = index; i >= 0; i--) {
    const kind = tokens[i].kind;
    if (kind === "rparen" || kind === "rbrace" || kind === "rbracket") depth++;
    if (kind === "lparen") {
      if (depth === 0 && tokens[i - 1]?.kind === "ident") {
        const nameParts = [tokens[i - 1].text];
        let cursor = i - 2;
        while (
          cursor >= 1 &&
          (tokens[cursor].kind === "dot" || tokens[cursor].kind === "colon") &&
          tokens[cursor - 1]?.kind === "ident"
        ) {
          nameParts.unshift(tokens[cursor - 1].text);
          cursor -= 2;
        }
        return nameParts.join(":");
      }
      depth--;
    }
    if (kind === "lbrace" || kind === "lbracket") depth--;
  }
  return undefined;
}

function headLooksMethodStyle(tokens: Token[], start: number, end: number): boolean {
  let depth = 0;
  let nameStart: number | undefined;
  let valueStart: number | undefined;
  let sawArg = false;
  let sawNamedArg = false;
  let sawNamedMethodArg = false;

  for (let i = start; i <= end; i++) {
    const token = tokens[i];
    if (i === end || (token.kind === "comma" && depth === 0)) {
      const value = valueStart !== undefined ? tokens[valueStart] : undefined;
      if (nameStart !== undefined && value?.kind === "ident" && (isTypeAnnotationName(value.text) || value.text !== tokens[nameStart].text)) {
        sawNamedMethodArg = true;
      } else if (nameStart === undefined && (value?.kind !== "ident" || !isTypeAnnotationName(value.text))) {
        return false;
      }
      if (nameStart !== undefined) sawNamedArg = true;
      sawArg = true;
      nameStart = undefined;
      valueStart = undefined;
      continue;
    }
    if (token.kind === "lparen" || token.kind === "lbrace" || token.kind === "lbracket") depth++;
    if (token.kind === "rparen" || token.kind === "rbrace" || token.kind === "rbracket") depth--;
    if (depth !== 0) continue;
    if (token.kind === "ident" && tokens[i + 1]?.kind === "colon") nameStart = i;
    if (token.kind === "colon") valueStart = i + 1;
  }

  return sawNamedMethodArg || sawNamedArg || (sawArg && !sawNamedArg);
}

function isTypeAnnotationName(name: string): boolean {
  return /^[A-Z]/.test(name) || ["any", "array", "bool", "boolean", "decimal", "double", "float", "int", "number", "string"].includes(name);
}

function collectHeadDeclaredVars(tokens: Token[], start: number, end: number, methodStyle: boolean): Set<string> {
  const declared = new Set<string>();
  let depth = 0;
  let argStart = start;
  let nameStart: number | undefined;
  let valueStart: number | undefined;

  for (let i = start; i <= end; i++) {
    const token = tokens[i];
    if (i === end || (token.kind === "comma" && depth === 0)) {
      if (methodStyle && nameStart !== undefined) {
        declared.add(tokens[nameStart].text);
      }
      if (valueStart !== undefined) {
        const value = tokens[valueStart];
        if (methodStyle && value?.kind === "ident") {
          if (!isTypeAnnotationName(value.text) && value.text !== tokens[nameStart ?? valueStart].text) {
            declared.add(value.text);
          }
        } else {
          for (const name of collectVariableNames(tokens, valueStart, i)) declared.add(name);
        }
      } else if (!methodStyle) {
        for (const name of collectVariableNames(tokens, argStart, i)) declared.add(name);
      }
      argStart = i + 1;
      nameStart = undefined;
      valueStart = undefined;
      continue;
    }

    if (token.kind === "lparen" || token.kind === "lbrace" || token.kind === "lbracket") depth++;
    if (token.kind === "rparen" || token.kind === "rbrace" || token.kind === "rbracket") depth--;
    if (depth !== 0) continue;

    if (i === argStart && token.kind === "ident" && tokens[i + 1]?.kind === "colon") nameStart = i;
    if (i === argStart + 1 && token.kind === "colon") valueStart = i + 1;
  }

  return declared;
}

function validateBodyDeclaredVars(document: vscode.TextDocument, tokens: Token[], start: number, end: number, declared: Set<string>, diagnostics: vscode.Diagnostic[]): void {
  let segmentStart = start;
  let depth = 0;

  const validateSegment = (from: number, to: number): void => {
    while (from < to && tokens[from].kind === "comma") from++;
    while (from < to && tokens[to - 1]?.kind === "comma") to--;
    if (from >= to) return;

    const isAssignment = tokens[from]?.kind === "ident" && tokens[from + 1]?.kind === "bind";
    const used = isAssignment ? collectVariableNames(tokens, from + 2, to) : collectVariableNames(tokens, from, to);
    for (const name of used) {
      if (!declared.has(name)) {
        diagnostics.push(makeDiagnostic(document, tokens[from].line, tokens[from].start, tokens[to - 1]?.end ?? tokens[from].end, `Variable '${name}' is used before declaration. Declare it in the rule head or assign it before use.`, vscode.DiagnosticSeverity.Error));
        break;
      }
    }
    if (isAssignment) declared.add(tokens[from].text);
  };

  for (let i = start; i <= end; i++) {
    const token = tokens[i];
    if (i === end || (((token.kind === "comma" || token.kind === "pipe") ||
      (token.kind === "ident" && token.text === "else")) && depth === 0)) {
      validateSegment(segmentStart, i);
      segmentStart = i + 1;
      continue;
    }
    if (token.kind === "lparen" || token.kind === "lbrace" || token.kind === "lbracket") depth++;
    if (token.kind === "rparen" || token.kind === "rbrace" || token.kind === "rbracket") depth--;
  }
}

function validateStatements(document: vscode.TextDocument, tokens: Token[], diagnostics: vscode.Diagnostic[]): void {
  const parenStack: Token[] = [];
  let statementStart: Token | undefined;
  let previous: Token | undefined;

  for (let i = 0; i < tokens.length; i++) {
    const token = tokens[i];
    statementStart ??= token;

    if (token.kind === "lparen" || token.kind === "lbrace" || token.kind === "lbracket") {
      parenStack.push(token);
    } else if (token.kind === "rparen" || token.kind === "rbrace" || token.kind === "rbracket") {
      if (parenStack.length === 0) {
        diagnostics.push(makeDiagnostic(document, token.line, token.start, token.end, "Unmatched closing delimiter.", vscode.DiagnosticSeverity.Error));
      } else {
        const open = parenStack.pop();
        const matches =
          (open?.kind === "lparen" && token.kind === "rparen") ||
          (open?.kind === "lbrace" && token.kind === "rbrace") ||
          (open?.kind === "lbracket" && token.kind === "rbracket");
        if (!matches) {
          diagnostics.push(makeDiagnostic(document, token.line, token.start, token.end, "Mismatched closing delimiter.", vscode.DiagnosticSeverity.Error));
        }
      }
    }

    if (previous?.kind === "arrow" && (token.kind === "dot" || token.kind === "arrow")) {
      diagnostics.push(makeDiagnostic(document, previous.line, previous.start, previous.end, "Rule arrow must be followed by at least one goal.", vscode.DiagnosticSeverity.Error));
    }

    const next = tokens[i + 1];
    const isAccessorDot = token.kind === "dot" && next?.kind === "ident" && next.line === token.line;
    if (token.kind === "dot" && !isAccessorDot) {
      statementStart = undefined;
    }

    previous = token;
  }

  for (const open of parenStack) {
    diagnostics.push(makeDiagnostic(document, open.line, open.start, open.end, "Unclosed delimiter.", vscode.DiagnosticSeverity.Error));
  }

  if (statementStart && tokens.length > 0) {
    const last = tokens[tokens.length - 1];
    diagnostics.push(makeDiagnostic(document, last.line, last.end, last.end + 1, "Statement should end with '.'. Queries may omit it at the command line, but source files should terminate statements.", vscode.DiagnosticSeverity.Warning));
  }
}

function validateCalls(document: vscode.TextDocument, tokens: Token[], diagnostics: vscode.Diagnostic[]): void {
  const lowercaseBuiltins = new Set(["throw", "lambda", "then", "type", "instanceof", "return"]);

  for (let i = 0; i < tokens.length; i++) {
    const token = tokens[i];
    const next = tokens[i + 1];

    if (token.kind === "doubleColon") {
      diagnostics.push(makeDiagnostic(document, token.line, token.start, token.end, "'::' is not supported in Felidae. Use '.' for top-level package/module calls.", vscode.DiagnosticSeverity.Error));
    }

    if (token.kind === "ident" && next?.kind === "lparen") {
      const previous = tokens[i - 1];
      const isNamespaced = previous?.kind === "colon" || previous?.kind === "dot";
      if (!isNamespaced && !lowercaseBuiltins.has(token.text) && !builtinDocs[token.text] && !/^[A-Z_]/.test(token.text)) {
        diagnostics.push(makeDiagnostic(document, token.line, token.start, token.end, "Predicate names usually start with an uppercase letter in this project.", vscode.DiagnosticSeverity.Warning));
      }
      continue;
    }

    if (token.kind === "ident" && next?.kind === "colon") {
      const value = tokens[i + 2];
      const nextNext = tokens[i + 2];
      const isNamespaceOrAccess = nextNext?.kind === "ident";
      if (!isNamespaceOrAccess && !isNamedArgument(tokens, i) && !isMapKey(tokens, i)) {
        continue;
      }
      if (!isNamespaceOrAccess && !isValueStart(value)) {
        diagnostics.push(makeDiagnostic(document, token.line, token.start, next.end, "Named argument must be followed by a value expression.", vscode.DiagnosticSeverity.Error));
      }
    }
  }
}

function validateDocument(document: vscode.TextDocument): vscode.Diagnostic[] {
  void document;
  return [];
}

function importLinkTarget(document: vscode.TextDocument, rawPath: string): vscode.Uri | undefined {
  const coreTarget = resolveCoreImport(document, rawPath);
  if (coreTarget) return coreTarget;
  const documentDir = path.dirname(document.uri.fsPath);
  const isWildcard = rawPath.endsWith("/*");
  const checkPath = isWildcard ? rawPath.slice(0, -2) : rawPath;
  const resolved = path.resolve(documentDir, checkPath);
  if (!fs.existsSync(resolved)) {
    return undefined;
  }
  return vscode.Uri.file(resolved);
}

function resolveCoreImport(document: vscode.TextDocument, rawPath: string): vscode.Uri | undefined {
  if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(rawPath)) return undefined;
  const folders = vscode.workspace.workspaceFolders ?? [];
  const candidates: string[] = [];
  for (const folder of folders) {
    candidates.push(path.join(folder.uri.fsPath, "core", `${rawPath}.fx`));
  }
  let current = path.dirname(document.uri.fsPath);
  while (current && current !== path.dirname(current)) {
    candidates.push(path.join(current, "core", `${rawPath}.fx`));
    current = path.dirname(current);
  }
  for (const candidate of candidates) {
    if (fs.existsSync(candidate) && fs.statSync(candidate).isFile()) {
      return vscode.Uri.file(candidate);
    }
  }
  return undefined;
}

function collectImportStrings(document: vscode.TextDocument): PositionedString[] {
  const result: PositionedString[] = [];
  for (let line = 0; line < document.lineCount; line++) {
    const text = document.lineAt(line).text;
    if (!/\bimport\b/.test(text)) continue;
    const importIndex = text.indexOf("import");
    const commentIndex = text.indexOf("#");
    if (commentIndex >= 0 && commentIndex < importIndex) continue;
    const regex = /"([^"]+)"/g;
    let match: RegExpExecArray | null;
    while ((match = regex.exec(text)) !== null) {
      result.push({
        value: match[1],
        line,
        start: match.index + 1,
        end: match.index + 1 + match[1].length
      });
    }
  }
  return result;
}

function collectImportedModuleNames(document: vscode.TextDocument): Set<string> {
  const names = new Set<string>();
  for (const item of collectImportStrings(document)) {
    const normalized = item.value.replace(/\\/g, "/").replace(/\*$/, "");
    const base = path.basename(normalized, ".fx");
    if (base && /^[A-Za-z_][A-Za-z0-9_]*$/.test(base)) names.add(base);
  }
  return names;
}

class FelidaeDocumentLinkProvider implements vscode.DocumentLinkProvider {
  provideDocumentLinks(document: vscode.TextDocument): vscode.ProviderResult<vscode.DocumentLink[]> {
    if (document.languageId !== "felidae") return [];
    return collectImportStrings(document)
      .map((item) => {
        const target = importLinkTarget(document, item.value);
        if (!target) return undefined;
        return new vscode.DocumentLink(documentRange(document, item.line, item.start, item.end), target);
      })
      .filter((link): link is vscode.DocumentLink => !!link);
  }
}

function getCallNameAtPosition(document: vscode.TextDocument, position: vscode.Position): string | undefined {
  const line = document.lineAt(position.line).text;
  let start = position.character;
  let end = position.character;
  const isNameChar = (ch: string | undefined): boolean => !!ch && /[A-Za-z0-9_:.]/.test(ch);

  while (start > 0 && isNameChar(line[start - 1])) start--;
  while (end < line.length && isNameChar(line[end])) end++;

  const name = line.slice(start, end).replace(/^\.+|\.+$/g, "");
  if (!/^[A-Za-z_][A-Za-z0-9_:.]*$/.test(name)) return undefined;

  const after = line.slice(end);
  const before = line.slice(0, start);
  if (/^\s*\(/.test(after) || /[A-Za-z0-9_:.]$/.test(before)) {
    return builtinDocs[name.replace(/\./g, ":")] ? name.replace(/\./g, ":") : name;
  }
  return undefined;
}

function definitionPattern(name: string): RegExp {
  const parts = name.split(/[:.]/).map((part) => part.replace(/[.*+?^${}()|[\]\\]/g, "\\$&"));
  const qualifiedName = parts.join("\\s*[:.]\\s*");
  return new RegExp(`^\\s*${qualifiedName}(?:\\s+extend\\s+[A-Za-z_][A-Za-z0-9_]*)?\\s*\\(`);
}

class FelidaeHoverProvider implements vscode.HoverProvider {
  provideHover(document: vscode.TextDocument, position: vscode.Position): vscode.ProviderResult<vscode.Hover> {
    const name = getCallNameAtPosition(document, position);
    if (!name) return undefined;

    const doc = builtinDocs[name];
    if (!doc) return undefined;

    const markdown = new vscode.MarkdownString();
    markdown.appendMarkdown(`### ${doc.heading}\n\n`);
    markdown.appendMarkdown(`${doc.description}\n\n`);
    markdown.appendCodeblock(doc.example, "felidae");
    return new vscode.Hover(markdown);
  }
}

class FelidaeDefinitionProvider implements vscode.DefinitionProvider {
  async provideDefinition(document: vscode.TextDocument, position: vscode.Position): Promise<vscode.Definition | undefined> {
    const name = getCallNameAtPosition(document, position);
    if (!name) return undefined;
    const builtin = await builtinDefinition(document, name);
    if (builtin) return builtin;

    const pattern = definitionPattern(name);
    const locations: vscode.Location[] = [];
    const files = await vscode.workspace.findFiles("**/*.fx", "**/{node_modules,build,out}/**", 200);

    for (const file of files) {
      const candidate = await vscode.workspace.openTextDocument(file);
      for (let line = 0; line < candidate.lineCount; line++) {
        const text = candidate.lineAt(line).text;
        if (!pattern.test(text)) continue;
        if (file.toString() === document.uri.toString() && line === position.line) continue;
        locations.push(new vscode.Location(file, new vscode.Position(line, text.search(/\S/))));
      }
    }

    return locations.length ? locations : undefined;
  }
}

async function builtinDefinition(document: vscode.TextDocument, name: string): Promise<vscode.Location | undefined> {
  const moduleName = name.split(":")[0]?.split(".")[0];
  if (!moduleName) return undefined;
  const workspaceFolder = vscode.workspace.getWorkspaceFolder(document.uri);
  if (!workspaceFolder) return undefined;
  const target = vscode.Uri.file(path.join(workspaceFolder.uri.fsPath, "core", `${moduleName}.fx`));
  if (!fs.existsSync(target.fsPath)) return undefined;
  const sourceName = builtinSourceName(name);
  const declaration = definitionPattern(sourceName);
  const targetDocument = await vscode.workspace.openTextDocument(target);
  for (let line = 0; line < targetDocument.lineCount; line++) {
    const text = targetDocument.lineAt(line).text;
    if (declaration.test(text)) {
      return new vscode.Location(target, new vscode.Position(line, text.search(/\S/)));
    }
  }
  return new vscode.Location(target, new vscode.Position(0, 0));
}

class FelidaeFoldingRangeProvider implements vscode.FoldingRangeProvider {
  provideFoldingRanges(document: vscode.TextDocument): vscode.FoldingRange[] {
    const lexed = lexDocument(document);
    const ranges: vscode.FoldingRange[] = [];
    let startLine: number | undefined;
    let depth = 0;

    for (let i = 0; i < lexed.tokens.length; i++) {
      const token = lexed.tokens[i];
      if (startLine === undefined && token.kind !== "dot" && token.kind !== "comma") {
        startLine = token.line;
      }

      if (token.kind === "lparen" || token.kind === "lbrace" || token.kind === "lbracket") {
        depth++;
        continue;
      }
      if (token.kind === "rparen" || token.kind === "rbrace" || token.kind === "rbracket") {
        depth = Math.max(0, depth - 1);
        continue;
      }

      const next = lexed.tokens[i + 1];
      const accessorDot = token.kind === "dot" && next?.kind === "ident" && next.line === token.line;
      if (token.kind === "dot" && depth === 0 && !accessorDot) {
        if (startLine !== undefined && token.line > startLine) {
          ranges.push(new vscode.FoldingRange(startLine, token.line, vscode.FoldingRangeKind.Region));
        }
        startLine = undefined;
      }
    }

    return ranges;
  }
}

class FelidaeCodeLensProvider implements vscode.CodeLensProvider {
  provideCodeLenses(document: vscode.TextDocument): vscode.CodeLens[] {
    if (document.languageId !== "felidae") return [];
    const lenses: vscode.CodeLens[] = [];
    for (let line = 0; line < document.lineCount; line++) {
      const text = document.lineAt(line).text;
      const match = /^\s*main\s*\([^)]*\)\s*=>/.exec(text);
      if (!match) continue;
      const range = new vscode.Range(line, text.indexOf("main"), line, text.indexOf("main") + 4);
      lenses.push(new vscode.CodeLens(range, {
        title: "$(play) Run",
        command: "felidae.runMain",
        arguments: [document.uri]
      }));
      lenses.push(new vscode.CodeLens(range, {
        title: "| $(debug-alt) Debug",
        command: "felidae.debugMain",
        arguments: [document.uri]
      }));
      lenses.push(new vscode.CodeLens(range, {
        title: "| $(type-hierarchy-sub) visualize",
        command: "felidae.visualize",
        arguments: [document.uri]
      }));
    }
    return lenses;
  }
}

class FelidaeSemanticTokensProvider implements vscode.DocumentSemanticTokensProvider {
  provideDocumentSemanticTokens(document: vscode.TextDocument): vscode.SemanticTokens {
    const builder = new vscode.SemanticTokensBuilder(semanticLegend);
    const lexed = lexDocument(document);
    const tokens = lexed.tokens;

    for (let i = 0; i < tokens.length; i++) {
      const token = tokens[i];
      if (token.kind !== "ident" || token.text === "_") continue;

      const next = tokens[i + 1];
      const previous = tokens[i - 1];
      const nextNext = tokens[i + 2];
      const isHeadParam = next?.kind === "colon" && isInsideMethodHead(tokens, i);
      const isAssignmentTarget = next?.kind === "bind";
      const isLambdaItem = previous?.kind === "comma" && next?.kind === "arrow";
      const isMemberBase = (next?.kind === "dot" || next?.kind === "colon") && nextNext?.kind === "ident";
      const isKeyword = ["if", "else", "extend", "where", "return", "lambda", "then", "nil"].includes(token.text);
      const isCall = next?.kind === "lparen";

      if (isKeyword) continue;

      if (isCall) {
        // A call/rule/method head: `Name(...)` immediately followed by `=>`.
        // This never overlaps the variable checks below, which all require
        // `next` to be colon/bind/arrow/dot rather than lparen.
        const close = findMatchingParen(tokens, i + 1);
        const isMethodHead = close !== undefined && tokens[close + 1]?.kind === "arrow";
        if (isMethodHead) {
          builder.push(token.line, token.start, Math.max(1, token.end - token.start), 1, 0);
        }
        continue;
      }

      // A plain variable reference: a lowercase-leading identifier used as a
      // value (a call argument, list item, or comparison operand) rather than
      // a named-arg key, a type annotation, or a call/predicate name. Type
      // annotations (`input: Person`) are excluded by the leading-uppercase
      // check, matching the interpreter's own convention for type names.
      // Builtin primitive type names (`value: int`) are lowercase, so they
      // need their own exclusion alongside the uppercase-type-name check.
      const isBuiltinTypeName = FELIDAE_BUILTIN_TYPE_NAMES.has(token.text);
      const isValuePosition =
        previous?.kind === "colon" ||
        previous?.kind === "comma" ||
        previous?.kind === "lparen" ||
        previous?.kind === "comparison" ||
        previous?.kind === "bind" ||
        next?.kind === "comparison";
      const isBareValueReference =
        !/^[A-Z]/.test(token.text) &&
        !isBuiltinTypeName &&
        isValuePosition &&
        next?.kind !== "lparen" &&
        next?.kind !== "colon";

      if (isHeadParam || isAssignmentTarget || isLambdaItem || isMemberBase || isBareValueReference) {
        builder.push(token.line, token.start, Math.max(1, token.end - token.start), 0, 1);
      }
    }

    return builder.build();
  }
}

function isInsideMethodHead(tokens: Token[], index: number): boolean {
  let left = index;
  while (left >= 0 && tokens[left].kind !== "lparen" && tokens[left].kind !== "dot") {
    left--;
  }
  if (left < 1 || tokens[left].kind !== "lparen" || tokens[left - 1]?.kind !== "ident") return false;

  let depth = 0;
  for (let right = left; right < tokens.length; right++) {
    const token = tokens[right];
    if (token.kind === "lparen") depth++;
    if (token.kind === "rparen") {
      depth--;
      if (depth === 0) {
        return tokens[right + 1]?.kind === "arrow";
      }
    }
  }
  return false;
}

async function visualizeFelidae(context: vscode.ExtensionContext, uri?: vscode.Uri): Promise<void> {
  const document = await getFelidaeDocument(uri);
  if (!document) {
    vscode.window.showWarningMessage("Open a Felidae .fx file before visualizing.");
    return;
  }

  const graph = await vscode.window.withProgress(
    { location: vscode.ProgressLocation.Notification, title: "Felidae: loading runtime graph from Celidae..." },
    () => loadRuntimeGraph(document)
  );
  const runtimeGraph = graph ?? staticGraphToRuntimeGraph(buildFelidaeGraph(document));
  const panel = vscode.window.createWebviewPanel(
    "felidaeVisualizer",
    `Felidae Graph: ${path.basename(document.uri.fsPath)}`,
    vscode.ViewColumn.Beside,
    {
      enableScripts: true,
      localResourceRoots: [vscode.Uri.joinPath(context.extensionUri, "media")]
    }
  );
  const cytoscapeUri = panel.webview.asWebviewUri(vscode.Uri.joinPath(context.extensionUri, "media", "cytoscape.min.js"));
  panel.webview.html = visualizationHtml(cytoscapeUri, runtimeGraph);
}

async function loadRuntimeGraph(document: vscode.TextDocument): Promise<RuntimeGraph | undefined> {
  const interpreterPath = resolveCelidaePath(document.uri);
  if (!fs.existsSync(interpreterPath)) {
    vscode.window.showWarningMessage(`Celidae visualizer not found. Using static source graph instead: ${interpreterPath}`);
    return undefined;
  }

  if (document.isDirty) {
    await document.save();
  }

  return new Promise((resolve) => {
    // The debugger graph is exchanged through stdout markers only; no JSON file is written.
    childProcess.execFile(
      interpreterPath,
      [document.uri.fsPath, "--visualize-data-json", "--load-imports"],
      { cwd: path.dirname(document.uri.fsPath), windowsHide: true, maxBuffer: 8 * 1024 * 1024 },
      (error, stdout, stderr) => {
        if (error) {
          vscode.window.showWarningMessage(`Felidae runtime graph failed. Using static source graph instead: ${stderr || error.message}`);
          resolve(undefined);
          return;
        }
        const match = /FELIDAE_GRAPH_BEGIN\s*([\s\S]*?)\s*FELIDAE_GRAPH_END/.exec(stdout);
        if (!match) {
          vscode.window.showWarningMessage("Celidae did not return a graph snapshot. Using static source graph instead.");
          resolve(undefined);
          return;
        }
        try {
          resolve(validateRuntimeGraph(JSON.parse(match[1])));
        } catch (parseError) {
          vscode.window.showWarningMessage(`Felidae graph snapshot was invalid JSON. Using static source graph instead: ${String(parseError)}`);
          resolve(undefined);
        }
      }
    );
  });
}

function validateRuntimeGraph(value: unknown): RuntimeGraph {
  const graph = value as { nodes?: unknown, edges?: unknown };
  const rawNodes = Array.isArray(graph.nodes) ? graph.nodes : [];
  const rawEdges = Array.isArray(graph.edges) ? graph.edges : [];
  const nodes: RuntimeGraphNode[] = rawNodes
    .filter((node): node is RuntimeGraphNode => {
      const candidate = node as RuntimeGraphNode;
      return typeof candidate.id === "string" &&
        typeof candidate.label === "string" &&
        typeof candidate.kind === "string";
    });
  const nodeIds = new Set(nodes.map((node) => node.id));
  const edges: RuntimeGraphEdge[] = rawEdges
    .filter((edge): edge is RuntimeGraphEdge => {
      const candidate = edge as RuntimeGraphEdge;
      return typeof candidate.from === "string" &&
        typeof candidate.to === "string" &&
        typeof candidate.label === "string" &&
        nodeIds.has(candidate.from) &&
        nodeIds.has(candidate.to);
    });
  return { nodes, edges };
}

function buildFelidaeGraph(document: vscode.TextDocument): FelidaeGraph {
  const graph: FelidaeGraph = { nodes: new Map(), edges: [] };
  const text = document.getText();
  const lexed = lexDocument(document);
  const declaration = /^\s*([A-Za-z_][A-Za-z0-9_:.]*)(?:\s+extend\s+([A-Za-z_][A-Za-z0-9_]*))?\s*\(([\s\S]*?)\)\s*(=>|\.)/gm;
  let match: RegExpExecArray | null;

  while ((match = declaration.exec(text)) !== null) {
    const name = normalizeGraphName(match[1]);
    const parent = match[2] ? normalizeGraphName(match[2]) : undefined;
    const bodyMarker = match[4];
    const bodyEnd = bodyMarker === "=>" ? statementEndOffset(document, lexed.tokens, declaration.lastIndex) : undefined;
    const body = bodyEnd !== undefined ? text.slice(declaration.lastIndex, bodyEnd) : "";
    const kind = isLibraryName(name) ? "library" : bodyMarker === "=>" ? "method" : "fact";
    graph.nodes.set(name, kind);

    if (kind === "fact") {
      for (const field of collectHeadFields(match[3])) {
        const fieldName = `${name}.${field}`;
        graph.nodes.set(fieldName, "field");
        graph.edges.push({ from: name, to: fieldName, label: "field" });
      }
    }

    if (parent) {
      graph.nodes.set(parent, "fact");
      graph.edges.push({ from: name, to: parent });
    }

    if (body) {
      for (const call of collectGraphCalls(body)) {
        if (call === name || ["return", "where", "else", "lambda", "then"].includes(call)) continue;
        graph.nodes.set(call, isLibraryName(call) ? "library" : "method");
        graph.edges.push({ from: name, to: call });
      }
    }
  }

  return graph;
}

function staticGraphToRuntimeGraph(graph: FelidaeGraph): RuntimeGraph {
  const nodes: RuntimeGraphNode[] = [];
  for (const [name, kind] of graph.nodes) {
    nodes.push({ id: kind + ":" + name, label: name, kind });
  }
  const nodeIds = new Set(nodes.map((node) => node.id));
  const edges = graph.edges
    .map((edge) => {
      const fromKind = graph.nodes.get(edge.from) ?? "method";
      const toKind = graph.nodes.get(edge.to) ?? "method";
      return { from: fromKind + ":" + edge.from, to: toKind + ":" + edge.to, label: edge.label ?? "calls" };
    })
    .filter((edge) => nodeIds.has(edge.from) && nodeIds.has(edge.to));
  return { nodes, edges };
}

function collectHeadFields(argsText: string): string[] {
  const fields = new Set<string>();
  let depth = 0;
  let segmentStart = 0;
  const flush = (end: number) => {
    const segment = argsText.slice(segmentStart, end).trim();
    const match = /^([A-Za-z_][A-Za-z0-9_]*)\s*:/.exec(segment);
    if (match) fields.add(match[1]);
  };
  for (let i = 0; i < argsText.length; i++) {
    const ch = argsText[i];
    if (ch === "(" || ch === "{" || ch === "[") depth++;
    else if (ch === ")" || ch === "}" || ch === "]") depth = Math.max(0, depth - 1);
    else if (ch === "," && depth === 0) {
      flush(i);
      segmentStart = i + 1;
    }
  }
  flush(argsText.length);
  return [...fields];
}

function statementEndOffset(document: vscode.TextDocument, tokens: Token[], offset: number): number | undefined {
  for (let i = 0; i < tokens.length; i++) {
    const token = tokens[i];
    const tokenOffset = document.offsetAt(new vscode.Position(token.line, token.start));
    if (tokenOffset < offset) continue;
    const next = tokens[i + 1];
    const accessorDot = token.kind === "dot" && next?.kind === "ident" && next.line === token.line;
    if (token.kind === "dot" && !accessorDot) {
      return tokenOffset;
    }
  }
  return undefined;
}

function collectGraphCalls(text: string): string[] {
  const calls: string[] = [];
  const callPattern = /\b([A-Za-z_][A-Za-z0-9_]*(?:(?:[:.])[A-Za-z_][A-Za-z0-9_]*)*)\s*\(/g;
  let match: RegExpExecArray | null;
  while ((match = callPattern.exec(text)) !== null) {
    calls.push(normalizeGraphName(match[1]));
  }
  return calls;
}

function normalizeGraphName(name: string): string {
  return name.replace(/\./g, ":");
}

const FELIDAE_LIBRARY_NAMES =
  "array|comparison|console|csv|db|exception|fact|fact_analysis|file|flibrary|fn|group|gtk|http|json|list|logic|math|ml|package|pair|plot|prelude|probability|process|qt|set|smoke|str|system|thread|wordnet";

function isLibraryName(name: string): boolean {
  return new RegExp(`^(${FELIDAE_LIBRARY_NAMES})(:|$)`).test(name);
}

function isLibraryNamespace(name: string): boolean {
  return new RegExp(`^(${FELIDAE_LIBRARY_NAMES})$`).test(name);
}

const DECLARATION_PATTERN =
  /^[ \t]*([A-Za-z_][A-Za-z0-9_:.]*)(?:\s+extend\s+([A-Za-z_][A-Za-z0-9_]*))?\s*\(([\s\S]*?)\)\s*(=>|\.)/gm;
const GLOBAL_BINDING_PATTERN = /^([A-Za-z_][A-Za-z0-9_]*)\s*:=/gm;

class FelidaeDocumentSymbolProvider implements vscode.DocumentSymbolProvider {
  provideDocumentSymbols(document: vscode.TextDocument): vscode.ProviderResult<vscode.DocumentSymbol[]> {
    if (document.languageId !== "felidae") return [];
    const text = document.getText();
    const symbols: vscode.DocumentSymbol[] = [];

    const declaration = new RegExp(DECLARATION_PATTERN);
    let match: RegExpExecArray | null;
    while ((match = declaration.exec(text)) !== null) {
      const rawName = match[1];
      const normalized = normalizeGraphName(rawName);
      if (isLibraryName(normalized)) continue;

      const isMethod = match[4] === "=>";
      const nameStart = match.index + match[0].indexOf(rawName);
      const nameRange = new vscode.Range(
        document.positionAt(nameStart),
        document.positionAt(nameStart + rawName.length)
      );
      const fullRange = new vscode.Range(
        document.positionAt(match.index),
        document.positionAt(match.index + match[0].length)
      );
      const detail = isMethod ? (match[2] ? `extends ${match[2]}` : "") : "fact";
      const symbol = new vscode.DocumentSymbol(
        rawName,
        detail,
        isMethod ? vscode.SymbolKind.Method : vscode.SymbolKind.Struct,
        fullRange,
        nameRange
      );

      if (!isMethod) {
        for (const field of collectHeadFields(match[3])) {
          const fieldOffset = text.indexOf(field, match.index);
          const fieldPos = fieldOffset >= 0 && fieldOffset < match.index + match[0].length
            ? document.positionAt(fieldOffset)
            : nameRange.start;
          const fieldRange = new vscode.Range(fieldPos, fieldPos.translate(0, field.length));
          symbol.children.push(
            new vscode.DocumentSymbol(field, "field", vscode.SymbolKind.Field, fieldRange, fieldRange)
          );
        }
      }
      symbols.push(symbol);
    }

    const globalBinding = new RegExp(GLOBAL_BINDING_PATTERN);
    while ((match = globalBinding.exec(text)) !== null) {
      const name = match[1];
      const nameRange = new vscode.Range(
        document.positionAt(match.index),
        document.positionAt(match.index + name.length)
      );
      symbols.push(new vscode.DocumentSymbol(name, "global", vscode.SymbolKind.Variable, nameRange, nameRange));
    }

    return symbols;
  }
}

function tokenIndexBefore(tokens: Token[], position: vscode.Position): number {
  let index = -1;
  for (let i = 0; i < tokens.length; i++) {
    const token = tokens[i];
    if (token.line > position.line) break;
    if (token.line === position.line && token.start >= position.character) break;
    index = i;
  }
  return index;
}

function builtinDocKeysForNamespace(baseName: string): string[] {
  const prefix = `${baseName}:`;
  return Object.keys(builtinDocs).filter((key) => key.startsWith(prefix));
}

function builtinDocCompletionsForNamespace(baseName: string): vscode.CompletionItem[] {
  return builtinDocKeysForNamespace(baseName).map((key) => {
    const doc = builtinDocs[key];
    const functionName = key.slice(key.indexOf(":") + 1);
    const item = new vscode.CompletionItem(functionName, vscode.CompletionItemKind.Function);
    item.detail = doc.heading;
    const markdown = new vscode.MarkdownString();
    markdown.appendMarkdown(`${doc.description}\n\n`);
    markdown.appendCodeblock(doc.example, "felidae");
    item.documentation = markdown;
    item.insertText = functionName;
    return item;
  });
}

function argNamesFromExample(example: string): string[] {
  const names = new Set<string>();
  const pattern = /([A-Za-z_][A-Za-z0-9_]*)\s*:/g;
  let match: RegExpExecArray | null;
  while ((match = pattern.exec(example)) !== null) names.add(match[1]);
  return [...names];
}

function namedArgCompletion(name: string, detail?: string): vscode.CompletionItem {
  const item = new vscode.CompletionItem(name, vscode.CompletionItemKind.Field);
  item.insertText = new vscode.SnippetString(`${name}: $0`);
  if (detail) item.detail = detail;
  return item;
}

function completionsForCallFields(document: vscode.TextDocument, callName: string): vscode.CompletionItem[] {
  const normalized = callName.replace(/\./g, ":");
  const builtin = builtinDocs[normalized];
  if (builtin) {
    return argNamesFromExample(builtin.example).map((name) => namedArgCompletion(name, builtin.heading));
  }

  const simpleName = normalized.split(":").pop() ?? normalized;
  const text = document.getText();
  const declaration = new RegExp(DECLARATION_PATTERN);
  let match: RegExpExecArray | null;
  while ((match = declaration.exec(text)) !== null) {
    if (match[1] !== simpleName && normalizeGraphName(match[1]) !== simpleName) continue;
    return collectHeadFields(match[3]).map((name) => namedArgCompletion(name, `field of ${match![1]}`));
  }
  return [];
}

function completionsForScope(
  document: vscode.TextDocument,
  tokens: Token[],
  index: number
): vscode.CompletionItem[] {
  const items = new Map<string, vscode.CompletionItem>();
  const add = (name: string, kind: vscode.CompletionItemKind, detail?: string) => {
    if (!name || items.has(name)) return;
    const item = new vscode.CompletionItem(name, kind);
    if (detail) item.detail = detail;
    items.set(name, item);
  };

  for (const name of collectVariableNames(tokens, 0, Math.max(0, index + 1))) {
    add(name, vscode.CompletionItemKind.Variable, "in scope");
  }
  for (const name of collectGlobalBindings(tokens)) {
    add(name, vscode.CompletionItemKind.Constant, "global");
  }
  for (const name of collectImportedModuleNames(document)) {
    add(name, vscode.CompletionItemKind.Module, "imported module");
  }
  for (const name of FELIDAE_LIBRARY_NAMES.split("|")) {
    add(name, vscode.CompletionItemKind.Module, "core library");
  }
  for (const key of Object.keys(builtinDocs)) {
    if (key.includes(":")) continue;
    add(key, vscode.CompletionItemKind.Function, builtinDocs[key].heading);
  }

  const text = document.getText();
  const declaration = new RegExp(DECLARATION_PATTERN);
  let match: RegExpExecArray | null;
  while ((match = declaration.exec(text)) !== null) {
    const normalized = normalizeGraphName(match[1]);
    if (isLibraryName(normalized)) continue;
    const isMethod = match[4] === "=>";
    add(match[1], isMethod ? vscode.CompletionItemKind.Method : vscode.CompletionItemKind.Struct, isMethod ? "method" : "fact");
  }

  const cached = symbolSummaryCache.get(document.uri.toString());
  if (cached) {
    for (const method of cached.methods) add(method.name, vscode.CompletionItemKind.Method, "method (felidae_debug)");
    for (const fact of cached.facts) add(fact.name, vscode.CompletionItemKind.Struct, "fact (felidae_debug)");
    for (const global of cached.globals) add(global.name, vscode.CompletionItemKind.Constant, "global (felidae_debug)");
  }

  return [...items.values()];
}

class FelidaeCompletionItemProvider implements vscode.CompletionItemProvider {
  provideCompletionItems(document: vscode.TextDocument, position: vscode.Position): vscode.ProviderResult<vscode.CompletionItem[]> {
    if (document.languageId !== "felidae") return [];
    const linePrefix = document.lineAt(position.line).text.slice(0, position.character);

    const dotMatch = /([A-Za-z_][A-Za-z0-9_]*)\.$/.exec(linePrefix);
    if (dotMatch) {
      const namespaceItems = builtinDocCompletionsForNamespace(dotMatch[1]);
      if (namespaceItems.length) return namespaceItems;
    }

    const lexed = lexDocument(document);
    const tokens = lexed.tokens;
    const index = tokenIndexBefore(tokens, position);
    const items = new Map<string, vscode.CompletionItem>();

    if (/[(,]\s*$/.test(linePrefix)) {
      const callName = enclosingCallName(tokens, index);
      if (callName) {
        for (const item of completionsForCallFields(document, callName)) {
          items.set(item.label as string, item);
        }
      }
    }

    for (const item of completionsForScope(document, tokens, index)) {
      if (!items.has(item.label as string)) items.set(item.label as string, item);
    }

    return [...items.values()];
  }
}

function cytoscapeElements(graph: RuntimeGraph): Array<{ data: Record<string, string> }> {
  const nodes = graph.nodes.map((node) => ({
    data: {
      id: node.id,
      label: node.label,
      kind: node.kind,
      detail: node.detail ?? ""
    }
  }));
  const edges = graph.edges.map((edge, index) => ({
    data: {
      id: `edge:${index}`,
      source: edge.from,
      target: edge.to,
      label: edge.label,
      kind: edge.label
    }
  }));
  return [...nodes, ...edges];
}

function visualizationHtml(cytoscapeUri: vscode.Uri, graph: RuntimeGraph): string {
  const nonce = String(Date.now());
  const elementsJson = escapeScriptJson(JSON.stringify(cytoscapeElements(graph)));
  const graphJson = escapeScriptJson(JSON.stringify(graph));
  return `<!doctype html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta http-equiv="Content-Security-Policy" content="default-src 'none'; img-src data:; style-src 'unsafe-inline'; script-src 'nonce-${nonce}';">
  <style>
    :root {
      --line: var(--vscode-panel-border);
      --muted: var(--vscode-descriptionForeground);
      --surface: color-mix(in srgb, var(--vscode-editor-background), var(--vscode-sideBar-background) 24%);
      --surface-2: color-mix(in srgb, var(--vscode-editor-background), var(--vscode-sideBar-background) 42%);
      --accent: var(--vscode-focusBorder);
    }
    body {
      margin: 0;
      background: var(--vscode-editor-background);
      color: var(--vscode-editor-foreground);
      font-family: var(--vscode-font-family);
      overflow: hidden;
    }
    .app {
      display: grid;
      grid-template-rows: auto 1fr;
      height: 100vh;
    }
    header {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 12px;
      padding: 10px 12px;
      border-bottom: 1px solid var(--line);
      color: var(--vscode-descriptionForeground);
      font-size: 12px;
    }
    .title {
      display: flex;
      flex-direction: column;
      gap: 2px;
      min-width: 220px;
    }
    .title strong {
      color: var(--vscode-editor-foreground);
      font-size: 13px;
    }
    .workspace {
      display: grid;
      grid-template-columns: minmax(420px, 1fr) 360px;
      min-height: 0;
    }
    .main {
      display: grid;
      grid-template-rows: auto 1fr;
      min-width: 0;
      min-height: 0;
      border-right: 1px solid var(--line);
    }
    .tabs, .toolbar, .filters, .legend, .summary {
      display: flex;
      flex-wrap: wrap;
      align-items: center;
      gap: 6px;
    }
    .tabs {
      padding: 8px 10px;
      border-bottom: 1px solid var(--line);
      background: var(--surface);
    }
    .tab {
      border: 1px solid var(--line);
      border-radius: 6px;
      background: var(--vscode-button-secondaryBackground);
      color: var(--vscode-button-secondaryForeground);
      padding: 5px 11px;
      cursor: pointer;
      transition: background-color .12s ease, border-color .12s ease, transform .06s ease;
    }
    .tab:hover { transform: translateY(-1px); }
    .tab.active {
      background: var(--vscode-button-background);
      color: var(--vscode-button-foreground);
      border-color: var(--accent);
      box-shadow: 0 0 0 1px var(--accent) inset;
    }
    .view {
      display: none;
      min-height: 0;
      overflow: auto;
    }
    .view.active { display: block; }
    .view.graph-view {
      overflow: hidden;
      position: relative;
    }
    .graph-toolbar {
      position: absolute;
      z-index: 2;
      left: 10px;
      right: 10px;
      top: 10px;
      display: flex;
      flex-wrap: wrap;
      justify-content: space-between;
      gap: 8px;
      pointer-events: none;
    }
    .graph-toolbar > * { pointer-events: auto; }
    .search {
      min-width: 220px;
      border: 1px solid var(--line);
      border-radius: 5px;
      background: var(--vscode-input-background);
      color: var(--vscode-input-foreground);
      padding: 5px 8px;
      transition: border-color .12s ease, box-shadow .12s ease;
    }
    .search:focus {
      outline: none;
      border-color: var(--accent);
      box-shadow: 0 0 0 2px color-mix(in srgb, var(--accent), transparent 75%);
    }
    .panel {
      padding: 12px;
      min-height: 0;
    }
    .side {
      min-height: 0;
      overflow: auto;
      background: var(--surface);
    }
    .section {
      border-bottom: 1px solid var(--line);
      padding: 12px;
    }
    .section h2 {
      margin: 0 0 8px;
      font-size: 12px;
      font-weight: 600;
      color: var(--vscode-editor-foreground);
    }
    .metric-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(120px, 1fr));
      gap: 8px;
      margin-bottom: 12px;
    }
    .metric {
      border: 1px solid var(--line);
      border-left: 3px solid var(--accent);
      border-radius: 6px;
      padding: 8px 10px;
      background: var(--surface-2);
      transition: border-color .15s ease;
    }
    .metric:hover { border-color: var(--accent); }
    .metric b {
      display: block;
      font-size: 20px;
      color: var(--vscode-editor-foreground);
      line-height: 1.1;
    }
    .metric span { color: var(--muted); font-size: 11px; }
    .chart {
      width: 100%;
      height: 210px;
      border: 1px solid var(--line);
      border-radius: 8px;
      background: var(--vscode-editor-background);
      margin-bottom: 12px;
      box-shadow: 0 1px 3px rgba(0,0,0,.12);
    }
    .quality-list, .detail-list {
      display: grid;
      gap: 8px;
    }
    .notice {
      border-left: 3px solid var(--vscode-editorWarning-foreground, #d97706);
      background: color-mix(in srgb, var(--vscode-editorWarning-foreground, #d97706), transparent 88%);
      padding: 8px;
      font-size: 12px;
    }
    .ok {
      border-left-color: var(--vscode-testing-iconPassed, #22c55e);
      background: color-mix(in srgb, var(--vscode-testing-iconPassed, #22c55e), transparent 90%);
    }
    table {
      border-collapse: collapse;
      width: 100%;
      font-size: 12px;
    }
    th, td {
      border-bottom: 1px solid var(--line);
      padding: 6px 8px;
      text-align: left;
      vertical-align: top;
    }
    th {
      position: sticky;
      top: 0;
      z-index: 1;
      background: var(--surface);
      color: var(--muted);
      font-weight: 600;
    }
    tr:hover td { background: var(--surface); }
    label {
      display: inline-flex;
      align-items: center;
      gap: 3px;
      border: 1px solid var(--line);
      border-radius: 4px;
      padding: 4px 7px;
      color: var(--vscode-foreground);
      background: var(--surface-2);
    }
    input { margin: 0; }
    button {
      border: 1px solid var(--vscode-button-border, transparent);
      border-radius: 5px;
      background: var(--vscode-button-secondaryBackground);
      color: var(--vscode-button-secondaryForeground);
      padding: 4px 9px;
      cursor: pointer;
      transition: background-color .12s ease, transform .06s ease;
    }
    button:hover { background: var(--vscode-button-secondaryHoverBackground); }
    button:active { transform: translateY(1px); }
    #graph {
      width: 100%;
      height: 100%;
      background:
        linear-gradient(var(--line) 1px, transparent 1px),
        linear-gradient(90deg, var(--line) 1px, transparent 1px);
      background-size: 28px 28px;
      background-color: var(--vscode-editor-background);
    }
    .legend span {
      border: 1px solid var(--line);
      border-radius: 5px;
      padding: 2px 7px;
      transition: border-color .12s ease;
    }
    .legend span:hover { border-color: var(--accent); }
    .tag {
      display: inline-block;
      border: 1px solid var(--line);
      border-radius: 4px;
      padding: 1px 5px;
      margin: 0 3px 3px 0;
      color: var(--muted);
    }
    .hidden { display: none; }
  </style>
</head>
<body>
  <div class="app">
    <header>
      <div class="title">
        <strong>Felidae Visual Analytics</strong>
        <span>Debugger snapshot for data querying, noisy-log profiling, and exportable diagrams.</span>
      </div>
      <div class="summary" id="summary"></div>
      <div class="toolbar">
        <button id="downloadSvg">Export SVG</button>
        <button id="downloadHtml">Export HTML</button>
      </div>
    </header>
    <div class="workspace">
      <main class="main">
        <nav class="tabs">
          <button class="tab active" data-view="graphView">Graph</button>
          <button class="tab" data-view="profileView">Profile</button>
          <button class="tab" data-view="qualityView">Quality</button>
          <button class="tab" data-view="tableView">Data Table</button>
          <input id="search" class="search" placeholder="filter labels, kinds, details, edge labels">
        </nav>
        <section id="graphView" class="view graph-view active">
          <div class="graph-toolbar">
            <div class="toolbar">
              <button id="fit">Fit</button>
              <button id="data">Force</button>
              <button id="flow">Flow</button>
              <button id="circle">Circle</button>
            </div>
            <div class="filters">
              <label><input type="checkbox" data-kind="fact" checked> facts</label>
              <label><input type="checkbox" data-kind="global" checked> outputs</label>
              <label><input type="checkbox" data-kind="method" checked> methods</label>
              <label><input type="checkbox" data-kind="library" checked> libraries</label>
              <label><input type="checkbox" data-kind="field"> fields</label>
            </div>
          </div>
          <div id="graph"></div>
        </section>
        <section id="profileView" class="view panel">
          <div class="metric-grid" id="metrics"></div>
          <svg id="kindChart" class="chart" role="img"></svg>
          <svg id="edgeChart" class="chart" role="img"></svg>
        </section>
        <section id="qualityView" class="view panel">
          <div class="quality-list" id="quality"></div>
        </section>
        <section id="tableView" class="view panel">
          <table>
            <thead><tr><th>Kind</th><th>Label</th><th>Degree</th><th>Signals</th><th>Detail</th></tr></thead>
            <tbody id="nodeRows"></tbody>
          </table>
        </section>
      </main>
      <aside class="side">
        <div class="section">
          <h2>Selection</h2>
          <div id="selection" class="detail-list">Select a graph node or table row.</div>
        </div>
        <div class="section">
          <h2>Legend</h2>
          <div class="legend">
            <span>facts</span><span>fields</span><span>outputs</span><span>methods</span><span>libraries</span>
          </div>
        </div>
        <div class="section">
          <h2>Data-Log Use</h2>
          <div class="detail-list">
            <div class="notice ok">Use Felidae rules and lambda filters to shape the snapshot before visualizing.</div>
            <div class="notice">Quality warnings highlight isolated types, duplicate labels, high fan-out, and sparse metadata that often appear in faulty or noisy logs.</div>
          </div>
        </div>
      </aside>
    </div>
  </div>
  <script nonce="${nonce}" src="${cytoscapeUri}"></script>
  <script nonce="${nonce}">
    const graph = JSON.parse("${graphJson}");
    const elements = JSON.parse("${elementsJson}");
    const colors = {
      fact: { bg: "#dbeafe", border: "#3b82f6", text: "#172554" },
      field: { bg: "#f8fafc", border: "#94a3b8", text: "#334155" },
      global: { bg: "#ede9fe", border: "#7c3aed", text: "#2e1065" },
      method: { bg: "#dcfce7", border: "#22c55e", text: "#052e16" },
      library: { bg: "#ffedd5", border: "#f97316", text: "#431407" },
      value: { bg: "#ffffff", border: "#d1d5db", text: "#1f2937" }
    };
    const visibleKinds = new Set(["fact", "global", "method", "library"]);
    const state = { query: "" };
    const nodeById = new Map(graph.nodes.map(node => [node.id, node]));
    const degree = new Map(graph.nodes.map(node => [node.id, { in: 0, out: 0 }]));
    graph.edges.forEach(edge => {
      if (degree.has(edge.from)) degree.get(edge.from).out++;
      if (degree.has(edge.to)) degree.get(edge.to).in++;
    });
    const countsByKind = countBy(graph.nodes, node => node.kind);
    const countsByEdge = countBy(graph.edges, edge => edge.label || "edge");
    const duplicateLabels = duplicateGroups(graph.nodes, node => node.label);
    const qualitySignals = buildQualitySignals();
    const cy = cytoscape({
      container: document.getElementById("graph"),
      elements,
      minZoom: 0.12,
      maxZoom: 2.5,
      wheelSensitivity: 0.18,
      style: [
        {
          selector: "node",
          style: {
            "shape": "round-rectangle",
            "width": "label",
            "height": "label",
            "padding": "10px",
            "label": "data(label)",
            "font-size": 12,
            "font-family": "var(--vscode-font-family)",
            "text-valign": "center",
            "text-halign": "center",
            "background-color": ele => (colors[ele.data("kind")] || colors.value).bg,
            "border-color": ele => (colors[ele.data("kind")] || colors.value).border,
            "color": ele => (colors[ele.data("kind")] || colors.value).text,
            "border-width": 1.4,
            "text-wrap": "wrap",
            "text-max-width": 130
          }
        },
        { selector: "node[kind = 'field']", style: { "shape": "ellipse", "font-size": 10, "padding": "6px" } },
        { selector: "node[kind = 'global']", style: { "shape": "hexagon" } },
        { selector: "node[kind = 'library']", style: { "shape": "tag" } },
        {
          selector: "edge",
          style: {
            "curve-style": "bezier",
            "target-arrow-shape": "triangle",
            "target-arrow-color": "#64748b",
            "line-color": "#94a3b8",
            "width": 1.3,
            "label": "data(label)",
            "font-size": 9,
            "text-background-color": "var(--vscode-editor-background)",
            "text-background-opacity": 0.86,
            "text-background-padding": "2px",
            "color": "#64748b"
          }
        },
        { selector: ":selected", style: { "border-width": 3, "line-color": "#f59e0b", "target-arrow-color": "#f59e0b" } }
      ],
      layout: {
        name: "cose",
        animate: false,
        randomize: true,
        nodeRepulsion: 9500,
        idealEdgeLength: 110,
        edgeElasticity: 80,
        nestingFactor: 1.2,
        gravity: 0.9,
        numIter: 1800
      }
    });
    function countBy(items, keyFn) {
      const result = {};
      items.forEach(item => {
        const key = keyFn(item) || "unknown";
        result[key] = (result[key] || 0) + 1;
      });
      return result;
    }
    function duplicateGroups(items, keyFn) {
      const groups = {};
      items.forEach(item => {
        const key = keyFn(item);
        if (!key) return;
        (groups[key] ||= []).push(item);
      });
      return Object.values(groups).filter(group => group.length > 1);
    }
    function nodeMatches(node) {
      if (!state.query) return true;
      const needle = state.query.toLowerCase();
      const d = degree.get(node.id) || { in: 0, out: 0 };
      const edgeLabels = graph.edges
        .filter(edge => edge.from === node.id || edge.to === node.id)
        .map(edge => edge.label)
        .join(" ");
      return [node.label, node.kind, node.detail || "", edgeLabels, String(d.in), String(d.out)]
        .join(" ")
        .toLowerCase()
        .includes(needle);
    }
    function buildQualitySignals() {
      const signals = [];
      const isolated = graph.nodes.filter(node => {
        const d = degree.get(node.id);
        return d && d.in + d.out === 0;
      });
      if (isolated.length) signals.push({
        level: "warn",
        title: "Isolated data points",
        body: isolated.slice(0, 12).map(node => node.label).join(", ") + (isolated.length > 12 ? " ..." : ""),
        count: isolated.length
      });
      if (duplicateLabels.length) signals.push({
        level: "warn",
        title: "Duplicate labels across nodes",
        body: duplicateLabels.slice(0, 8).map(group => group[0].label + " x" + group.length).join(", "),
        count: duplicateLabels.length
      });
      const highFanout = graph.nodes.filter(node => (degree.get(node.id)?.out || 0) >= 8);
      if (highFanout.length) signals.push({
        level: "warn",
        title: "High fan-out hubs",
        body: highFanout.map(node => node.label + " -> " + degree.get(node.id).out).join(", "),
        count: highFanout.length
      });
      const sparse = graph.nodes.filter(node => (node.kind === "fact" || node.kind === "global") && !node.detail && (degree.get(node.id)?.out || 0) === 0);
      if (sparse.length) signals.push({
        level: "info",
        title: "Sparse runtime metadata",
        body: sparse.slice(0, 10).map(node => node.label).join(", ") + (sparse.length > 10 ? " ..." : ""),
        count: sparse.length
      });
      const unlabeledEdges = graph.edges.filter(edge => !edge.label || edge.label === "edge");
      if (unlabeledEdges.length) signals.push({
        level: "info",
        title: "Unlabeled relationships",
        body: "Add explicit rule names or richer runtime metadata when these relationships matter for data analysis.",
        count: unlabeledEdges.length
      });
      if (!signals.length) signals.push({
        level: "ok",
        title: "No obvious quality warnings",
        body: "The snapshot has connected nodes, unique labels, and labeled relationships.",
        count: 0
      });
      return signals;
    }
    function applyFilters() {
      cy.batch(() => {
        cy.nodes().forEach(node => {
          const model = nodeById.get(node.id());
          const visible = visibleKinds.has(node.data("kind")) && (!model || nodeMatches(model));
          node.style("display", visible ? "element" : "none");
        });
        cy.edges().forEach(edge => {
          const visible = edge.source().visible() && edge.target().visible();
          edge.style("display", visible ? "element" : "none");
        });
      });
      renderTables();
    }
    function runLayout(name) {
      const options = name === "breadthfirst"
        ? { name, directed: true, spacingFactor: 1.18, animate: true, animationDuration: 260 }
        : name === "circle"
          ? { name: "circle", animate: true, animationDuration: 260, spacingFactor: 1.18 }
          : { name: "cose", animate: true, animationDuration: 260, randomize: false, nodeRepulsion: 9500, idealEdgeLength: 110, edgeElasticity: 80, gravity: 0.9, numIter: 900 };
      cy.layout(options).run();
    }
    function renderSummary() {
      document.getElementById("summary").innerHTML = [
        ["Nodes", graph.nodes.length],
        ["Edges", graph.edges.length],
        ["Fact types", countsByKind.fact || 0],
        ["Methods", countsByKind.method || 0],
        ["Warnings", qualitySignals.filter(item => item.level === "warn").length]
      ].map(([label, value]) => '<span class="tag">' + label + ': ' + value + '</span>').join("");
    }
    function renderMetrics() {
      const metrics = [
        ["Nodes", graph.nodes.length],
        ["Edges", graph.edges.length],
        ["Fact types", countsByKind.fact || 0],
        ["Fields", countsByKind.field || 0],
        ["Outputs", countsByKind.global || 0],
        ["Libraries", countsByKind.library || 0],
        ["Quality signals", qualitySignals.length]
      ];
      document.getElementById("metrics").innerHTML = metrics
        .map(([label, value]) => '<div class="metric"><b>' + value + '</b><span>' + label + '</span></div>')
        .join("");
      renderBarChart(document.getElementById("kindChart"), countsByKind, "Node profile");
      renderBarChart(document.getElementById("edgeChart"), countsByEdge, "Relationship profile");
    }
    function renderBarChart(svg, counts, title) {
      const entries = Object.entries(counts).sort((a, b) => b[1] - a[1]).slice(0, 12);
      const width = svg.clientWidth || 720;
      const height = svg.clientHeight || 210;
      const max = Math.max(1, ...entries.map(item => item[1]));
      const barHeight = Math.max(12, Math.floor((height - 44) / Math.max(1, entries.length)) - 4);
      const rows = entries.map(([label, count], index) => {
        const y = 34 + index * (barHeight + 4);
        const w = Math.max(2, Math.round((width - 180) * count / max));
        return '<text x="12" y="' + (y + barHeight - 2) + '" font-size="11" fill="currentColor">' + xmlEscape(label) + '</text>' +
          '<rect x="130" y="' + y + '" width="' + w + '" height="' + barHeight + '" rx="3" fill="#3b82f6"/>' +
          '<text x="' + (138 + w) + '" y="' + (y + barHeight - 2) + '" font-size="11" fill="currentColor">' + count + '</text>';
      }).join("");
      svg.setAttribute("viewBox", "0 0 " + width + " " + height);
      svg.innerHTML = '<text x="12" y="20" font-size="12" font-weight="600" fill="currentColor">' + xmlEscape(title) + '</text>' + rows;
    }
    function renderQuality() {
      document.getElementById("quality").innerHTML = qualitySignals.map(item => {
        const cls = item.level === "ok" ? "notice ok" : "notice";
        return '<div class="' + cls + '"><b>' + xmlEscape(item.title) + '</b>' +
          '<div>' + xmlEscape(item.body) + '</div>' +
          '<span class="tag">count: ' + item.count + '</span></div>';
      }).join("");
    }
    function nodeSignals(node) {
      const signals = [];
      const d = degree.get(node.id) || { in: 0, out: 0 };
      if (d.in + d.out === 0) signals.push("isolated");
      if (d.out >= 8) signals.push("hub");
      if (!node.detail && (node.kind === "fact" || node.kind === "global")) signals.push("sparse");
      if (duplicateLabels.some(group => group.some(item => item.id === node.id))) signals.push("duplicate label");
      return signals;
    }
    function renderTables() {
      const rows = graph.nodes
        .filter(node => visibleKinds.has(node.kind) && nodeMatches(node))
        .sort((a, b) => a.kind.localeCompare(b.kind) || a.label.localeCompare(b.label))
        .map(node => {
          const d = degree.get(node.id) || { in: 0, out: 0 };
          const signals = nodeSignals(node);
          return '<tr data-node="' + xmlEscape(node.id) + '"><td>' + xmlEscape(node.kind) + '</td><td>' + xmlEscape(node.label) + '</td><td>' + (d.in + d.out) + ' <span class="tag">in ' + d.in + '</span><span class="tag">out ' + d.out + '</span></td><td>' + (signals.length ? signals.map(s => '<span class="tag">' + xmlEscape(s) + '</span>').join("") : '<span class="tag">ok</span>') + '</td><td>' + xmlEscape(node.detail || "") + '</td></tr>';
        }).join("");
      document.getElementById("nodeRows").innerHTML = rows || '<tr><td colspan="5">No nodes match the active filters.</td></tr>';
      document.querySelectorAll("tr[data-node]").forEach(row => {
        row.addEventListener("click", () => selectNode(row.dataset.node));
      });
    }
    function selectNode(id) {
      const node = nodeById.get(id);
      if (!node) return;
      cy.elements().unselect();
      const cyNode = cy.getElementById(id);
      cyNode.select();
      cyNode.connectedEdges().select();
      const d = degree.get(id) || { in: 0, out: 0 };
      const incoming = graph.edges.filter(edge => edge.to === id).map(edge => nodeById.get(edge.from)?.label + " -" + edge.label + "-> " + node.label);
      const outgoing = graph.edges.filter(edge => edge.from === id).map(edge => node.label + " -" + edge.label + "-> " + nodeById.get(edge.to)?.label);
      document.getElementById("selection").innerHTML =
        '<div><b>' + xmlEscape(node.label) + '</b> <span class="tag">' + xmlEscape(node.kind) + '</span></div>' +
        '<div><span class="tag">degree ' + (d.in + d.out) + '</span><span class="tag">in ' + d.in + '</span><span class="tag">out ' + d.out + '</span></div>' +
        (node.detail ? '<div>' + xmlEscape(node.detail) + '</div>' : '') +
        '<div>' + nodeSignals(node).map(signal => '<span class="tag">' + xmlEscape(signal) + '</span>').join("") + '</div>' +
        '<div><b>Incoming</b><br>' + (incoming.length ? incoming.map(xmlEscape).join("<br>") : "none") + '</div>' +
        '<div><b>Outgoing</b><br>' + (outgoing.length ? outgoing.map(xmlEscape).join("<br>") : "none") + '</div>';
    }
    function xmlEscape(value) {
      return String(value)
        .replace(/&/g, "&amp;")
        .replace(/</g, "&lt;")
        .replace(/>/g, "&gt;")
        .replace(/"/g, "&quot;");
    }
    function svgColor(kind, key) {
      return (colors[kind] || colors.value)[key];
    }
    function nodeShape(node, x, y, width, height) {
      const kind = node.data("kind");
      const fill = svgColor(kind, "bg");
      const stroke = svgColor(kind, "border");
      if (kind === "field") {
        return '<ellipse cx="' + (x + width / 2) + '" cy="' + (y + height / 2) + '" rx="' + (width / 2) + '" ry="' + (height / 2) + '" fill="' + fill + '" stroke="' + stroke + '" stroke-width="1.4"/>';
      }
      if (kind === "global") {
        const points = [
          [x + width * 0.22, y], [x + width * 0.78, y], [x + width, y + height / 2],
          [x + width * 0.78, y + height], [x + width * 0.22, y + height], [x, y + height / 2]
        ].map(point => point.join(",")).join(" ");
        return '<polygon points="' + points + '" fill="' + fill + '" stroke="' + stroke + '" stroke-width="1.4"/>';
      }
      return '<rect x="' + x + '" y="' + y + '" width="' + width + '" height="' + height + '" rx="8" fill="' + fill + '" stroke="' + stroke + '" stroke-width="1.4"/>';
    }
    function downloadVisibleSvg() {
      const visible = cy.elements(":visible");
      const visibleNodes = cy.nodes(":visible");
      const visibleEdges = cy.edges(":visible");
      if (visibleNodes.length === 0) return;
      const bounds = visible.boundingBox({ includeLabels: true });
      const margin = 36;
      const width = Math.max(320, Math.ceil(bounds.w + margin * 2));
      const height = Math.max(240, Math.ceil(bounds.h + margin * 2));
      const ox = margin - bounds.x1;
      const oy = margin - bounds.y1;
      const parts = [
        '<svg xmlns="http://www.w3.org/2000/svg" width="' + width + '" height="' + height + '" viewBox="0 0 ' + width + ' ' + height + '">',
        '<defs><marker id="arrow" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse"><path d="M 0 0 L 10 5 L 0 10 z" fill="#64748b"/></marker></defs>',
        '<rect width="100%" height="100%" fill="#ffffff"/>'
      ];
      visibleEdges.forEach(edge => {
        const source = edge.source().position();
        const target = edge.target().position();
        const x1 = source.x + ox;
        const y1 = source.y + oy;
        const x2 = target.x + ox;
        const y2 = target.y + oy;
        const mx = (x1 + x2) / 2;
        const my = (y1 + y2) / 2;
        parts.push('<line x1="' + x1 + '" y1="' + y1 + '" x2="' + x2 + '" y2="' + y2 + '" stroke="#94a3b8" stroke-width="1.3" marker-end="url(#arrow)"/>');
        parts.push('<text x="' + mx + '" y="' + (my - 4) + '" text-anchor="middle" font-size="9" font-family="Arial, sans-serif" fill="#64748b">' + xmlEscape(edge.data("label")) + '</text>');
      });
      visibleNodes.forEach(node => {
        const box = node.boundingBox({ includeLabels: true });
        const x = box.x1 + ox - 4;
        const y = box.y1 + oy - 4;
        const width = box.w + 8;
        const height = box.h + 8;
        const textX = x + width / 2;
        const textY = y + height / 2 + 4;
        parts.push(nodeShape(node, x, y, width, height));
        parts.push('<text x="' + textX + '" y="' + textY + '" text-anchor="middle" font-size="12" font-family="Arial, sans-serif" fill="' + svgColor(node.data("kind"), "text") + '">' + xmlEscape(node.data("label")) + '</text>');
      });
      parts.push("</svg>");
      const blob = new Blob([parts.join("")], { type: "image/svg+xml" });
      downloadBlob(blob, "felidae-visualization.svg");
    }
    function downloadBlob(blob, fileName) {
      const url = URL.createObjectURL(blob);
      const link = document.createElement("a");
      link.href = url;
      link.download = fileName;
      document.body.appendChild(link);
      link.click();
      link.remove();
      URL.revokeObjectURL(url);
    }
    function downloadHtml() {
      const html = "<!doctype html>\\n" + document.documentElement.outerHTML;
      downloadBlob(new Blob([html], { type: "text/html" }), "felidae-visualization.html");
    }
    document.querySelectorAll(".tab").forEach(tab => {
      tab.addEventListener("click", () => {
        document.querySelectorAll(".tab").forEach(item => item.classList.remove("active"));
        document.querySelectorAll(".view").forEach(item => item.classList.remove("active"));
        tab.classList.add("active");
        document.getElementById(tab.dataset.view).classList.add("active");
        cy.resize();
        cy.fit(cy.elements(":visible"), 42);
      });
    });
    document.getElementById("fit").addEventListener("click", () => cy.fit(undefined, 42));
    document.getElementById("data").addEventListener("click", () => runLayout("cose"));
    document.getElementById("flow").addEventListener("click", () => runLayout("breadthfirst"));
    document.getElementById("circle").addEventListener("click", () => runLayout("circle"));
    document.getElementById("downloadSvg").addEventListener("click", downloadVisibleSvg);
    document.getElementById("downloadHtml").addEventListener("click", downloadHtml);
    document.getElementById("search").addEventListener("input", event => {
      state.query = event.target.value.trim();
      applyFilters();
      cy.fit(cy.elements(":visible"), 42);
    });
    document.querySelectorAll("input[data-kind]").forEach(input => {
      input.addEventListener("change", event => {
        const box = event.target;
        if (box.checked) visibleKinds.add(box.dataset.kind);
        else visibleKinds.delete(box.dataset.kind);
        applyFilters();
        cy.fit(cy.elements(":visible"), 42);
      });
    });
    cy.on("tap", "node", event => {
      const node = event.target;
      selectNode(node.id());
    });
    cy.on("tap", event => {
      if (event.target === cy) cy.elements().unselect();
    });
    window.addEventListener("resize", () => {
      renderMetrics();
      cy.resize();
    });
    renderSummary();
    renderMetrics();
    renderQuality();
    renderTables();
    applyFilters();
    cy.fit(cy.elements(":visible"), 42);
  </script>
</body>
</html>`;
}

function escapeScriptJson(value: string): string {
  return value
    .replace(/\\/g, "\\\\")
    .replace(/"/g, "\\\"")
    .replace(/</g, "\\u003c")
    .replace(/>/g, "\\u003e")
    .replace(/&/g, "\\u0026");
}

function resolveInterpreterPath(documentUri: vscode.Uri): string {
  const config = vscode.workspace.getConfiguration("felidae");
  const configuredPath = config.get<string>("interpreterPath", "build/felidae.exe");
  return resolveConfiguredPath(documentUri, configuredPath);
}

function resolveDebugInterpreterPath(documentUri: vscode.Uri): string {
  const debuggerFromEnv = process.env.FELIDAE_DEBUG_PATH;
  if (debuggerFromEnv && fs.existsSync(debuggerFromEnv)) return debuggerFromEnv;

  const config = vscode.workspace.getConfiguration("felidae");
  return resolveConfiguredPath(
    documentUri,
    config.get<string>("debugInterpreterPath", "build/felidae_debug.exe")
  );
}

function resolveCelidaePath(documentUri: vscode.Uri): string {
  const celidaeFromEnv = process.env.CELIDAE_PATH;
  if (celidaeFromEnv && fs.existsSync(celidaeFromEnv)) return celidaeFromEnv;

  const config = vscode.workspace.getConfiguration("felidae");
  return resolveConfiguredPath(
    documentUri,
    config.get<string>("celidaePath", "build/celidae.exe")
  );
}

function resolveConfiguredPath(documentUri: vscode.Uri, configuredPath: string): string {
  if (path.isAbsolute(configuredPath)) {
    return configuredPath;
  }

  const workspaceFolder = vscode.workspace.getWorkspaceFolder(documentUri);
  if (workspaceFolder) {
    return path.join(workspaceFolder.uri.fsPath, configuredPath);
  }

  return configuredPath;
}

async function ensureInterpreterInstalled(
  interpreterPath: string,
  label: string,
  settingsQuery: "felidae.interpreterPath" | "felidae.debugInterpreterPath" | "felidae.celidaePath" = "felidae.interpreterPath"
): Promise<boolean> {
  if (fs.existsSync(interpreterPath)) return true;
  const downloadLabel = settingsQuery === "felidae.celidaePath" ? "Download Celidae" : "Download Felidae";
  const choice = await vscode.window.showWarningMessage(
    `You have not installed the ${label}, or the configured path was not found: ${interpreterPath}`,
    downloadLabel,
    "Open Settings"
  );
  if (choice === downloadLabel) {
    await vscode.env.openExternal(vscode.Uri.parse("https://github.com/xnvtserver/Felidae/releases"));
  } else if (choice === "Open Settings") {
    await vscode.commands.executeCommand("workbench.action.openSettings", settingsQuery);
  }
  return false;
}

interface FelidaeSymbolDefinition {
  name: string;
  count: number;
  spans: Array<{ startLine: number; startColumn: number; endLine: number; endColumn: number }>;
}

interface FelidaeSymbolSummary {
  methods: FelidaeSymbolDefinition[];
  facts: FelidaeSymbolDefinition[];
  globals: FelidaeSymbolDefinition[];
  files: string[];
  unresolvedImports: string[];
}

// Best-effort cache of `felidae_debug <file> --symbols-json --load-imports`
// results, keyed by document URI. Populated in the background on the same
// debounce cycle as diagnostics; completion reads it synchronously and falls
// back to text-scanning when no entry exists yet (e.g. right after opening a
// file, or against a felidae_debug build too old to support the flag).
const symbolSummaryCache = new Map<string, FelidaeSymbolSummary>();

function refreshSymbolCache(document: vscode.TextDocument): void {
  if (document.uri.scheme !== "file" || document.languageId !== "felidae") return;
  const interpreterPath = resolveDebugInterpreterPath(document.uri);
  if (!fs.existsSync(interpreterPath)) return;
  childProcess.execFile(
    interpreterPath,
    [document.uri.fsPath, "--symbols-json", "--load-imports"],
    { cwd: path.dirname(document.uri.fsPath), windowsHide: true, timeout: 10000 },
    (error, stdout) => {
      if (error) return;
      try {
        const parsed = JSON.parse(stdout.trim()) as FelidaeSymbolSummary;
        if (parsed && Array.isArray(parsed.methods) && Array.isArray(parsed.facts)) {
          symbolSummaryCache.set(document.uri.toString(), parsed);
        }
      } catch {
        // Older felidae_debug builds without --symbols-json, or a transient
        // parse failure mid-edit. Completion silently keeps using text scans.
      }
    }
  );
}

function runtimeCheckDiagnostics(document: vscode.TextDocument): Promise<vscode.Diagnostic[]> {
  return new Promise((resolve) => {
    if (document.uri.scheme !== "file") {
      resolve([]);
      return;
    }
    const interpreterPath = resolveDebugInterpreterPath(document.uri);
    if (!fs.existsSync(interpreterPath)) {
      const range = new vscode.Range(new vscode.Position(0, 0), new vscode.Position(0, 1));
      resolve([new vscode.Diagnostic(
        range,
        `Felidae AST debugger not found: ${interpreterPath}. Parser and AST validation via --check-json is disabled.`,
        vscode.DiagnosticSeverity.Warning
      )]);
      return;
    }
    childProcess.execFile(
      interpreterPath,
      [document.uri.fsPath, "--check-json"],
      { cwd: path.dirname(document.uri.fsPath), windowsHide: true, timeout: 15000 },
      (error, stdout, stderr) => {
        const jsonDiagnostics = parseRuntimeJsonDiagnostics(document, stdout);
        if (jsonDiagnostics) {
          resolve(jsonDiagnostics);
          return;
        }
        const analyzerDiagnostics = parseRuntimeAnalyzerDiagnostics(document, stdout);
        if (!error && stdout.includes("FELIDAE_CHECK_OK")) {
          resolve(analyzerDiagnostics);
          return;
        }
        const text = stderr.trim() || error?.message || "Felidae check failed.";
        const { message, severity } = formatRuntimeCheckMessage(text);
        const lineMatch = / at (\d+):(\d+)/.exec(message);
        const line = lineMatch ? Math.max(0, Number(lineMatch[1]) - 1) : 0;
        const column = lineMatch ? Math.max(0, Number(lineMatch[2]) - 1) : 0;
        const range = new vscode.Range(
          new vscode.Position(line, column),
          new vscode.Position(line, column + 1)
        );
        resolve([...analyzerDiagnostics, new vscode.Diagnostic(range, message, severity)]);
      }
    );
  });
}

function parseRuntimeJsonDiagnostics(document: vscode.TextDocument, stdout: string): vscode.Diagnostic[] | undefined {
  const text = stdout.trim();
  if (!text.startsWith("{")) return undefined;

  try {
    const payload = JSON.parse(text) as {
      diagnostics?: Array<{
        severity?: string;
        line?: number;
        column?: number;
        message?: string;
      }>;
    };
    if (!Array.isArray(payload.diagnostics)) return undefined;

    return payload.diagnostics
      .filter((item) => typeof item.message === "string" && item.message.trim().length > 0)
      .map((item) => {
        const sourceLine = Math.max(0, Number(item.line ?? 1) - 1);
        const sourceColumn = Math.max(0, Number(item.column ?? 1) - 1);
        const boundedLine = Math.min(sourceLine, Math.max(0, document.lineCount - 1));
        const lineText = document.lineAt(boundedLine).text;
        const boundedColumn = Math.min(sourceColumn, lineText.length);
        const severity = item.severity === "error"
          ? vscode.DiagnosticSeverity.Error
          : item.severity === "info"
            ? vscode.DiagnosticSeverity.Information
            : item.severity === "hint"
              ? vscode.DiagnosticSeverity.Hint
              : vscode.DiagnosticSeverity.Warning;
        return new vscode.Diagnostic(
          new vscode.Range(
            new vscode.Position(boundedLine, boundedColumn),
            new vscode.Position(boundedLine, Math.min(boundedColumn + 1, lineText.length))
          ),
          item.message ?? "Felidae AST diagnostic",
          severity
        );
      });
  } catch {
    return undefined;
  }
}

function parseRuntimeAnalyzerDiagnostics(document: vscode.TextDocument, stdout: string): vscode.Diagnostic[] {
  const diagnostics: vscode.Diagnostic[] = [];
  for (const line of stdout.split(/\r?\n/)) {
    if (!line.startsWith("FELIDAE_DIAGNOSTIC ")) continue;
    const severityMatch = /\bseverity=(error|warning|info|hint)\b/.exec(line);
    const lineMatch = /\bline=(\d+)\b/.exec(line);
    const columnMatch = /\bcolumn=(\d+)\b/.exec(line);
    const messageMatch = /\bmessage=(.*)$/.exec(line);
    const message = messageMatch?.[1]?.trim();
    if (!message) continue;

    const sourceLine = Math.max(0, Number(lineMatch?.[1] ?? "1") - 1);
    const sourceColumn = Math.max(0, Number(columnMatch?.[1] ?? "1") - 1);
    const boundedLine = Math.min(sourceLine, Math.max(0, document.lineCount - 1));
    const boundedColumn = Math.min(sourceColumn, document.lineAt(boundedLine).text.length);
    const severity = severityMatch?.[1] === "error"
      ? vscode.DiagnosticSeverity.Error
      : severityMatch?.[1] === "info"
        ? vscode.DiagnosticSeverity.Information
        : severityMatch?.[1] === "hint"
          ? vscode.DiagnosticSeverity.Hint
          : vscode.DiagnosticSeverity.Warning;
    diagnostics.push(new vscode.Diagnostic(
      new vscode.Range(
        new vscode.Position(boundedLine, boundedColumn),
        new vscode.Position(boundedLine, Math.min(boundedColumn + 1, document.lineAt(boundedLine).text.length))
      ),
      message,
      severity
    ));
  }
  return diagnostics;
}

function formatRuntimeCheckMessage(text: string): { message: string; severity: vscode.DiagnosticSeverity } {
  const raw = text.trim();
  const severity = /^warning:/i.test(raw) ? vscode.DiagnosticSeverity.Warning : vscode.DiagnosticSeverity.Error;
  let message = raw.replace(/^(error|warning):\s*/i, "");

  const factIteration = /^Fact type '([^']+)' is not implicitly iterable/.exec(message);
  if (factIteration) {
    const name = factIteration[1];
    message = `Fact type '${name}' is not implicitly iterable here. Direct ${name}(...) declarations and named queries are supported, but ${name}(item) in a method body does not scan facts. Use lambda(${name}, item => ...) or iterate an explicit list/array.`;
  } else if (/^Module '.*' not found/.test(message)) {
    message = `${message}. Check the import path, native module name, or workspace-relative Celidae configuration.`;
  } else if (/expects argument/.test(message)) {
    message = `${message}. This was reported by felidae_debug --check-json during parser and AST validation.`;
  } else if (/Unknown field/.test(message)) {
    message = `${message}. Named fact calls must match the declared fact fields.`;
  }

  return { message, severity };
}

class FelidaeCodeActionProvider implements vscode.CodeActionProvider {
  static readonly providedCodeActionKinds = [vscode.CodeActionKind.QuickFix];

  provideCodeActions(
    document: vscode.TextDocument,
    _range: vscode.Range | vscode.Selection,
    context: vscode.CodeActionContext
  ): vscode.ProviderResult<vscode.CodeAction[]> {
    const actions: vscode.CodeAction[] = [];
    for (const diagnostic of context.diagnostics) {
      const notIterable = /^Fact type '([^']+)' is not implicitly iterable/.exec(diagnostic.message);
      if (!notIterable) continue;
      const factName = notIterable[1];
      const line = diagnostic.range.start.line;
      const lineText = document.lineAt(line).text;
      const callPattern = new RegExp(
        `\\b${factName.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}\\s*\\(\\s*([A-Za-z_][A-Za-z0-9_]*)\\s*\\)`
      );
      const callMatch = callPattern.exec(lineText);
      if (!callMatch) continue;

      const itemName = callMatch[1];
      const startChar = callMatch.index;
      const endChar = callMatch.index + callMatch[0].length;
      const replacement = `lambda(${factName}, ${itemName} => ${itemName})`;

      const action = new vscode.CodeAction(
        `Rewrite as lambda(${factName}, ${itemName} => ...)`,
        vscode.CodeActionKind.QuickFix
      );
      action.diagnostics = [diagnostic];
      action.isPreferred = true;
      action.edit = new vscode.WorkspaceEdit();
      action.edit.replace(
        document.uri,
        new vscode.Range(new vscode.Position(line, startChar), new vscode.Position(line, endChar)),
        replacement
      );
      actions.push(action);
    }
    return actions;
  }
}

async function confirmRuntimeCheck(document: vscode.TextDocument, actionLabel: string): Promise<boolean> {
  const interpreterPath = resolveDebugInterpreterPath(document.uri);
  const installed = await ensureInterpreterInstalled(interpreterPath, "Felidae AST debugger", "felidae.debugInterpreterPath");
  if (!installed) return false;

  const diagnostics = await vscode.window.withProgress(
    { location: vscode.ProgressLocation.Notification, title: "Felidae: checking with felidae_debug --check-json..." },
    () => runtimeCheckDiagnostics(document)
  );
  const hasErrors = diagnostics.some((item) => item.severity === vscode.DiagnosticSeverity.Error);
  if (!hasErrors) return true;

  const choice = await vscode.window.showWarningMessage(
    `felidae_debug --check-json reported errors. ${actionLabel} anyway?`,
    `${actionLabel} Anyway`,
    "Cancel"
  );
  return choice === `${actionLabel} Anyway`;
}

async function getFelidaeDocument(uri?: vscode.Uri): Promise<vscode.TextDocument | undefined> {
  if (uri) {
    const document = await vscode.workspace.openTextDocument(uri);
    if (document.languageId === "felidae") {
      return document;
    }
  }

  const editor = vscode.window.activeTextEditor;
  if (editor?.document.languageId === "felidae") {
    return editor.document;
  }

  return undefined;
}

async function runQuery(uri?: vscode.Uri): Promise<void> {
  const document = await getFelidaeDocument(uri);
  if (!document) {
    vscode.window.showWarningMessage("Open a Felidae .fx file before running a query.");
    return;
  }

  if (document.isDirty) {
    await document.save();
  }

  const canRun = await confirmRuntimeCheck(document, "Run");
  if (!canRun) return;

  const config = vscode.workspace.getConfiguration("felidae");
  const defaultQuery = config.get<string>("defaultQuery", "? Engineer(name: name)");
  const editor = vscode.window.activeTextEditor;
  const selectedText = editor?.document.uri.toString() === document.uri.toString()
    ? editor.document.getText(editor.selection).trim()
    : "";
  const query = await vscode.window.showInputBox({
    title: "Run Felidae Query",
    prompt: "Enter a query for the current Felidae file.",
    value: selectedText || defaultQuery
  });

  if (!query) {
    return;
  }

  const interpreterPath = resolveInterpreterPath(document.uri);
  const programPath = document.uri.fsPath;
  const installed = await ensureInterpreterInstalled(interpreterPath, "Felidae interpreter");
  if (!installed) return;
  const command = felidaeTerminalCommand(interpreterPath, [programPath, query]);

  const terminal = vscode.window.createTerminal({ name: "Felidae", cwd: path.dirname(programPath) });
  terminal.show();
  terminal.sendText(command);
}

function hasMainMethod(document: vscode.TextDocument): boolean {
  const text = document.getText();
  return /^\s*main\s*\([^)]*\)\s*=>/m.test(text);
}

async function runMain(uri?: vscode.Uri): Promise<void> {
  const document = await getFelidaeDocument(uri);
  if (!document) {
    vscode.window.showWarningMessage("Open a Felidae .fx file before running main.");
    return;
  }

  if (!hasMainMethod(document)) {
    vscode.window.showWarningMessage("This Felidae file does not define main(...).");
    return;
  }

  if (document.isDirty) {
    await document.save();
  }

  const canRun = await confirmRuntimeCheck(document, "Run");
  if (!canRun) return;

  const interpreterPath = resolveInterpreterPath(document.uri);
  const programPath = document.uri.fsPath;
  if (!fs.existsSync(interpreterPath)) {
    vscode.window.showErrorMessage(`Felidae interpreter not found: ${interpreterPath}`);
    return;
  }
  const installed = await ensureInterpreterInstalled(interpreterPath, "Felidae interpreter");
  if (!installed) return;
  const command = felidaeTerminalCommand(interpreterPath, [programPath]);

  const terminal = vscode.window.createTerminal({ name: "Felidae", cwd: path.dirname(programPath) });
  terminal.show();
  terminal.sendText(command);
}

async function debugMain(uri?: vscode.Uri): Promise<void> {
  const document = await getFelidaeDocument(uri);
  if (!document) {
    vscode.window.showWarningMessage("Open a Felidae .fx file before debugging main.");
    return;
  }

  if (!hasMainMethod(document)) {
    vscode.window.showWarningMessage("This Felidae file does not define main(...).");
    return;
  }

  if (document.isDirty) {
    await document.save();
  }

  const canDebug = await confirmRuntimeCheck(document, "Debug");
  if (!canDebug) return;

  const interpreterPath = resolveDebugInterpreterPath(document.uri);
  const installed = await ensureInterpreterInstalled(interpreterPath, "Felidae AST debugger", "felidae.debugInterpreterPath");
  if (!installed) return;

  const workspaceFolder = vscode.workspace.getWorkspaceFolder(document.uri);
  await vscode.debug.startDebugging(workspaceFolder, {
    type: "felidae",
    request: "launch",
    name: "Debug Felidae Main",
    program: document.uri.fsPath,
    interpreterPath,
    stopOnEntry: true
  });
}

class FelidaeDebugAdapter implements vscode.DebugAdapter {
  private readonly emitter = new vscode.EventEmitter<vscode.DebugProtocolMessage>();
  private static readonly localVariablesReference = 1;
  private process?: childProcess.ChildProcessWithoutNullStreams;
  private stopped = false;
  private currentProgram?: string;
  private currentSource?: string;
  private currentLine = 1;
  private executableLines: number[] = [1];
  private callStack: Array<{ name: string, file: string, line: number, returnFile: string, returnLine: number }> = [];
  private methodDefinitions = new Map<string, { file: string, line: number }>();
  private breakpointsByFile = new Map<string, Set<number>>();
  private fileLinesCache = new Map<string, { mtimeMs: number, lines: string[] }>();
  private stdoutBuffer = "";
  readonly onDidSendMessage = this.emitter.event;

  handleMessage(message: vscode.DebugProtocolMessage): void {
    const request = message as DapRequest;
    if (request.type !== "request") {
      return;
    }

    if (request.command === "initialize") {
      this.sendResponse(request, {
        supportsSetVariable: false,
        supportsEvaluateForHovers: true,
        supportsEvaluateForRepl: true,
        supportsConditionalBreakpoints: false,
        supportsHitConditionalBreakpoints: false,
        supportsConfigurationDoneRequest: false,
        supportsSetBreakpointsRequest: true,
        supportsPauseRequest: true,
        supportsTerminateRequest: true
      });
      this.sendEvent("initialized");
      return;
    }

    if (request.command === "launch") {
      this.launch(request);
      return;
    }

    if (request.command === "threads") {
      this.sendResponse(request, { threads: [{ id: 1, name: "Felidae AST debugger" }] });
      return;
    }

    if (request.command === "stackTrace") {
      const activeFile = this.currentSource ?? this.currentProgram;
      const source = activeFile ? {
        name: path.basename(activeFile),
        path: activeFile
      } : undefined;
      const frames = [{
        id: 1,
        name: this.callStack[this.callStack.length - 1]?.name ?? (this.stopped ? "Felidae simulated step" : "Felidae program"),
        source,
        line: this.currentLine,
        column: 1
      }];
      for (let i = this.callStack.length - 1; i >= 0; i--) {
        const frame = this.callStack[i];
        frames.push({
          id: frames.length + 1,
          name: frame.name,
          source: { name: path.basename(frame.returnFile), path: frame.returnFile },
          line: frame.returnLine,
          column: 1
        });
      }
      this.sendResponse(request, {
        stackFrames: frames,
        totalFrames: frames.length
      });
      return;
    }

    if (request.command === "scopes") {
      this.sendResponse(request, {
        scopes: [{
          name: "Locals",
          variablesReference: FelidaeDebugAdapter.localVariablesReference,
          expensive: false
        }]
      });
      return;
    }

    if (request.command === "variables") {
      const args = (request.arguments ?? {}) as { variablesReference?: number };
      const variables = args.variablesReference === FelidaeDebugAdapter.localVariablesReference
        ? this.collectVisibleVariables()
        : [];
      this.sendResponse(request, { variables });
      return;
    }

    if (request.command === "evaluate") {
      this.evaluate(request);
      return;
    }

    if (request.command === "setBreakpoints") {
      const args = (request.arguments ?? {}) as { source?: { path?: string }, breakpoints?: Array<{ line: number }> };
      const sourcePath = args.source?.path;
      const requested = args.breakpoints ?? [];
      const sourceExecutableLines = sourcePath ? this.loadExecutableLines(sourcePath) : this.executableLines;
      if (sourcePath) {
        const verified = requested
          .map((breakpoint) => breakpoint.line)
          .filter((line) => sourceExecutableLines.includes(line));
        this.breakpointsByFile.set(this.normalizePath(sourcePath), new Set(verified));
      }
      const breakpoints = (args.breakpoints ?? []).map((breakpoint) => ({
        verified: sourceExecutableLines.includes(breakpoint.line),
        line: breakpoint.line,
        message: sourceExecutableLines.includes(breakpoint.line)
          ? "Felidae simulated breakpoint."
          : "No executable Felidae statement found on this line."
      }));
      this.sendResponse(request, { breakpoints });
      return;
    }

    if (request.command === "next" || request.command === "stepIn" || request.command === "stepOut") {
      this.stepSimulated(request.command);
      this.sendResponse(request);
      this.sendEvent("stopped", { reason: "step", threadId: 1, allThreadsStopped: true });
      return;
    }

    if (request.command === "pause") {
      this.stopped = true;
      this.sendResponse(request);
      this.sendEvent("stopped", { reason: "pause", threadId: 1, allThreadsStopped: true });
      return;
    }

    if (request.command === "continue") {
      this.sendResponse(request, { allThreadsContinued: true });
      this.sendEvent("continued", { threadId: 1, allThreadsContinued: true });
      if (this.continueToNextBreakpoint()) {
        this.sendEvent("stopped", { reason: "breakpoint", threadId: 1, allThreadsStopped: true });
        return;
      }
      this.stopped = false;
      this.process?.stdin.write("continue\n");
      return;
    }

    if (request.command === "disconnect" || request.command === "terminate") {
      this.process?.stdin.write("terminate\n");
      this.process?.kill();
      this.sendResponse(request);
      this.sendEvent("terminated");
      return;
    }

    this.sendResponse(request);
  }

  dispose(): void {
    this.process?.kill();
    this.emitter.dispose();
  }

  private launch(request: DapRequest): void {
    const args = (request.arguments ?? {}) as Record<string, unknown>;
    const interpreterPath = typeof args.interpreterPath === "string" ? args.interpreterPath : undefined;
    const program = typeof args.program === "string" ? args.program : undefined;
    const query = typeof args.query === "string" ? args.query : undefined;
    const stopOnEntry = args.stopOnEntry !== false;

    if (!interpreterPath || !program) {
      this.sendResponse(request, undefined, false, "Debug configuration requires interpreterPath and program.");
      this.sendEvent("terminated");
      return;
    }

    const launchArgs = [program];
    if (stopOnEntry) launchArgs.push("--stop-on-entry");
    if (query) launchArgs.push("--query", query);
    this.currentProgram = program;
    this.currentSource = program;
    this.currentInterpreterPath = interpreterPath;
    this.methodDefinitions = this.loadMethodDefinitions(program);
    this.executableLines = this.loadExecutableLines(program);
    this.currentLine = this.executableLines[0] ?? 1;
    this.callStack = [];
    this.stdoutBuffer = "";
    this.sendOutput(`Celidae launch\n${interpreterPath} ${launchArgs.join(" ")}\n`, "console");
    this.process = childProcess.spawn(interpreterPath, launchArgs, {
      cwd: path.dirname(program),
      windowsHide: true
    });

    this.process.stdout.on("data", (data: Buffer) => this.handleDebugStdout(data.toString()));
    this.process.stderr.on("data", (data: Buffer) => this.sendOutput(data.toString(), "stderr"));
    this.process.on("error", (error: Error) => {
      this.sendOutput(`${error.message}\n`, "stderr");
      this.sendEvent("terminated");
    });
    this.process.on("close", (code: number | null) => {
      this.flushDebugStdout();
      this.sendOutput(`Felidae process exited with code ${code ?? "unknown"}.\n`, "console");
      this.sendEvent("terminated");
    });

    this.sendResponse(request);
  }

  private currentInterpreterPath?: string;

  private evaluate(request: DapRequest): void {
    const args = (request.arguments ?? {}) as { expression?: string, context?: string };
    const expression = (args.expression ?? "").trim();
    if (!expression) {
      this.sendResponse(request, { result: "", variablesReference: 0 });
      return;
    }
    if (!this.currentProgram || !this.currentInterpreterPath) {
      this.sendResponse(request, undefined, false, "Start a Celidae debug session before running Debug Console queries.");
      return;
    }

    const query = expression.startsWith("?") ? expression : `? ${expression}`;
    const result = childProcess.spawnSync(this.currentInterpreterPath, [this.currentProgram, "--query", query], {
      cwd: path.dirname(this.currentProgram),
      encoding: "utf8",
      windowsHide: true
    });
    const output = `${result.stdout ?? ""}${result.stderr ?? ""}`.trim();
    if (result.error) {
      this.sendResponse(request, undefined, false, result.error.message);
      return;
    }
    this.sendResponse(request, {
      result: output || "(no result)",
      variablesReference: 0
    }, result.status === 0, result.status === 0 ? undefined : output);
  }

  private handleDebugStdout(text: string): void {
    this.stdoutBuffer += text;
    const lines = this.stdoutBuffer.split(/\r?\n/);
    this.stdoutBuffer = lines.pop() ?? "";
    for (const line of lines) {
      this.handleDebugLine(line);
    }
  }

  private flushDebugStdout(): void {
    if (!this.stdoutBuffer) return;
    this.handleDebugLine(this.stdoutBuffer);
    this.stdoutBuffer = "";
  }

  private handleDebugLine(line: string): void {
    if (!line) return;
    if (line.startsWith("FELIDAE_DEBUG_STOPPED")) {
      this.stopped = true;
      this.currentLine = this.executableLines[0] ?? 1;
      this.sendEvent("stopped", { reason: "entry", threadId: 1, allThreadsStopped: true });
      return;
    }
    if (line.startsWith("FELIDAE_DEBUG_CONTINUED")) {
      this.stopped = false;
      this.sendEvent("continued", { threadId: 1, allThreadsContinued: true });
      return;
    }
    if (line.startsWith("FELIDAE_DEBUG_EXIT")) {
      return;
    }
    if (line.startsWith("FELIDAE_DEBUG_READY")) {
      this.sendOutput(`${line}\n`, "console");
      return;
    }
    this.sendOutput(`${line}\n`, "stdout");
  }

  // Every stepIntoCall/stepOutOfCall/launch and every "variables" DAP request
  // (fired after each stop event) used to re-read and re-split the source
  // file from scratch. Cache split lines per file, invalidated by mtime, so
  // repeated steps within the same unchanged file are cheap.
  private getFileLines(filePath: string): string[] {
    const key = this.normalizePath(filePath);
    try {
      const mtimeMs = fs.statSync(filePath).mtimeMs;
      const cached = this.fileLinesCache.get(key);
      if (cached && cached.mtimeMs === mtimeMs) return cached.lines;
      const lines = fs.readFileSync(filePath, "utf8").split(/\r?\n/);
      this.fileLinesCache.set(key, { mtimeMs, lines });
      return lines;
    } catch {
      return [];
    }
  }

  private loadExecutableLines(program: string): number[] {
    const lines = this.getFileLines(program);
    if (!lines.length) return [1];
    const executable: number[] = [];
    for (let i = 0; i < lines.length; i++) {
      const trimmed = lines[i].trim();
      if (!trimmed || trimmed.startsWith("#")) continue;
      if (/^[)\]}.,]+$/.test(trimmed)) continue;
      executable.push(i + 1);
    }
    return executable.length ? executable : [1];
  }

  private loadMethodDefinitions(program: string): Map<string, { file: string, line: number }> {
    const definitions = new Map<string, { file: string, line: number }>();
    const visited = new Set<string>();
    const visit = (filePath: string): void => {
      const normalized = this.normalizePath(filePath);
      if (visited.has(normalized) || !fs.existsSync(filePath)) return;
      visited.add(normalized);
      const lines = this.getFileLines(filePath);
      const text = lines.join("\n");

      // Reuse the same declaration scanner the Document Symbol/Completion
      // providers already use, instead of a second, slightly different
      // per-line method-head regex.
      const declaration = new RegExp(DECLARATION_PATTERN);
      let match: RegExpExecArray | null;
      while ((match = declaration.exec(text)) !== null) {
        if (match[4] !== "=>") continue;
        const name = match[1].replace(/\./g, ":");
        if (!definitions.has(name)) {
          const line = text.slice(0, match.index).split("\n").length;
          definitions.set(name, { file: filePath, line });
        }
      }

      for (let i = 0; i < lines.length; i++) {
        const withoutComment = lines[i].split("#", 1)[0];
        const importMatch = /^\s*import\s+"([^"]+)"/.exec(withoutComment);
        if (importMatch) {
          for (const imported of this.resolveImportFiles(filePath, importMatch[1])) visit(imported);
        }
      }
    };
    visit(program);
    return definitions;
  }

  private resolveImportFiles(fromFile: string, importPath: string): string[] {
    const root = this.findWorkspaceRoot(fromFile);
    const candidates: string[] = [];
    if (!path.isAbsolute(importPath) && !importPath.includes("/") && !importPath.includes("\\") && !path.extname(importPath)) {
      candidates.push(path.join(root, "core", `${importPath}.fx`));
    }
    const direct = path.isAbsolute(importPath) ? importPath : path.resolve(path.dirname(fromFile), importPath);
    candidates.push(direct);
    if (!path.extname(direct)) candidates.push(`${direct}.fx`);
    return candidates.filter((candidate) => fs.existsSync(candidate) && fs.statSync(candidate).isFile());
  }

  private findWorkspaceRoot(fromFile: string): string {
    let current = path.dirname(fromFile);
    while (true) {
      if (fs.existsSync(path.join(current, "core"))) return current;
      const parent = path.dirname(current);
      if (parent === current) return path.dirname(fromFile);
      current = parent;
    }
  }

  private normalizePath(filePath: string): string {
    return path.resolve(filePath).toLowerCase();
  }

  private continueToNextBreakpoint(): boolean {
    const activeFile = this.currentSource ?? this.currentProgram;
    if (!activeFile) return false;
    const breakpoints = this.breakpointsByFile.get(this.normalizePath(activeFile));
    if (!breakpoints || breakpoints.size === 0) return false;

    // Recompute directly for activeFile (cheap: getFileLines is cached)
    // rather than trusting this.executableLines to already match it, so this
    // stays correct even if a future edit adds a path that moves
    // currentSource without also refreshing executableLines.
    const fileExecutableLines = this.loadExecutableLines(activeFile);
    const next = Array.from(breakpoints)
      .filter((line) => line > this.currentLine && fileExecutableLines.includes(line))
      .sort((left, right) => left - right)[0];
    if (!next) return false;

    this.currentLine = next;
    this.stopped = true;
    return true;
  }

  private stepSimulated(command: string): void {
    this.stopped = true;
    if (command === "stepIn" && this.stepIntoCall()) return;
    if (command === "stepOut" && this.stepOutOfCall()) return;
    this.stepToNextLine();
  }

  private stepToNextLine(): void {
    const foundIndex = this.executableLines.findIndex((line) => line >= this.currentLine);
    // findIndex returns -1 once currentLine is past every known executable
    // line; falling back to 0 would jump backward to the top of the file
    // instead of staying at the last line.
    const currentIndex = foundIndex === -1 ? this.executableLines.length - 1 : foundIndex;
    const nextIndex = Math.min(this.executableLines.length - 1, currentIndex + 1);
    this.currentLine = this.executableLines[nextIndex] ?? this.currentLine;
  }

  private stepIntoCall(): boolean {
    const activeFile = this.currentSource ?? this.currentProgram;
    if (!activeFile) return false;
    const line = this.readLine(activeFile, this.currentLine);
    const call = this.findMethodCallOnLine(line);
    if (!call) return false;
    const definition = this.methodDefinitions.get(call);
    if (!definition) return false;
    this.callStack.push({
      name: call,
      file: definition.file,
      line: definition.line,
      returnFile: activeFile,
      returnLine: this.nextExecutableLineAfter(activeFile, this.currentLine)
    });
    this.currentSource = definition.file;
    this.executableLines = this.loadExecutableLines(definition.file);
    this.currentLine = definition.line;
    return true;
  }

  private stepOutOfCall(): boolean {
    const frame = this.callStack.pop();
    if (!frame) {
      this.stepToNextLine();
      return true;
    }
    this.currentSource = frame.returnFile;
    this.executableLines = this.loadExecutableLines(frame.returnFile);
    this.currentLine = frame.returnLine;
    return true;
  }

  private readLine(filePath: string, line: number): string {
    return this.getFileLines(filePath)[line - 1] ?? "";
  }

  private nextExecutableLineAfter(filePath: string, line: number): number {
    const lines = this.loadExecutableLines(filePath);
    return lines.find((candidate) => candidate > line) ?? line;
  }

  private findMethodCallOnLine(line: string): string | undefined {
    const withoutComment = line.split("#", 1)[0];
    const calls = withoutComment.matchAll(/\b([A-Za-z_][A-Za-z0-9_]*(?:(?:[:.])[A-Za-z_][A-Za-z0-9_]*)*)\s*\(/g);
    const ignored = new Set(["return", "where", "lambda", "else", "then"]);
    for (const match of calls) {
      const name = match[1].replace(/\./g, ":");
      const base = name.split(":").pop() ?? name;
      if (ignored.has(base) || ignored.has(name)) continue;
      if (this.methodDefinitions.has(name)) return name;
    }
    return undefined;
  }

  private collectVisibleVariables(): Array<{ name: string, value: string, variablesReference: number }> {
    const activeFile = this.currentSource ?? this.currentProgram;
    if (!activeFile) {
      return [];
    }

    const lines = this.getFileLines(activeFile);
    if (!lines.length) return [];

    const endIndex = Math.min(lines.length, Math.max(1, this.currentLine));
    let scopeStart = 0;
    const variables = new Set<string>();

    for (let i = endIndex - 1; i >= 0; i--) {
      const head = /^\s*[A-Za-z_][A-Za-z0-9_]*\s*\(([^)]*)\)\s*=>/.exec(lines[i]);
      if (head) {
        scopeStart = i;
        this.collectHeadVariables(head[1], variables);
        break;
      }
    }

    for (let i = scopeStart; i < endIndex; i++) {
      this.collectLineVariables(lines[i], variables);
    }

    return Array.from(variables)
      .sort((left, right) => left.localeCompare(right))
      .map((name) => ({
        name,
        value: "<simulated>",
        variablesReference: 0
      }));
  }

  private collectHeadVariables(parameters: string, variables: Set<string>): void {
    const parts = parameters.split(",");
    for (const part of parts) {
      const match = /^\s*([A-Za-z_][A-Za-z0-9_]*)\s*:/.exec(part);
      if (match && match[1] !== "_") {
        variables.add(match[1]);
      }
    }
  }

  private collectLineVariables(line: string, variables: Set<string>): void {
    const withoutComment = line.split("#", 1)[0];
    const assignment = /\b([A-Za-z_][A-Za-z0-9_]*)\s*:=/g;
    let assignmentMatch: RegExpExecArray | null;
    while ((assignmentMatch = assignment.exec(withoutComment)) !== null) {
      if (assignmentMatch[1] !== "_") {
        variables.add(assignmentMatch[1]);
      }
    }

    const lambda = /\blambda\s*\([^,]+,\s*([a-z_][A-Za-z0-9_]*)\s*=>/g;
    let lambdaMatch: RegExpExecArray | null;
    while ((lambdaMatch = lambda.exec(withoutComment)) !== null) {
      if (lambdaMatch[1] !== "_") {
        variables.add(lambdaMatch[1]);
      }
    }
  }

  private sendResponse(request: DapRequest, body?: unknown, success = true, message?: string): void {
    this.emitter.fire({
      type: "response",
      seq: 0,
      request_seq: request.seq ?? 0,
      command: request.command,
      success,
      message,
      body
    } as vscode.DebugProtocolMessage);
  }

  private sendEvent(event: string, body?: unknown): void {
    this.emitter.fire({ type: "event", seq: 0, event, body } as vscode.DebugProtocolMessage);
  }

  private sendOutput(output: string, category: "console" | "stdout" | "stderr"): void {
    this.sendEvent("output", { category, output });
  }
}

class FelidaeDebugAdapterFactory implements vscode.DebugAdapterDescriptorFactory {
  createDebugAdapterDescriptor(): vscode.ProviderResult<vscode.DebugAdapterDescriptor> {
    return new vscode.DebugAdapterInlineImplementation(new FelidaeDebugAdapter());
  }
}

class FelidaeDebugConfigurationProvider implements vscode.DebugConfigurationProvider {
  resolveDebugConfiguration(folder: vscode.WorkspaceFolder | undefined, config: vscode.DebugConfiguration): vscode.ProviderResult<vscode.DebugConfiguration> {
    const editor = vscode.window.activeTextEditor;
    const activeDocument = editor?.document.languageId === "felidae" ? editor.document : undefined;
    const workspacePath = folder?.uri.fsPath;

    config.type ??= "felidae";
    config.name ??= "Debug Felidae Query";
    config.request ??= "launch";
    config.program ??= activeDocument?.uri.fsPath ?? "${file}";
    config.interpreterPath ??= workspacePath ? path.join(workspacePath, "build", "felidae_debug.exe") : resolveDebugInterpreterPath(activeDocument?.uri ?? vscode.Uri.file(""));
    config.stopOnEntry ??= true;
    return config;
  }
}

export function activate(context: vscode.ExtensionContext): void {
  const diagnostics = vscode.languages.createDiagnosticCollection("felidae");
  const debounceTimers = new Map<string, ReturnType<typeof setTimeout>>();
  const DIAGNOSTICS_DEBOUNCE_MS = 350;

  const refreshDiagnostics = (document: vscode.TextDocument): void => {
    if (document.languageId !== "felidae") return;
    const version = document.version;
    diagnostics.set(document.uri, []);
    void runtimeCheckDiagnostics(document).then((runtimeDiagnostics) => {
      if (document.isClosed || document.version !== version) return;
      diagnostics.set(document.uri, runtimeDiagnostics);
    });
    refreshSymbolCache(document);
  };

  const scheduleDiagnosticsRefresh = (document: vscode.TextDocument): void => {
    if (document.languageId !== "felidae") return;
    const key = document.uri.toString();
    const existing = debounceTimers.get(key);
    if (existing) clearTimeout(existing);
    debounceTimers.set(
      key,
      setTimeout(() => {
        debounceTimers.delete(key);
        refreshDiagnostics(document);
      }, DIAGNOSTICS_DEBOUNCE_MS)
    );
  };

  const refreshMainContext = (): void => {
    const document = vscode.window.activeTextEditor?.document;
    const enabled = !!document && document.languageId === "felidae" && hasMainMethod(document);
    void vscode.commands.executeCommand("setContext", "felidaeHasMain", enabled);
  };

  for (const document of vscode.workspace.textDocuments) {
    refreshDiagnostics(document);
  }
  refreshMainContext();

  context.subscriptions.push(
    diagnostics,
    vscode.commands.registerCommand("felidae.runMain", runMain),
    vscode.commands.registerCommand("felidae.debugMain", debugMain),
    vscode.commands.registerCommand("felidae.runQuery", runQuery),
    vscode.commands.registerCommand("felidae.visualize", (uri?: vscode.Uri) => visualizeFelidae(context, uri)),
    vscode.workspace.onDidOpenTextDocument((document) => {
      refreshDiagnostics(document);
      refreshMainContext();
    }),
    vscode.workspace.onDidChangeTextDocument((event) => {
      scheduleDiagnosticsRefresh(event.document);
      refreshMainContext();
    }),
    vscode.workspace.onDidSaveTextDocument((document) => {
      refreshDiagnostics(document);
      refreshMainContext();
    }),
    vscode.workspace.onDidCloseTextDocument((document) => {
      diagnostics.delete(document.uri);
      symbolSummaryCache.delete(document.uri.toString());
      const key = document.uri.toString();
      const timer = debounceTimers.get(key);
      if (timer) {
        clearTimeout(timer);
        debounceTimers.delete(key);
      }
    }),
    vscode.window.onDidChangeActiveTextEditor((editor) => {
      if (editor) refreshDiagnostics(editor.document);
      refreshMainContext();
    }),
    vscode.languages.registerDocumentLinkProvider({ language: "felidae" }, new FelidaeDocumentLinkProvider()),
    vscode.languages.registerHoverProvider({ language: "felidae" }, new FelidaeHoverProvider()),
    vscode.languages.registerDefinitionProvider({ language: "felidae" }, new FelidaeDefinitionProvider()),
    vscode.languages.registerFoldingRangeProvider({ language: "felidae" }, new FelidaeFoldingRangeProvider()),
    vscode.languages.registerCodeLensProvider({ language: "felidae" }, new FelidaeCodeLensProvider()),
    vscode.languages.registerDocumentSemanticTokensProvider({ language: "felidae" }, new FelidaeSemanticTokensProvider(), semanticLegend),
    vscode.languages.registerDocumentSymbolProvider({ language: "felidae" }, new FelidaeDocumentSymbolProvider()),
    vscode.languages.registerCompletionItemProvider(
      { language: "felidae" },
      new FelidaeCompletionItemProvider(),
      ".", "(", ","
    ),
    vscode.languages.registerCodeActionsProvider(
      { language: "felidae" },
      new FelidaeCodeActionProvider(),
      { providedCodeActionKinds: FelidaeCodeActionProvider.providedCodeActionKinds }
    ),
    vscode.debug.registerDebugConfigurationProvider("felidae", new FelidaeDebugConfigurationProvider()),
    vscode.debug.registerDebugAdapterDescriptorFactory("felidae", new FelidaeDebugAdapterFactory())
  );
}

export function deactivate(): void {}

