#pragma once

#include "FelidaeGrammar.h"
#include "Tokenizer.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace Felidae {

// The sole source-tokenization result. Tokenization never decides whether a
// period ends a sequence or belongs to an expression; IntegerParser owns all
// statement, block, and REPL-completeness boundaries.
class IntegerTokenList {
public:
  struct Entry {
    TokenId::Id id = TokenId::UNKNOWN;
    std::size_t begin = 0;
    std::size_t end = 0;
  };

  IntegerTokenList() = default;
  IntegerTokenList(std::shared_ptr<ByteTokenizer> tokenizer, std::string source);

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
  const ByteTokenizer &tokenizer() const noexcept { return *tokenizer_; }

private:
  void encodeSource();
  std::string source_;
  std::shared_ptr<ByteTokenizer> tokenizer_;
  std::vector<Entry> entries_;
  std::size_t encodeCount_ = 0;
  mutable std::vector<std::size_t> lineStarts_;
};

} // namespace Felidae
