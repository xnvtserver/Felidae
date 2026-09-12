# Felidae documentation

The supported product path is deliberately small and direct:

```text
source.fx -> deterministic lexer + byte-level tokenizer -> IntegerParser -> Program AST -> Interpreter
```

Build artifacts are created only in `build/`. Use the repository
[`README.md`](../README.md) for build, direct interpretation, the
byte-level tokenizer, mixfix, fact-memory, and source-reload guidance.

There is no executable IR, binary program format, VM, SentencePiece model, or
SSM path. The AST interpreter and `FactMemory` are authoritative at runtime.
