# tree-sitter-felidae

Tree-sitter grammar scaffold for the Felidae `.fx` language.

Tree-sitter turns source text into a concrete syntax tree. Editors and code
hosts can use that tree for syntax highlighting, symbol search, folding,
selection ranges, and eventually jump-to-definition style features.

This grammar is useful for Felidae tooling, but GitHub will not automatically
use a grammar that only exists inside this repository. GitHub code navigation
currently works only for languages that GitHub has enabled in its own
tree-sitter pipeline. To make Felidae first-class on GitHub, Felidae also needs
upstream language metadata in GitHub Linguist and GitHub-side tree-sitter
support.

## Local Development

Install the tree-sitter CLI, then run:

```powershell
npm install
npm run generate
npm run parse ..\examples\stdlib_utilities.fx
```

The current grammar covers the MVP syntax:

- imports
- facts and method/rule clauses
- `extend`
- named and positional calls
- `=>`, `:=`, `return`, `where`, `else`
- conjunction `,` and disjunction `|`
- maps, arrays, strings, numbers, `nil`, booleans, `_`
- member/module access with `.` or `:`
- lambda expressions

It is intentionally kept separate from the C++ parser. The C++ interpreter
remains the source of truth for execution and diagnostics.
