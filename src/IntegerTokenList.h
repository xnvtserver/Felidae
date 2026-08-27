#pragma once

#include "FelidaeGrammar.h"

#include <cstddef>
#include <string>
#include <vector>

namespace sentencepiece {
class SentencePieceProcessor;
}

namespace Felidae {

// The sole source-tokenization result. Each physical source line is encoded
// independently so no learned piece can cross a statement line. Newline bytes
// remain in the encoded slice and all returned offsets are rebased to the
// original source. This type assigns no secondary token categories.
class IntegerTokenList {
public:
  struct Entry {
    TokenId::Id id = TokenId::UNKNOWN;
    std::size_t begin = 0;
    std::size_t end = 0;
  };

  IntegerTokenList() = default;
  IntegerTokenList(const sentencepiece::SentencePieceProcessor &processor,
                   std::string source);

  const std::string &source() const noexcept { return source_; }
  // Parser-facing lazy access. Requesting the first token beyond the loaded
  // line encodes exactly the next physical line, never a block.
  bool has(std::size_t index) const;
  const Entry &entry(std::size_t index) const;
  std::size_t loadedSize() const noexcept { return entries_.size(); }
  // Tool/test materialization boundary. Production parsing uses has/entry.
  const std::vector<Entry> &entries() const;
  std::size_t encodeCount() const noexcept { return encodeCount_; }

private:
  void encodeNextLine() const;
  std::string source_;
  const sentencepiece::SentencePieceProcessor *processor_ = nullptr;
  mutable std::vector<Entry> entries_;
  mutable std::size_t nextLineBegin_ = 0;
  mutable std::size_t nextLineNumber_ = 1;
  mutable std::size_t encodeCount_ = 0;
  mutable bool complete_ = false;
};

} // namespace Felidae
