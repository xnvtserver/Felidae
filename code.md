# Felidae architecture

Felidae executes source directly. There is no compiler, IR, binary artifact,
VM, or statistical mixfix model in the supported path.

```text
source.fx
  -> custom BPE tokenizer (stable line IDs and byte offsets)
  -> IntegerTokenList
  -> IntegerParser
  -> Program AST / SymbolId interning
  -> Interpreter::addProgram
  -> solve / callMain / callAutoEntry
```

`models/felidae-bpe/model.txt` is the checked-in, line-oriented identifier
vocabulary: line N is token ID N. The normal lexer owns fixed syntax, comments,
numbers, and strings; only identifiers and mixfix anchors use BPE. Unknown
identifier words are added in source order, and execution needs no training.

`FactMemory` is Felidae's in-process fact database. It uses immutable,
copy-on-write relation roots and bounded snapshots; the interpreter reuses its
indexes, paging, provenance, rollback, and source reload behavior.

Mixfix is deterministic parser and AST-interpreter behavior. Literal anchors
are tokenized from their source spelling and matched by the registered
operator registry; no SSM participates.

Run `felidae program.fx`, or `felidae program.fx --serve` to replace the live
interpreter after a successful source reload.
