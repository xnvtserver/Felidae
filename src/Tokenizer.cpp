#include "Tokenizer.h"

#include <stdexcept>

namespace Felidae {

std::vector<EncodedToken>
WordVocabulary::encodeWithOffsets(std::string_view text) const {
  std::vector<EncodedToken> result;
  result.reserve(text.size());
  for (std::size_t offset = 0; offset < text.size(); ++offset) {
    const int id = kFirstByteToken + static_cast<unsigned char>(text[offset]);
    result.push_back({id, offset, offset + 1});
  }
  return result;
}

std::vector<int> WordVocabulary::encode(std::string_view text) const {
  std::vector<int> result;
  result.reserve(text.size());
  for (const unsigned char byte : text) result.push_back(kFirstByteToken + byte);
  return result;
}

std::string WordVocabulary::decode(std::span<const int> tokens) const {
  std::string result;
  result.reserve(tokens.size());
  for (const int id : tokens) {
    const int byte = id - kFirstByteToken;
    if (byte < 0 || byte > 255) {
      throw std::runtime_error("word vocabulary decode received an invalid token ID");
    }
    result.push_back(static_cast<char>(byte));
  }
  return result;
}

} // namespace Felidae
