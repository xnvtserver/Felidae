"""Structural, line-based beautifier for Felidae (.fx) source, plus the
Sublime Text command that applies it to a view.

Felidae has no Sublime-native parser, so - like this repository's VS Code
and IntelliJ extensions, both text-scanning rather than AST/PSI based -
this recomputes leading indentation from bracket nesting and clause/
if-then-else block structure and otherwise leaves line content untouched.

format_lines() below is a line-for-line port of
vs-code-extension/src/formatter.ts's formatFelidaeLines (also ported to
Java for IntelliJ, Emacs Lisp for Emacs, and Vimscript for Vim/Neovim) -
keep all five in sync. See that file's header comment for the full design
rationale: a stack of open "block" levels keyed by the *original*
indentation width of the line that opened them (a clause's own `=>`, or a
nested `if <cond> then`), popped Python-dedent-style (fittingly), with
`else` pairing at (not popping) the level whose width it exactly matches;
bracket/brace/paren continuations tracked the same way but keyed off
bracket nesting, with multiple brackets opened on one line counting as a
single extra level.
"""

import sublime
import sublime_plugin

OPENERS = "([{"
CLOSERS = ")]}"
INDENT_UNIT = "    "
TAB_WIDTH = 4


def mask_line(line):
    """Blank out string-literal contents and '#' comments (preserving
    length) so bracket/keyword scanning never misreads a '(' or '=>' that
    only appears inside a string or a comment."""
    out = []
    in_string = False
    i = 0
    length = len(line)
    while i < length:
        ch = line[i]
        if in_string:
            if ch == "\\" and i + 1 < length:
                out.append("x")
                out.append("x")
                i += 2
                continue
            if ch == '"':
                in_string = False
                out.append('"')
                i += 1
                continue
            out.append("x")
            i += 1
            continue
        if ch == "#":
            out.append(" " * (length - i))
            break
        if ch == '"':
            in_string = True
            out.append('"')
            i += 1
            continue
        out.append(ch)
        i += 1
    return "".join(out)


def leading_width(line):
    width = 0
    for ch in line:
        if ch == " ":
            width += 1
        elif ch == "\t":
            width += TAB_WIDTH
        else:
            break
    return width


def opens_block(masked):
    """True if MASKED ends with a depth-0 '=>' or word-boundary 'then' -
    meaning this block's body continues on later lines. One with content
    after the arrow/keyword on the same line (`Foo() => return`,
    `if x then return 1`) is a complete inline block - nothing to open."""
    local_depth = 0
    tail_end = -1
    length = len(masked)
    i = 0
    while i < length:
        ch = masked[i]
        if ch in OPENERS:
            local_depth += 1
        elif ch in CLOSERS:
            local_depth -= 1
        elif local_depth == 0:
            if ch == "=" and i + 1 < length and masked[i + 1] == ">":
                tail_end = i + 2
            elif (
                masked.startswith("then", i)
                and (i == 0 or masked[i - 1].isspace())
                and (i + 4 >= length or not (masked[i + 4].isalnum() or masked[i + 4] == "_"))
            ):
                tail_end = i + 4
        i += 1
    return tail_end >= 0 and masked[tail_end:].strip() == ""


def format_lines(raw_lines):
    """Reformat a list of EOL-free lines, returning a new list."""
    frame_widths = []    # stack of open block head-widths
    bracket_levels = []  # stack of raw-bracket-depth pop thresholds
    raw_depth = 0
    output = []
    previous_blank = True

    for raw_line in raw_lines:
        trimmed = raw_line.strip()

        if trimmed == "":
            output.append("")
            previous_blank = True
            continue

        masked = mask_line(raw_line)
        is_comment = trimmed.startswith("#")
        is_bare_else = trimmed == "else"
        # A comment's own column is only trustworthy as a dedent signal
        # when it opens a new paragraph (preceded by a blank line, or file
        # start) - a leading doc-comment for the next top-level
        # declaration. A comment glued directly under body code with no
        # blank line keeps whatever depth is already active.
        comment_trusts_column = is_comment and previous_blank
        previous_blank = False

        if raw_depth == 0 and (not is_comment or comment_trusts_column):
            width = leading_width(raw_line)
            if is_bare_else:
                while frame_widths and width < frame_widths[-1]:
                    frame_widths.pop()
            else:
                while frame_widths and width <= frame_widths[-1]:
                    frame_widths.pop()

        if is_bare_else and frame_widths:
            depth_units = len(frame_widths) - 1 + len(bracket_levels)
        else:
            idx = 0
            length = len(masked)
            while idx < length and masked[idx] in " \t":
                idx += 1
            while idx < length and masked[idx] in CLOSERS:
                raw_depth -= 1
                while bracket_levels and raw_depth <= bracket_levels[-1]:
                    bracket_levels.pop()
                idx += 1
            depth_units = len(frame_widths) + len(bracket_levels)

            depth_after_leading = raw_depth
            while idx < length:
                ch = masked[idx]
                if ch in OPENERS:
                    raw_depth += 1
                elif ch in CLOSERS:
                    raw_depth -= 1
                    while bracket_levels and raw_depth <= bracket_levels[-1]:
                        bracket_levels.pop()
                idx += 1
            if raw_depth > depth_after_leading:
                # Threshold = this line's own final depth - 1, so a single
                # closer (wherever it lands) is enough to pop this level,
                # regardless of how many raw brackets this line net-opened.
                bracket_levels.append(raw_depth - 1)

        depth_units = max(0, depth_units)
        output.append((INDENT_UNIT * depth_units) + trimmed)

        if raw_depth == 0 and not is_comment and opens_block(masked):
            frame_widths.append(leading_width(raw_line))

    collapsed = []
    for line in output:
        if line == "" and collapsed and collapsed[-1] == "":
            continue
        collapsed.append(line)
    while collapsed and collapsed[-1] == "":
        collapsed.pop()

    return collapsed


def format_source(text):
    normalized = text.replace("\r\n", "\n").replace("\r", "\n")
    return "\n".join(format_lines(normalized.split("\n"))) + "\n"


class FelidaeFormatCommand(sublime_plugin.TextCommand):
    """Reformats the whole view: indentation, blank lines, trailing
    whitespace. Bound to the Felidae: Format Document command palette
    entry - see Default.sublime-commands."""

    def run(self, edit):
        view = self.view
        whole = sublime.Region(0, view.size())
        original = view.substr(whole)
        formatted = format_source(original)
        if formatted == original:
            return
        view.replace(edit, whole, formatted)

    def is_enabled(self):
        return self.view.match_selector(0, "source.felidae")
