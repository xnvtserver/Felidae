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

`WordVocabulary` (`src/Tokenizer.h`) is a fixed, compile-time vocabulary:
59 fixed grammar IDs plus one token per possible byte value (315 entries
total), with no file on disk and no training step. The normal lexer owns
fixed syntax, comments, numbers, and strings; only identifiers and mixfix
anchors go through the byte-level tokenizer, one byte per token. See
`src/Tokenizer.h` for why byte-level tokens, rather than subword merging,
are the right fit for this interpreter.

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
