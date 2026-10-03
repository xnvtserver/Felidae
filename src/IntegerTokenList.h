#pragma once

#include "FelidaeGrammar.h"
#include "Tokenizer.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace Felidae {

// The sole source-tokenization result. WordVocabulary encodes complete
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
  IntegerTokenList(std::shared_ptr<WordVocabulary> tokenizer, std::string source);

  const std::string &source() const noexcept { return source_; }
  // 1-based line and column of a byte offset (clamped to the source size). A
  // line ends at \n, \r\n or a lone \r. The line starts are indexed once, so
  // each lookup is O(log lines); spans are stamped on every AST node, and
  // rescanning the source for each one made large files quadratic to parse.
  struct LineColumn {
    int line = 1;
    int column = 1;
  };
  LineColumn lineColumn(std::size_t offset) const;
  bool has(std::size_t index) const;
  const Entry &entry(std::size_t index) const;
  std::size_t loadedSize() const noexcept { return entries_.size(); }
  // Tool/test materialization boundary. Production parsing uses has/entry.
  const std::vector<Entry> &entries() const;
  std::size_t encodeCount() const noexcept { return encodeCount_; }
  const WordVocabulary &tokenizer() const noexcept { return *tokenizer_; }

private:
  void encodeNextStatement() const;
  std::string source_;
  std::shared_ptr<WordVocabulary> tokenizer_;
  mutable std::vector<Entry> entries_;
  mutable std::size_t nextStatementBegin_ = 0;
  mutable std::size_t encodeCount_ = 0;
  mutable bool complete_ = false;
  mutable std::vector<std::size_t> lineStarts_;
};

} // namespace Felidae
