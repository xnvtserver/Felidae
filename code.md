# Felidae architecture

Felidae executes source directly. There is no compiler, IR, binary artifact,
VM, or statistical mixfix model in the supported path.

```text
source.fx
  -> byte-level tokenizer (stable IDs and byte offsets)
  -> IntegerTokenList
  -> IntegerParser
  -> Program AST / SymbolId interning
  -> Interpreter::addProgram
  -> solve / callMain / callAutoEntry
```

`WordVocabulary` (`src/Tokenizer.h`) is a fixed, compile-time vocabulary:
59 fixed grammar IDs plus one token per possible byte value (315 entries
total), with no file on disk and no training step. The normal lexer owns
fixed syntax, comments, numbers, and strings; only identifiers and mixfix
anchors go through the byte-level tokenizer, one byte per token. See
`src/Tokenizer.h` for why byte-level tokens, rather than subword merging,
are the right fit for this interpreter.

`FactMemory` is Felidae's in-process fact database. It uses immutable,
copy-on-write relation roots and bounded snapshots; the interpreter reuses its
indexes, paging, provenance, rollback, and source reload behavior.

Mixfix is deterministic parser and AST-interpreter behavior. Literal anchors
are tokenized from their source spelling and matched by the registered
operator registry; no SSM participates.

Run `felidae program.fx`, or `felidae program.fx --serve` to replace the live
interpreter after a successful source reload.

The same executable owns live debugging (`--debug`), AST checks
(`--check-json`), metadata, and LSP (`--lsp`). These modes share the
authoritative tokenizer, parser, source loader, and operator registry; no
separate tooling parser or visualization runtime exists.
