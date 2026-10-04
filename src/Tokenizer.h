#pragma once

#include "FelidaeGrammar.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Felidae {

struct EncodedToken {
  int id = 0;
  std::size_t begin = 0;
  std::size_t end = 0;
};

// Deterministic ByteT5-style byte tokenizer. IntegerTokenList owns grammar,
// strings, comments, and numbers; every identifier/mixfix byte maps directly
// into the fixed 256-byte range following the grammar IDs. No generated model,
// learned vocabulary, or mutable token assignment participates in parsing.
class ByteTokenizer {
public:
  ByteTokenizer() = default;

  std::vector<int> encode(std::string_view text) const;
  std::vector<EncodedToken> encodeWithOffsets(std::string_view text) const;

  // Grammar IDs deliberately retain retired gaps for protocol stability, so
  // this boundary is based on the highest grammar ID rather than token count.
  static constexpr int kFirstByteToken = TokenId::DIGIT_9 + 1;
};

static_assert(ByteTokenizer::kFirstByteToken > TokenId::DIGIT_9);

} // namespace Felidae
