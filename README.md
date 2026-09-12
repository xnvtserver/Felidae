# Felidae

Felidae is a deterministic functional-logic language with an in-process fact
database. `.fx` source is tokenized, parsed to an AST, and evaluated directly
by the restored interpreter.

```text
source.fx -> word vocabulary tokenizer -> IntegerParser -> Program AST -> Interpreter
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

`models/felidae-bpe/model.txt` is the checked-in, deterministic word-vocabulary
identifier dictionary (not byte-pair encoding - see `src/Tokenizer.h` for why
the directory kept its old name). Its physical line number is the token ID.
The lexer owns the first 59 fixed syntax IDs plus reserved `class`, `extends`,
`index`, and `end`, as well as comments, strings, punctuation, and numbers;
the vocabulary only looks up whole identifier and mixfix-anchor words in the
text table. Unknown words are added deterministically in source order, with
no training step.

See [code.md](code.md) for the execution architecture and
[docs_language.md](docs_language.md) for language semantics.
