# zed-extension

Zed extension scaffold for Felidae (`.fx`) files: syntax highlighting,
bracket matching, indentation, and outline/symbol support, built entirely
on this repository's existing `tree-sitter-felidae/` grammar (see that
directory's README — "intentionally kept separate from the C++ parser").

This is a **grammar-only** extension: no language server is wired up.
`felidae_debug` already speaks LSP (diagnostics, symbols), but connecting
it requires Zed's Rust/WASM extension API (`zed_extension_api`, a
`Cargo.toml`, compiling to `wasm32-wasip1`), which is a separate, larger
undertaking from the grammar/query work done here.

## Files

- `extension.toml` — extension manifest and grammar declaration.
- `languages/felidae/config.toml` — file association, comment syntax,
  auto-closing brackets.
- `languages/felidae/highlights.scm` — copied verbatim from
  `../tree-sitter-felidae/queries/highlights.scm`; keep both in sync.
- `languages/felidae/brackets.scm` — bracket-pair matching.
- `languages/felidae/indents.scm` — indent/outdent on the grammar's
  bracket-delimited constructs (call arguments, grouped goals, map and
  array literals). This deliberately does not attempt clause- or
  `if...then...else`-level indentation, which - as documented at length in
  `vs-code-extension/src/formatter.ts`'s header comment - needs a fuller
  heuristic than a static query can express; use that extension's (or
  `intellij-idea-extension`'s / `emacs-extension`'s / `vim-extension`'s)
  explicit "format document" command for that.
- `languages/felidae/outline.scm` — feeds Zed's outline/"go to symbol"
  panel from top-level clause and `:=` binding declarations, the same
  declarations `vs-code-extension`'s document-symbol provider surfaces.

## Known limitation: the grammar needs a published git repository

Zed builds a tree-sitter grammar by `git clone`-ing the `repository` set
under `[grammars.felidae]` in `extension.toml` at the pinned `rev` (commit
SHA). `tree-sitter-felidae/` currently lives inside this monorepo rather
than as its own published repository, so `extension.toml` here has a
placeholder `rev` that will not resolve as-is.

To make this installable as a real (non-dev) extension:

1. Publish `tree-sitter-felidae/` somewhere Zed can `git clone` it — either
   its own repository, or (if Zed's current grammar-fetch mechanism
   supports a grammar living in a subdirectory of a larger repository;
   confirm against Zed's current extension docs, since this has changed
   between versions) this repository as-is.
2. Fill in `rev` in `extension.toml` with the resulting commit SHA.
3. For local development/testing before publishing anywhere, use Zed's
   **"zed: install dev extension"** command pointed at this directory -
   check Zed's current extension-development documentation for whether
   dev extensions support building a grammar from a local path directly
   (bypassing the git-clone step), since that would let you iterate
   without pushing anywhere first.

## Regenerating alongside the grammar

If `tree-sitter-felidae/grammar.js` changes, re-copy
`languages/felidae/highlights.scm` from
`tree-sitter-felidae/queries/highlights.scm`, and re-check
`indents.scm`/`outline.scm`/`brackets.scm` against any renamed or new node
and field names (`npm run generate` in `tree-sitter-felidae/` first, so
node/field names reflect the current grammar).
