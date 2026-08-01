"use strict";
var __createBinding = (this && this.__createBinding) || (Object.create ? (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    var desc = Object.getOwnPropertyDescriptor(m, k);
    if (!desc || ("get" in desc ? !m.__esModule : desc.writable || desc.configurable)) {
      desc = { enumerable: true, get: function() { return m[k]; } };
    }
    Object.defineProperty(o, k2, desc);
}) : (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    o[k2] = m[k];
}));
var __setModuleDefault = (this && this.__setModuleDefault) || (Object.create ? (function(o, v) {
    Object.defineProperty(o, "default", { enumerable: true, value: v });
}) : function(o, v) {
    o["default"] = v;
});
var __importStar = (this && this.__importStar) || (function () {
    var ownKeys = function(o) {
        ownKeys = Object.getOwnPropertyNames || function (o) {
            var ar = [];
            for (var k in o) if (Object.prototype.hasOwnProperty.call(o, k)) ar[ar.length] = k;
            return ar;
        };
        return ownKeys(o);
    };
    return function (mod) {
        if (mod && mod.__esModule) return mod;
        var result = {};
        if (mod != null) for (var k = ownKeys(mod), i = 0; i < k.length; i++) if (k[i] !== "default") __createBinding(result, mod, k[i]);
        __setModuleDefault(result, mod);
        return result;
    };
})();
Object.defineProperty(exports, "__esModule", { value: true });
exports.FelidaeDocumentFormattingEditProvider = void 0;
exports.formatFelidaeLines = formatFelidaeLines;
exports.formatFelidaeSource = formatFelidaeSource;
const vscode = __importStar(require("vscode"));
// Felidae has no PSI/AST available to editor tooling (see extension.ts's
// text-scanning providers), so this formatter is a structural, line-based
// beautifier rather than a token-reflowing pretty-printer: it recomputes
// leading indentation and otherwise leaves line content untouched. That
// keeps it safe to run on any file (it can never corrupt a string literal
// or reorder tokens) while still fixing the most common real complaints -
// inconsistent indentation, stray trailing whitespace, and extra blank
// lines.
//
// Felidae's real control flow is indentation-structured in practice - not
// just a clause body after a top-level `=>`, but also nested `if <cond>
// then ... else ...` (see examples/exceptions.fx), where `else` sits back
// at its own `if`'s column, not always column 0. That is genuinely
// ambiguous to re-derive from keywords and brackets alone (a body can
// contain a blank line purely as visual grouping, and `else` can belong to
// an outer clause or an inner `if`). Rather than guess, this formatter
// borrows Python's approach: track a stack of open "block" levels keyed by
// the *original* indentation width of the line that opened them (`=>` or
// `then` with nothing after), and dedent by popping levels whose owning
// line is at or past the new line's width - `else` is the one exception,
// pairing with (not popping) the level whose width it exactly matches.
// Bracket/brace/paren continuations (e.g. a multi-line `return (...)`
// argument list) are tracked the same way but keyed off bracket nesting
// instead of keywords, with multiple brackets opened on one line counting
// as a single extra level (matching the convention already used by hand
// throughout examples/*.fx, e.g. `foo(value: {` only indents its contents
// once, not twice).
const INDENT_UNIT = "    ";
const OPENERS = "([{";
const CLOSERS = ")]}";
const TAB_WIDTH = 4;
// Blanks out string-literal contents and "#" line comments (preserving
// string length) so bracket/keyword scanning never misreads a `(` or `=>`
// that only appears inside a string or a comment.
function maskLine(line) {
    let masked = "";
    let inString = false;
    let i = 0;
    while (i < line.length) {
        const ch = line[i];
        if (inString) {
            if (ch === "\\" && i + 1 < line.length) {
                masked += "xx";
                i += 2;
                continue;
            }
            if (ch === '"') {
                inString = false;
                masked += '"';
                i++;
                continue;
            }
            masked += "x";
            i++;
            continue;
        }
        if (ch === "#") {
            masked += " ".repeat(line.length - i);
            break;
        }
        if (ch === '"') {
            inString = true;
            masked += '"';
            i++;
            continue;
        }
        masked += ch;
        i++;
    }
    return masked;
}
function leadingWidth(rawLine) {
    let width = 0;
    for (const ch of rawLine) {
        if (ch === " ")
            width++;
        else if (ch === "\t")
            width += TAB_WIDTH;
        else
            break;
    }
    return width;
}
// A depth-0 `=>` or word-boundary `then` that is the last token on the
// line (masked, so never one found inside a string/comment) means "this
// block's body continues on later lines." One with content after it on
// the same line (`Foo() => return`, `if x then return 1`) is a complete
// inline block - nothing to open.
function opensBlock(masked) {
    let localDepth = 0;
    let tailKeywordEnd = -1;
    for (let i = 0; i < masked.length; i++) {
        const ch = masked[i];
        if (OPENERS.includes(ch)) {
            localDepth++;
        }
        else if (CLOSERS.includes(ch)) {
            localDepth--;
        }
        else if (localDepth === 0) {
            if (ch === "=" && masked[i + 1] === ">")
                tailKeywordEnd = i + 2;
            else if (masked.startsWith("then", i) &&
                (i === 0 || /\s/.test(masked[i - 1])) &&
                !/[A-Za-z0-9_]/.test(masked[i + 4] ?? "")) {
                tailKeywordEnd = i + 4;
            }
        }
    }
    return tailKeywordEnd >= 0 && masked.slice(tailKeywordEnd).trim().length === 0;
}
// Formats an array of EOL-free lines. Kept separate from any VS Code /
// document EOL handling so the same core logic can be ported verbatim to
// other editors (IntelliJ, Emacs, Vim) without dragging along VS Code types.
function formatFelidaeLines(rawLines) {
    const frames = [];
    // Stack of raw-bracket-depth thresholds: one entry per *origin line* that
    // net-opened brackets, regardless of how many brackets that one line
    // opened (matching the convention used throughout examples/*.fx, e.g.
    // `foo(value: {` only indents its contents once, not twice). Each
    // threshold is set to (that line's own final raw depth - 1), so the
    // level pops as soon as *any single* closer - wherever it lands, leading
    // or split later on a closing line like `], factType: "x")` - brings raw
    // depth back down to it, without needing every one of that line's own
    // raw brackets to be individually closed first.
    const bracketLevels = [];
    let rawBracketDepth = 0;
    const output = [];
    let previousRawLineWasBlank = true; // start-of-file counts as a boundary
    for (const rawLine of rawLines) {
        const trimmed = rawLine.trim();
        if (trimmed.length === 0) {
            output.push("");
            previousRawLineWasBlank = true;
            continue;
        }
        const masked = maskLine(rawLine);
        const isComment = trimmed.startsWith("#");
        const isBareElse = trimmed === "else";
        // A comment's own column is only trustworthy as a dedent signal when it
        // opens a new paragraph (preceded by a blank line, or file start) - a
        // leading doc-comment for the next top-level declaration. A comment
        // glued directly under body code with no blank line (a commented-out
        // statement, a trailing note) keeps whatever depth is already active,
        // the same as any other non-structural annotation.
        const commentTrustsOwnColumn = isComment && previousRawLineWasBlank;
        previousRawLineWasBlank = false;
        if (rawBracketDepth === 0 && (!isComment || commentTrustsOwnColumn)) {
            // Only a genuine structural line (not inside a bracket continuation)
            // participates in block dedent/pairing.
            const width = leadingWidth(rawLine);
            if (isBareElse) {
                while (frames.length > 0 && width < frames[frames.length - 1].headWidth)
                    frames.pop();
                // If the top frame's head is at exactly this width, `else` pairs
                // with it (stays open, body resumes). Otherwise (malformed input)
                // fall through and just print at the best-effort current depth.
            }
            else {
                while (frames.length > 0 && width <= frames[frames.length - 1].headWidth)
                    frames.pop();
            }
        }
        let depthUnits;
        if (isBareElse && frames.length > 0) {
            depthUnits = frames.length - 1 + bracketLevels.length;
        }
        else {
            // Leading closers dedent this line immediately.
            let idx = 0;
            while (idx < masked.length && (masked[idx] === " " || masked[idx] === "\t"))
                idx++;
            while (idx < masked.length && CLOSERS.includes(masked[idx])) {
                rawBracketDepth--;
                while (bracketLevels.length > 0 && rawBracketDepth <= bracketLevels[bracketLevels.length - 1]) {
                    bracketLevels.pop();
                }
                idx++;
            }
            depthUnits = frames.length + bracketLevels.length;
            // Process the remainder of the line for bracket bookkeeping. Closers
            // found here (not just in the leading run above - e.g. the trailing
            // `)` in `], factType: "x")`) must keep popping bracketLevels too, or
            // a level leaks open forever and every following line - including
            // the next top-level declaration - drifts one level deep, and since
            // the drifted output is re-measured on the next format pass,
            // formatting stops being idempotent. A closer that instead matches
            // something opened earlier in this same remainder scan (e.g. the
            // `)` closing `Place(` in `place: Place(name: "x"),`) never brings
            // rawBracketDepth down to an *outer* level's threshold, so it
            // correctly leaves that outer level untouched.
            const depthAfterLeadingClosers = rawBracketDepth;
            for (let i = idx; i < masked.length; i++) {
                const ch = masked[i];
                if (OPENERS.includes(ch)) {
                    rawBracketDepth++;
                }
                else if (CLOSERS.includes(ch)) {
                    rawBracketDepth--;
                    while (bracketLevels.length > 0 && rawBracketDepth <= bracketLevels[bracketLevels.length - 1]) {
                        bracketLevels.pop();
                    }
                }
            }
            if (rawBracketDepth > depthAfterLeadingClosers) {
                // Threshold = this line's own final depth - 1, so a single closer
                // (wherever it lands) is enough to pop this level, regardless of
                // how many raw brackets this one line net-opened.
                bracketLevels.push(rawBracketDepth - 1);
            }
        }
        if (depthUnits < 0)
            depthUnits = 0;
        output.push(INDENT_UNIT.repeat(depthUnits) + trimmed);
        if (rawBracketDepth === 0 && !isComment && opensBlock(masked)) {
            frames.push({ headWidth: leadingWidth(rawLine) });
        }
    }
    const collapsed = [];
    for (const line of output) {
        if (line === "" && collapsed.length > 0 && collapsed[collapsed.length - 1] === "")
            continue;
        collapsed.push(line);
    }
    while (collapsed.length > 0 && collapsed[collapsed.length - 1] === "")
        collapsed.pop();
    return collapsed;
}
function formatFelidaeSource(text) {
    const lines = text.replace(/\r\n/g, "\n").replace(/\r/g, "\n").split("\n");
    return formatFelidaeLines(lines).join("\n") + "\n";
}
class FelidaeDocumentFormattingEditProvider {
    provideDocumentFormattingEdits(document) {
        const eol = document.eol === vscode.EndOfLine.CRLF ? "\r\n" : "\n";
        const originalLines = [];
        for (let i = 0; i < document.lineCount; i++)
            originalLines.push(document.lineAt(i).text);
        const formattedLines = formatFelidaeLines(originalLines);
        const newText = formattedLines.join(eol) + eol;
        const fullRange = new vscode.Range(document.positionAt(0), document.positionAt(document.getText().length));
        if (document.getText() === newText)
            return [];
        return [vscode.TextEdit.replace(fullRange, newText)];
    }
}
exports.FelidaeDocumentFormattingEditProvider = FelidaeDocumentFormattingEditProvider;
//# sourceMappingURL=formatter.js.map