#include "Tokenizer.h"

namespace Felidae {

std::vector<EncodedToken>
ByteTokenizer::encodeWithOffsets(std::string_view text) const {
  std::vector<EncodedToken> result;
  result.reserve(text.size());
  for (std::size_t offset = 0; offset < text.size(); ++offset) {
    const int id = kFirstByteToken + static_cast<unsigned char>(text[offset]);
    result.push_back({id, offset, offset + 1});
  }
  return result;
}

std::vector<int> ByteTokenizer::encode(std::string_view text) const {
  const auto encoded = encodeWithOffsets(text);
  std::vector<int> result;
  result.reserve(encoded.size());
  for (const auto& token : encoded) result.push_back(token.id);
  return result;
}

} // namespace Felidae
