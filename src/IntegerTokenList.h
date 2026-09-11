#pragma once

#include "FelidaeGrammar.h"

#include <cstddef>
#include <string>
#include <vector>

namespace sentencepiece {
class SentencePieceProcessor;
}

namespace Felidae {

// The sole source-tokenization result. SentencePiece encodes complete
// dot-terminated statement spans, so multiline statements and `then` chains
// have one stable token stream. Newlines are formatting inside a span.
//
// `end` is a parser boundary, not a tokenizer boundary: discovering a safe
// block requires first distinguishing syntax from strings, comments, decimal
// points, member access, and mixfix anchors. Keep that decision in
// IntegerParser rather than duplicating a partial parser here.
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
  bool has(std::size_t index) const;
  const Entry &entry(std::size_t index) const;
  std::size_t loadedSize() const noexcept { return entries_.size(); }
  // Tool/test materialization boundary. Production parsing uses has/entry.
  const std::vector<Entry> &entries() const;
  std::size_t encodeCount() const noexcept { return encodeCount_; }

private:
  void encodeNextStatement() const;
  std::string source_;
  const sentencepiece::SentencePieceProcessor *processor_ = nullptr;
  mutable std::vector<Entry> entries_;
  mutable std::size_t nextStatementBegin_ = 0;
  mutable std::size_t encodeCount_ = 0;
  mutable bool complete_ = false;
};

} // namespace Felidae
