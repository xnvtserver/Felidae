# vim-extension

Vim / Neovim runtime files for Felidae (`.fx`) files.

Felidae has no Vim-native parser, so — like this repository's VS Code and
IntelliJ extensions — syntax highlighting, indentation, and formatting all
work directly on buffer text rather than an AST.

## Features

- `.fx` filetype detection (`ftdetect/felidae.vim`).
- Syntax highlighting (`syntax/felidae.vim`): keywords, `#` comments,
  strings, numbers, declarations, `:=` bindings, standard-library module
  names, type annotations, and constants.
- `indent/felidae.vim` — a lightweight per-line `indentexpr` heuristic for
  interactive typing.
- `:FelidaeFormat` — a structural beautifier that recomputes indentation,
  collapses extra blank lines, and trims trailing whitespace. It is a
  line-for-line port of the same algorithm used by `vs-code-extension`'s
  "Format Document" and `intellij-idea-extension`'s "Format Felidae File",
  so all editors in this repository normalize `.fx` files the same way.
- `:FelidaeRun`, `:FelidaeCheck`, `:FelidaeVisualize` — shell out to the
  `felidae`, `felidae_debug`, and `celidae` executables respectively.
- Buffer-local mappings: `<LocalLeader>ff` (format), `<LocalLeader>fr` (run),
  `<LocalLeader>fc` (check), `<LocalLeader>fv` (visualize).
- Optional, Neovim-only: `lua/felidae/init.lua` registers this repository's
  `tree-sitter-felidae/` grammar with `nvim-treesitter` so `:TSInstall
  felidae` builds richer tree-sitter-based highlighting from the same
  grammar used for the Zed extension. Everything above works without this -
  it is purely an opt-in upgrade.

## Installation

### Plain Vim or Neovim (native package management)

```sh
mkdir -p ~/.vim/pack/felidae/start
ln -s /path/to/Felidae/vim-extension ~/.vim/pack/felidae/start/felidae
```

(For Neovim, use `~/.local/share/nvim/site/pack/felidae/start/felidae`
instead.)

### A plugin manager (e.g. vim-plug, lazy.nvim)

```vim
Plug '/path/to/Felidae/vim-extension'
```

```lua
{ dir = "/path/to/Felidae/vim-extension" }
```

### Optional: Neovim tree-sitter highlighting

Requires `nvim-treesitter` and, to build the grammar, `tree-sitter-cli` and
a C compiler:

```lua
require('felidae').setup()
-- then, in Neovim:
-- :TSInstall felidae
```

## Configuration

```vim
let g:felidae_interpreter_path = '/path/to/build/felidae'
let g:felidae_debug_interpreter_path = '/path/to/build/felidae_debug'
let g:felidae_celidae_path = '/path/to/build/celidae'
```

All three default to the bare command name (resolved via `$PATH`) and
mirror the `felidae.interpreterPath` / `felidae.debugInterpreterPath` /
`felidae.celidaePath` settings in `vs-code-extension`.
