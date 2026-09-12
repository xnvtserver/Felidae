#pragma once

#include "FelidaeGrammar.h"
#include "Tokenizer.h"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Felidae {

// The one place a spelling is checked against the fixed grammar/reserved-word
// table (keywords, punctuation, operators, and the lexer-owned reserved words
// - class/end/index/extends/elif/def) - returns TokenId::UNKNOWN if `spelling`
// is not one of them. encodeNextStatement uses this for ordinary source
// scanning; IntegerParser::registerOperatorPattern must use the exact same
// lookup when computing a mixfix anchor's token IDs, or an anchor whose
// literal text happens to equal a keyword (e.g. "as") would be registered
// under different IDs than the source scanner ever produces for it, and
// could never match - the bug this function exists to make impossible by
// construction rather than by two call sites happening to agree.
TokenId::Id fixedGrammarTokenId(std::string_view spelling);

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
};

} // namespace Felidae
