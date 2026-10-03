# Felidae architecture

Felidae tokenizes source into a `Program` AST and executes it directly.

```text
source.fx
  -> byte-level tokenizer (stable IDs and byte offsets)
  -> IntegerTokenList
  -> IntegerParser
  -> Program AST / SymbolId interning
  -> Interpreter::addProgram
  -> solve / callMain / callAutoEntry
  -> RocksDB fact and graph store
```

`WordVocabulary` is a fixed, compile-time byte vocabulary. The normal lexer
owns syntax, comments, numbers, and strings; identifier and mixfix-anchor
bytes are encoded after the grammar-token range. There is no tokenizer model,
training step, generated vocabulary, or mutable token assignment.

RocksDB is Felidae's authoritative persistent fact and graph database. The
interpreter keeps only execution state and bounded query results in memory;
typed bucket scans, indexes, stable node identities, and adjacency are read
from the store. Before constructing an executable runtime, the frontend parses
the entry program's sibling `init.fx`, requires `db.location(...)`, and opens
that RocksDB directory. It never silently substitutes an in-memory or
temporary fact store.

Mixfix is deterministic parser and AST-interpreter behavior. Literal anchors
are tokenized from their source spelling and matched by the registered
operator registry.

Run `felidae program.fx` for normal source-to-AST execution.

The same executable owns live execution debugging (`--debug`). Debug hooks are
not installed for an ordinary run.
