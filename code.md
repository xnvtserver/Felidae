# Felidae architecture

Felidae parses one source representation only:

```text
source.fx
  -> SentencePiece Encode (IDs and UTF-8 byte offsets, once)
  -> IntegerTokenList
  -> IntegerParser
  -> AST compiler / SymbolId interning
  -> verified integer IR
  -> .bin binary artifact
  -> Form RegisterVm
```

`felidae.model` is the lexical model. `src/FelidaeSentencePieceIds.h` is the
generated runtime vocabulary: it defines every `TokenId` used by the integer
parser. `src/FelidaeGrammar.h` is not a second ID table; it is the declarative
spelling specification consumed only when generating and validating the
model.

`IntegerTokenList` performs one full-source `SentencePieceProcessor::Encode`
call and preserves its byte offsets. It does not classify pieces, scan source
characters, or synthesize tokens. `IntegerParser` consumes ranges from that
same ID stream. Fixed grammar is matched by `TokenId`; multi-piece identifiers
are assembled as bounded payloads and interned after parsing.

Mixfix literal anchors are encoded once at declaration time and matched as
piece-ID sequences. SentencePiece IDs are lexical only: runtime identity uses
Felidae `SymbolId` and semantic `BuiltinId` values.

There is no Felidae lexer, `TokenType`, legacy parser, fallback tokenizer, or
fragment re-encoding path. Celidae is deprecated and excluded from the active
Felidae build and quality scope.

## Validation and measurement

`felidae_sentencepiece_model_test` validates that each declared grammar symbol
has exactly one atomic model piece and its generated ID matches the model.
It also covers ID-only parsing, offsets, Unicode identifiers, native qualified
calls, nested mixfix patterns, parser bounds, and one-encode invariants.

`felidae_integer_parser_benchmark` reports model initialization, full-source
encoding, parser work, token count, iteration count, recursion depth, and
backtracking attempts. Use `--dump-ids` to inspect model IDs and offsets for a
source file without invoking the parser.

`./quality_check.sh` uses a single low-priority build job by default. Expensive
sanitizer, Valgrind, and stress modes are opt-in.
