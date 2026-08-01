; Bracket-nested continuations (call/group-goal arguments, map and array
; literals) indent their contents and outdent on the closing bracket. This
; deliberately covers only the unambiguous, bracket-delimited part of
; Felidae's indentation convention - see
; vs-code-extension/src/formatter.ts's header comment for why clause/
; `if...then...else` block indentation (no bracket involved) needs the
; fuller heuristic in :FelidaeFormat-equivalent commands rather than a
; static query.

(call
  "(" @open
  ")" @close) @indent

(group_goal
  "(" @open
  ")" @close) @indent

(map
  "{" @open
  "}" @close) @indent

(array
  "[" @open
  "]" @close) @indent
