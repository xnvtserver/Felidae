#pragma once
#include "FelidaeGrammar.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Felidae {

struct EncodedToken {
  int id = 0;
  std::size_t begin = 0;
  std::size_t end = 0;
};

// Byte-level, training-free tokenizer (ByT5-style): every byte of an
// already-delimited word becomes exactly one token, offset past the fixed
// grammar IDs (kBuiltinTokens, FelidaeGrammar.h). No merge table, no corpus,
// no training, no file on disk at all - the complete vocabulary (59 grammar
// tokens + 256 byte tokens = 315 total) is fixed at compile time, so
// construction and every encode call are pure, allocation-light functions
// of their input.
//
// This replaced an earlier real-BPE implementation (merge table trained
// once, offline, from this project's own identifier corpus). BPE exists to
// solve two problems, neither of which applies to this interpreter:
//   1. Bounding vocabulary size for a fixed-size learned embedding table -
//      there is no embedding table here; SymbolId (Symbol.h), the interned
//      identifier space the interpreter actually dispatches on, is already
//      unbounded and needs no compression.
//   2. Reducing token-stream length because attention cost is
//      superlinear in sequence length - IntegerParser's per-token cost is
//      O(1) (a switch, an array index), so more tokens costs proportionally
//      more, linearly, not quadratically.
// Since neither justification holds, byte-level tokens are not a
// simplification at the cost of correctness or speed - they are the
// right-sized design for a deterministic, non-neural tree-walking
// interpreter, and they are faster to produce than merged tokens were (no
// merge search at all) with only a small, linear increase in how many
// token entries a long identifier spans.
//
// encode/encodeWithOffsets are called with one already-delimited word at a
// time (an identifier, or a mixfix anchor lexeme) - word-boundary detection
// (comments, strings, whitespace, digits, fixed punctuation/operators,
// keywords) happens earlier, in IntegerTokenList's static lexing pass.
// IntegerParser::consumeNameRange() reconstructs an identifier's actual
// spelling straight from source bytes rather than decoding tokens (see its
// own comment), so it already tolerates one identifier spanning any number
// of adjacent token entries - going from a handful of merged pieces to one
// piece per byte needed no parser change at all.
class WordVocabulary {
public:
  WordVocabulary() = default;

  std::vector<int> encode(std::string_view text) const;
  std::vector<EncodedToken> encodeWithOffsets(std::string_view text) const;
  std::string decode(std::span<const int> tokens) const;

  // Derived from the fixed grammar table itself, not a hand-copied count:
  // grammar IDs are 1..std::size(kBuiltinTokens) (see FelidaeGrammar.h), so
  // byte tokens start right after. A hardcoded number here would silently
  // go stale (byte tokens colliding with grammar IDs, no compile error) the
  // moment a fixed grammar spelling is ever added or removed.
  static constexpr int kFirstByteToken = static_cast<int>(std::size(kBuiltinTokens)) + 1;
  static constexpr int kVocabularySize = kFirstByteToken + 256;
};

} // namespace Felidae
