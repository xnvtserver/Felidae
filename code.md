# Felidae architecture

Felidae tokenizes source into a `Program` AST and executes it directly.

```text
source.fx
  -> word vocabulary tokenizer (stable line IDs and byte offsets)
  -> IntegerTokenList
  -> IntegerParser
  -> Program AST / SymbolId interning
  -> Interpreter::addProgram
  -> solve / callMain / callAutoEntry
  -> RocksDB fact and graph store
```

`models/felidae-bpe/model.txt` is the checked-in, line-oriented identifier
vocabulary: line N is token ID N. The normal lexer owns fixed syntax, comments,
numbers, and strings; only identifiers and mixfix anchors go through the word
vocabulary (a deterministic dictionary lookup, not byte-pair encoding - the
directory name is retained for repository compatibility). Unknown
identifier words receive parse-local IDs in lexical order without rewriting
the checked-in model, and execution needs no training.

RocksDB is Felidae's authoritative persistent fact and graph database. The
interpreter keeps only execution state and bounded query results in memory;
typed bucket scans, indexes, stable node identities, and adjacency are read
from the store.

Mixfix is deterministic parser and AST-interpreter behavior. Literal anchors
are tokenized from their source spelling and matched by the registered
operator registry.

Run `felidae program.fx` for normal source-to-AST execution.

The same executable owns live execution debugging (`--debug`). Debug hooks are
not installed for an ordinary run.
