package local.felidae.intellij.formatting;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.List;

/**
 * Structural, line-based beautifier for Felidae source. Felidae has no
 * PSI/AST available to editor tooling here (every .fx file is a
 * PsiPlainTextFile - see FelidaeExternalAnnotator/FelidaeCompletionContributor,
 * which are all text-scanning based), so this recomputes leading indentation
 * from bracket nesting and clause/if-then/else block structure and otherwise
 * leaves line content untouched. That keeps it safe to run on any file (it
 * can never corrupt a string literal or reorder tokens) while still fixing
 * the most common real complaints: inconsistent indentation, stray trailing
 * whitespace, and extra blank lines.
 *
 * <p>This is a line-for-line port of vs-code-extension/src/formatter.ts's
 * {@code formatFelidaeLines} - keep the two in sync. See that file's header
 * comment for the full design rationale (block-frame stack keyed by original
 * indentation width, Python-style dedent, bracket levels collapsed one per
 * origin line). In short: Felidae's real control flow is indentation
 * structured in practice, not just a clause body after a top-level {@code =>},
 * but also nested {@code if <cond> then ... else ...}, where {@code else}
 * sits back at its own {@code if}'s column rather than always column 0. That
 * is genuinely ambiguous to re-derive from keywords and brackets alone (a
 * body can contain a blank line purely as visual grouping, and {@code else}
 * can belong to an outer clause or an inner {@code if}), so this borrows
 * Python's approach instead of guessing from brackets alone.
 */
public final class FelidaeFormatter {

    private static final String INDENT_UNIT = "    ";
    private static final String OPENERS = "([{";
    private static final String CLOSERS = ")]}";
    private static final int TAB_WIDTH = 4;

    private FelidaeFormatter() {
    }

    private record BlockFrame(int headWidth) {
    }

    /** Blanks out string-literal contents and "#" line comments (preserving
     * length) so bracket/keyword scanning never misreads a `(` or `=>` that
     * only appears inside a string or a comment. */
    private static String maskLine(String line) {
        StringBuilder masked = new StringBuilder(line.length());
        boolean inString = false;
        int i = 0;
        int length = line.length();
        while (i < length) {
            char ch = line.charAt(i);
            if (inString) {
                if (ch == '\\' && i + 1 < length) {
                    masked.append("xx");
                    i += 2;
                    continue;
                }
                if (ch == '"') {
                    inString = false;
                    masked.append('"');
                    i++;
                    continue;
                }
                masked.append('x');
                i++;
                continue;
            }
            if (ch == '#') {
                masked.append(" ".repeat(length - i));
                break;
            }
            if (ch == '"') {
                inString = true;
                masked.append('"');
                i++;
                continue;
            }
            masked.append(ch);
            i++;
        }
        return masked.toString();
    }

    private static int leadingWidth(String rawLine) {
        int width = 0;
        for (int i = 0; i < rawLine.length(); i++) {
            char ch = rawLine.charAt(i);
            if (ch == ' ') {
                width++;
            } else if (ch == '\t') {
                width += TAB_WIDTH;
            } else {
                break;
            }
        }
        return width;
    }

    /** A depth-0 {@code =>} or word-boundary {@code then} that is the last
     * token on the (masked) line means "this block's body continues on
     * later lines." One with content after it on the same line
     * ({@code Foo() => return}, {@code if x then return 1}) is a complete
     * inline block - nothing to open. */
    private static boolean opensBlock(String masked) {
        int localDepth = 0;
        int tailKeywordEnd = -1;
        int length = masked.length();
        for (int i = 0; i < length; i++) {
            char ch = masked.charAt(i);
            if (OPENERS.indexOf(ch) >= 0) {
                localDepth++;
            } else if (CLOSERS.indexOf(ch) >= 0) {
                localDepth--;
            } else if (localDepth == 0) {
                if (ch == '=' && i + 1 < length && masked.charAt(i + 1) == '>') {
                    tailKeywordEnd = i + 2;
                } else if (masked.startsWith("then", i)
                        && (i == 0 || Character.isWhitespace(masked.charAt(i - 1)))
                        && (i + 4 >= length || !isIdentifierChar(masked.charAt(i + 4)))) {
                    tailKeywordEnd = i + 4;
                }
            }
        }
        return tailKeywordEnd >= 0 && masked.substring(tailKeywordEnd).isBlank();
    }

    private static boolean isIdentifierChar(char ch) {
        return Character.isLetterOrDigit(ch) || ch == '_';
    }

    /** Formats a list of EOL-free lines. */
    public static List<String> formatLines(List<String> rawLines) {
        Deque<BlockFrame> frames = new ArrayDeque<>();
        // Stack of raw-bracket-depth thresholds: one entry per *origin line*
        // that net-opened brackets, regardless of how many brackets that one
        // line opened (matching the convention used throughout examples/*.fx,
        // e.g. `foo(value: {` only indents its contents once, not twice).
        // Each threshold is set to (that line's own final raw depth - 1), so
        // the level pops as soon as *any single* closer - wherever it lands,
        // leading or split later on a closing line like `], factType: "x")` -
        // brings raw depth back down to it, without needing every one of
        // that line's own raw brackets to be individually closed first.
        Deque<Integer> bracketLevels = new ArrayDeque<>();
        int[] rawBracketDepthBox = {0};
        List<String> output = new ArrayList<>();
        boolean previousRawLineWasBlank = true; // start-of-file counts as a boundary

        for (String rawLine : rawLines) {
            String trimmed = rawLine.strip();

            if (trimmed.isEmpty()) {
                output.add("");
                previousRawLineWasBlank = true;
                continue;
            }

            String masked = maskLine(rawLine);
            boolean isComment = trimmed.startsWith("#");
            boolean isBareElse = trimmed.equals("else");
            // A comment's own column is only trustworthy as a dedent signal
            // when it opens a new paragraph (preceded by a blank line, or
            // file start) - a leading doc-comment for the next top-level
            // declaration. A comment glued directly under body code with no
            // blank line (a commented-out statement, a trailing note) keeps
            // whatever depth is already active, like any other annotation.
            boolean commentTrustsOwnColumn = isComment && previousRawLineWasBlank;
            previousRawLineWasBlank = false;

            if (rawBracketDepthBox[0] == 0 && (!isComment || commentTrustsOwnColumn)) {
                int width = leadingWidth(rawLine);
                if (isBareElse) {
                    while (!frames.isEmpty() && width < frames.peek().headWidth()) frames.pop();
                    // If the top frame's head is at exactly this width, `else`
                    // pairs with it (stays open, body resumes). Otherwise
                    // (malformed input) fall through and print best-effort.
                } else {
                    while (!frames.isEmpty() && width <= frames.peek().headWidth()) frames.pop();
                }
            }

            int depthUnits;
            if (isBareElse && !frames.isEmpty()) {
                depthUnits = frames.size() - 1 + bracketLevels.size();
            } else {
                // Leading closers dedent this line immediately.
                int idx = 0;
                while (idx < masked.length() && (masked.charAt(idx) == ' ' || masked.charAt(idx) == '\t')) idx++;
                while (idx < masked.length() && CLOSERS.indexOf(masked.charAt(idx)) >= 0) {
                    rawBracketDepthBox[0]--;
                    while (!bracketLevels.isEmpty() && rawBracketDepthBox[0] <= bracketLevels.peek()) {
                        bracketLevels.pop();
                    }
                    idx++;
                }
                depthUnits = frames.size() + bracketLevels.size();

                // Process the remainder of the line for bracket bookkeeping.
                // Closers found here (not just in the leading run above -
                // e.g. the trailing `)` in `], factType: "x")`) must keep
                // popping bracketLevels too, or a level leaks open forever
                // and every following line - including the next top-level
                // declaration - drifts one level deep, and since the
                // drifted output is re-measured on the next format pass,
                // formatting stops being idempotent. A closer that instead
                // matches something opened earlier in this same remainder
                // scan (e.g. the `)` closing `Place(` in
                // `place: Place(name: "x"),`) never brings rawBracketDepth
                // down to an *outer* level's threshold, so it correctly
                // leaves that outer level untouched.
                int depthAfterLeadingClosers = rawBracketDepthBox[0];
                for (int i = idx; i < masked.length(); i++) {
                    char ch = masked.charAt(i);
                    if (OPENERS.indexOf(ch) >= 0) {
                        rawBracketDepthBox[0]++;
                    } else if (CLOSERS.indexOf(ch) >= 0) {
                        rawBracketDepthBox[0]--;
                        while (!bracketLevels.isEmpty() && rawBracketDepthBox[0] <= bracketLevels.peek()) {
                            bracketLevels.pop();
                        }
                    }
                }
                if (rawBracketDepthBox[0] > depthAfterLeadingClosers) {
                    // Threshold = this line's own final depth - 1, so a
                    // single closer (wherever it lands) is enough to pop
                    // this level, regardless of how many raw brackets this
                    // one line net-opened.
                    bracketLevels.push(rawBracketDepthBox[0] - 1);
                }
            }

            if (depthUnits < 0) depthUnits = 0;
            output.add(INDENT_UNIT.repeat(depthUnits) + trimmed);

            if (rawBracketDepthBox[0] == 0 && !isComment && opensBlock(masked)) {
                frames.push(new BlockFrame(leadingWidth(rawLine)));
            }
        }

        List<String> collapsed = new ArrayList<>();
        for (String line : output) {
            if (line.isEmpty() && !collapsed.isEmpty() && collapsed.get(collapsed.size() - 1).isEmpty()) continue;
            collapsed.add(line);
        }
        while (!collapsed.isEmpty() && collapsed.get(collapsed.size() - 1).isEmpty()) {
            collapsed.remove(collapsed.size() - 1);
        }

        return collapsed;
    }

    /** Formats a full source string (any line-ending style), returning LF-joined output with a single trailing newline. */
    public static String formatSource(String text) {
        String normalized = text.replace("\r\n", "\n").replace("\r", "\n");
        List<String> lines = List.of(normalized.split("\n", -1));
        return String.join("\n", formatLines(lines)) + "\n";
    }
}
