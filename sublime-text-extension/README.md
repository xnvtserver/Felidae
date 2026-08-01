# sublime-text-extension

Sublime Text package for Felidae (`.fx`) files.

Felidae has no Sublime-native parser, so — like this repository's VS Code
and IntelliJ extensions — syntax highlighting and formatting both work
directly on buffer text.

## Features

- `Felidae.sublime-syntax` — syntax highlighting: keywords, `#` comments,
  strings, numbers, declarations, `:=` bindings, standard-library module
  names, type annotations, and constants.
- **Felidae: Format Document** (command palette, `Ctrl+Shift+P` /
  `Cmd+Shift+P`) — a structural beautifier that recomputes indentation,
  collapses extra blank lines, and trims trailing whitespace. It is a
  line-for-line port of the same algorithm used by `vs-code-extension`'s
  "Format Document" and `intellij-idea-extension`'s "Format Felidae File",
  so all editors in this repository normalize `.fx` files the same way.
- `Felidae.sublime-build` — `Tools > Build` runs the current file with
  `felidae`; build variants (`Ctrl+Shift+B`) run `felidae_debug --check`
  and `celidae --html`.
- A few starter snippets (`import`, `fact`, `rule`, `method`) ported from
  `vs-code-extension/snippets/felidae.json`.

## Installation

Copy (or symlink) this directory into your Sublime Text `Packages`
directory as `Felidae`:

- Windows: `%APPDATA%\Sublime Text\Packages\Felidae`
- macOS: `~/Library/Application Support/Sublime Text/Packages/Felidae`
- Linux: `~/.config/sublime-text/Packages/Felidae`

(`Preferences > Browse Packages…` opens that directory directly.)

## Configuration

The build system's `felidae` / `felidae_debug` / `celidae` commands are
resolved via `$PATH` by default. Edit `Felidae.sublime-build` (`Tools >
Build System > Edit`) to point at absolute executable paths if they are not
on `$PATH`, matching `felidae.interpreterPath` /
`felidae.debugInterpreterPath` / `felidae.celidaePath` in
`vs-code-extension`.
