# Contributing to Felidae

Felidae is an open-source C++ language and reasoning runtime. Source files are
tokenized, parsed into an AST, and executed directly by the interpreter.

## Development setup

Use CMake 3.21 or newer and a C++20 compiler. Keep every configuration below
`build/`:

```sh
./build.sh debug --test
./build.sh sanitize --test
./build.sh release
```

On Windows, use `build.ps1`. Do not commit generated build files.

## Changes

- Preserve documented language behavior unless a proposal explicitly changes
  the language contract.
- Reuse the shared tokenizer, parser, source loader, interpreter, and fact
  memory paths. Do not add editor-specific parsers or parallel evaluators.
- Add a focused regression test for every bug fix.
- Keep ownership, lifetime, source-span, fact-identity, cache-invalidation, and
  import-resolution contracts explicit near their authoritative types.
- Measure before adding performance-specific complexity.
- Do not change protected dependency revisions as part of unrelated work.

The `felidae` executable owns execution, live debugging, diagnostics, symbol
metadata, operator metadata, and the stdio language server. Editor integrations
must call these interfaces instead of reimplementing language semantics.

## Pull requests

Keep changes focused and explain the observed problem, intended behavior, and
verification performed. Use conventional commit subjects such as
`fix: invalidate query cache after fact mutation` or
`test: cover imported operator diagnostics`.

Security reports should follow [SECURITY.md](SECURITY.md) rather than public
issues.
