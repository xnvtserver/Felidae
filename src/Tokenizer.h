#pragma once

#include <cstddef>
#include <filesystem>
#include <mutex>
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

// Deterministic, training-free BPE vocabulary. Line N in model.txt owns ID N.
// <0xNN> is the escaped spelling for bytes which cannot be represented
// literally in a line-oriented text file. Unknown identifier words are
// appended in encounter order and are immediately reusable by encode/decode.
class BpeTokenizer {
public:
  explicit BpeTokenizer(std::filesystem::path modelPath = {});

  std::vector<int> encode(std::string_view text) const;
  std::vector<EncodedToken> encodeWithOffsets(std::string_view text) const;
  std::string decode(std::span<const int> tokens) const;
  // Reserve unknown identifier words in lexical order before the parser
  // traverses the source. This makes newly assigned line IDs independent of
  // parser backtracking and statement chunking.
  void prime(std::string_view source) const;

  const std::filesystem::path &modelPath() const noexcept { return modelPath_; }

private:
  struct Entry {
    std::string spelling;
    std::string bytes;
  };

  void loadModel();
  void ensureFixedVocabulary();
  int appendToken(std::string_view bytes) const;
  int findToken(std::string_view bytes) const;
  void appendModelLine(std::string_view bytes) const;

  std::filesystem::path modelPath_;
  mutable std::vector<Entry> entries_;
  mutable std::vector<std::pair<std::string, int>> matchOrder_;
  mutable std::mutex mutex_;
};

std::filesystem::path defaultTokenizerModelPath();

} // namespace Felidae
