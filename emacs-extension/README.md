# felidae-mode

Emacs major mode for editing Felidae (`.fx`) source files.

Felidae has no Emacs-native parser, so — like this repository's VS Code and
IntelliJ extensions, both of which are text-scanning rather than AST/PSI
based — this mode's fontification and formatting work directly on buffer
text.

## Features

- Syntax highlighting (`font-lock`) for keywords, string/comment syntax
  (`#` line comments), declarations, `:=` bindings, standard-library module
  names, type annotations, and constants (`nil`/`true`/`false`).
- `felidae-format-buffer` (`C-c C-f`) — a structural beautifier that
  recomputes indentation, collapses extra blank lines, and trims trailing
  whitespace. It is a line-for-line port of the same algorithm used by
  `vs-code-extension`'s "Format Document" and `intellij-idea-extension`'s
  "Format Felidae File", so all three editors normalize `.fx` files the same
  way.
- `felidae-indent-line` — a lighter-weight heuristic indent function wired
  up as `indent-line-function`, for interactive typing.
- `felidae-run-file` (`C-c C-r`), `felidae-check-file` (`C-c C-c`),
  `felidae-visualize-file` (`C-c C-v`) — shell out to the `felidae`,
  `felidae_debug`, and `celidae` executables respectively, via
  `compile`/`compilation-mode`.

## Installation

Copy `felidae-mode.el` somewhere on your `load-path` and require it:

```elisp
(add-to-list 'load-path "/path/to/Felidae/emacs-extension")
(require 'felidae-mode)
```

Or with `use-package`:

```elisp
(use-package felidae-mode
  :load-path "/path/to/Felidae/emacs-extension"
  :mode "\\.fx\\'")
```

`.fx` files are associated with `felidae-mode` automatically once the
package is loaded.

## Configuration

```elisp
(setq felidae-interpreter-path "/path/to/build/felidae")
(setq felidae-debug-interpreter-path "/path/to/build/felidae_debug")
(setq felidae-celidae-path "/path/to/build/celidae")
(setq felidae-indent-offset 4) ;; default
```

All three paths may be a bare command name (resolved via `exec-path`) or an
absolute path, matching the `felidae.interpreterPath` /
`felidae.debugInterpreterPath` / `felidae.celidaePath` settings in
`vs-code-extension`.
