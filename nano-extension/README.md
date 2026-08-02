# nano-extension

GNU nano syntax highlighting for Felidae (`.fx`) files.

nano has no plugin system, LSP support, or snippet/completion engine — a
"nano extension" is a `.nanorc` syntax-color definition, the same
mechanism nano's own bundled language files (`c.nanorc`, `python.nanorc`,
...) use. This covers highlighting only: keywords (`extend where if else
return lambda then import`), constants (`nil true false`), type
annotations, standard-library module names, fact/type names (capitalized
identifiers), `#` line comments (wired to nano's `M-3` comment/uncomment
via the `comment` directive), strings, numbers, operators, and a generic
`Name(` call/declaration highlight.

Regex-based highlighting is inherently heuristic — nano's syntax engine
uses POSIX extended regular expressions per line (no lookahead, no
multi-line string awareness), so, for example, a `#` inside a string
literal is still treated as starting a comment. This matches the
precision level of the Vim and Emacs extensions in this repository, which
carry the same disclaimer.

## Installation

1. Copy `felidae.nanorc` somewhere nano can find it, e.g.:
   - Linux (per-user): `~/.nano/felidae.nanorc`
   - Linux (system-wide): `/usr/share/nano/felidae.nanorc`
   - macOS (Homebrew nano): `/opt/homebrew/share/nano/felidae.nanorc`
2. Add an `include` line to `~/.nanorc` (create it if it doesn't exist)
   pointing at wherever you put the file, e.g.:

   ```
   include "~/.nano/felidae.nanorc"
   ```

3. Restart nano (or open a new terminal). Opening any `*.fx` file now
   highlights it automatically — no per-file configuration needed.

## Configuration

There's nothing to configure; `.nanorc` files aren't per-project. If
you want different colors, edit the `color <name> "<regex>"` lines
directly — nano's built-in color names are documented in `man nanorc`
under `color`.
