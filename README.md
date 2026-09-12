# Felidae

Felidae is a deterministic functional-logic language with an in-process fact
database. `.fx` source is tokenized, parsed to an AST, and evaluated directly
by the restored interpreter.

```text
source.fx -> byte-level tokenizer -> IntegerParser -> Program AST -> Interpreter
```

There is no compiler-to-IR conversion, binary program format, VM, SentencePiece
dependency, or SSM-based mixfix path.

## Build and run

```sh
cmake -S . -B build/debug -DFELIDAE_BUILD_TESTS=ON
cmake --build build/debug --target felidae -j2
build/debug/felidae tests/direct_ast_smoke.fx
build/debug/felidae v2_examples/mixfix_nested_expression.fx
build/debug/felidae tests/direct_ast_smoke.fx --serve
build/debug/felidae tests/direct_ast_smoke.fx --check-json
build/debug/felidae tests/direct_ast_smoke.fx --debug
build/debug/felidae --lsp
```

`--serve` watches the root module and loaded imports. A changed program is
parsed and registered in a replacement interpreter; the prior interpreter
remains alive if the new source is invalid.

`felidae` is also the single source-tooling executable. Static checks, symbol
and operator metadata, builtin/library listings, and the stdio language server
reuse the runtime tokenizer, parser, import loader, and operator registry.
They do not execute the program. Live breakpoints and stepping are enabled
only by `--debug`; normal execution leaves the goal hook unset.

## Facts and memory

`FactMemory` is the fact database. It owns fact values through immutable,
copy-on-write relation roots, supports indexed query candidates, source
provenance, transactional registration rollback, and explicit snapshots.
No external fact store is required for this interpreter-only runtime.

## Token model

`WordVocabulary` (`src/Tokenizer.h`) is a fixed, compile-time, byte-level
tokenizer: the first 59 IDs are Felidae's fixed grammar tokens
(`src/FelidaeTokenizerIds.h`), and every other byte value maps directly to
`59 + byte value` - a 315-entry vocabulary with no file on disk, no corpus,
and no training step. The normal lexer (`IntegerTokenList`) still owns
comments, strings, numbers, punctuation, and reserved words such as
`class`, `extends`, `index`, and `end`; only identifiers and mixfix anchors
reach the byte-level tokenizer, one byte per token. See `src/Tokenizer.h`
for why byte-level tokens, rather than subword merging, are the right fit
for this interpreter.

See [code.md](code.md) for the execution architecture and
[docs_language.md](docs_language.md) for language semantics.
