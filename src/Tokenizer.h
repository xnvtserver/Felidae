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

// Deterministic, training-free tokenizer. IntegerTokenList separates fixed
// syntax, strings, comments, numbers, and identifiers before calling this
// class. Each byte in an identifier or mixfix anchor maps to one token after
// the fixed grammar-token range. The complete vocabulary is compiled in;
// parsing never depends on a model file or mutable process-local state.
class WordVocabulary {
public:
  WordVocabulary() = default;

  std::vector<int> encode(std::string_view text) const;
  std::vector<EncodedToken> encodeWithOffsets(std::string_view text) const;
  std::string decode(std::span<const int> tokens) const;

  static constexpr int kFirstByteToken =
      static_cast<int>(std::size(kBuiltinTokens)) + 1;
  static constexpr int kVocabularySize = kFirstByteToken + 256;
};

} // namespace Felidae
