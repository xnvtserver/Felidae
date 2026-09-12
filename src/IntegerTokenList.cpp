#include "IntegerTokenList.h"

#include <cctype>
#include <stdexcept>
#include <unordered_map>

namespace Felidae {
namespace {

// Built once, on first use: a hash lookup instead of a linear chain of
// string_view comparisons (up to 59 of them) against every word and
// punctuation candidate encodeNextStatement scans - which used to run for
// every single identifier too, since a non-keyword only falls through to
// tokenization after failing every comparison in the chain.
const std::unordered_map<std::string_view, TokenId::Id>& fixedIdTable() {
    static const std::unordered_map<std::string_view, TokenId::Id> table = [] {
        std::unordered_map<std::string_view, TokenId::Id> map;
        map.reserve(std::size(kBuiltinTokens) + 6);
        for (std::size_t index = 0; index < std::size(kBuiltinTokens); ++index) {
            map.emplace(kBuiltinTokens[index].spelling, static_cast<TokenId::Id>(index + 1));
        }
        map.emplace("class", TokenId::CLASS);
        map.emplace("end", TokenId::END);
        map.emplace("index", TokenId::INDEX);
        map.emplace("extends", TokenId::EXTENDS);
        map.emplace("elif", TokenId::ELIF);
        map.emplace("def", TokenId::DEF);
        return map;
    }();
    return table;
}

} // namespace

TokenId::Id fixedGrammarTokenId(std::string_view spelling) {
    const auto& table = fixedIdTable();
    const auto found = table.find(spelling);
    return found == table.end() ? TokenId::UNKNOWN : found->second;
}

namespace {

std::size_t statementEnd(std::string_view source, std::size_t begin) {
  bool quoted = false;
  bool escaped = false;
  bool comment = false;
  for (std::size_t index = begin; index < source.size(); ++index) {
    const char byte = source[index];
    if (comment) {
      if (byte == '\n' || byte == '\r')
        comment = false;
      continue;
    }
    if (quoted) {
      if (escaped)
        escaped = false;
      else if (byte == '\\')
        escaped = true;
      else if (byte == '"')
        quoted = false;
      continue;
    }
    if (byte == '"') {
      quoted = true;
      continue;
    }
    if (byte == '#') {
      comment = true;
      continue;
    }
    if (byte != '.')
      continue;
    // Decimal points and adjacent member access are grammar punctuation, not
    // framing. An explicit terminator is followed by trivia or end of input.
    const auto next = index + 1;
    if (next == source.size() || source[next] == ' ' || source[next] == '\t' ||
        source[next] == '\r' || source[next] == '\n' || source[next] == '#') {
      auto end = next;
      for (;;) {
        while (end < source.size() &&
               (source[end] == ' ' || source[end] == '\t' ||
                source[end] == '\r' || source[end] == '\n'))
          ++end;
        if (end == source.size() || source[end] != '#')
          return end;
        while (end < source.size() && source[end] != '\r' && source[end] != '\n')
          ++end;
      }
    }
  }
  return source.size();
}

} // namespace

IntegerTokenList::IntegerTokenList(std::shared_ptr<WordVocabulary> tokenizer,
                                   std::string source)
    : source_(std::move(source)), tokenizer_(std::move(tokenizer)),
      complete_(source_.empty()) {
  if (!tokenizer_)
    throw std::invalid_argument("IntegerTokenList requires a tokenizer");
  if (!complete_)
    encodeNextStatement();
}

void IntegerTokenList::encodeNextStatement() const {
  if (complete_)
    return;
  const auto begin = nextStatementBegin_;
  const auto end = statementEnd(source_, begin);
  const std::string_view statement(source_.data() + begin, end - begin);
  ++encodeCount_;
  const auto push = [&](TokenId::Id id, std::size_t first, std::size_t last) {
    entries_.push_back(Entry{id, begin + first, begin + last});
  };
  const auto fixedId = fixedGrammarTokenId;
  for (std::size_t offset = 0; offset < statement.size();) {
    const unsigned char byte = static_cast<unsigned char>(statement[offset]);
    if (statement[offset] == '#') {
      const std::size_t first = offset++;
      push(TokenId::COMMENT, first, offset);
      const std::size_t content = offset;
      while (offset < statement.size() && statement[offset] != '\n' && statement[offset] != '\r') ++offset;
      if (content != offset) push(TokenId::UNKNOWN, content, offset);
      continue;
    }
    if (statement[offset] == '"') {
      push(TokenId::QUOTE, offset, offset + 1);
      const std::size_t content = ++offset;
      bool escaped = false;
      while (offset < statement.size()) {
        const char current = statement[offset];
        if (!escaped && current == '"') break;
        escaped = !escaped && current == '\\';
        if (current != '\\') escaped = false;
        ++offset;
      }
      if (content != offset) push(TokenId::UNKNOWN, content, offset);
      if (offset < statement.size()) {
        push(TokenId::QUOTE, offset, offset + 1);
        ++offset;
      }
      continue;
    }
    if (std::isspace(byte)) {
      const TokenId::Id id = statement[offset] == ' ' ? TokenId::SPACE
          : statement[offset] == '\t' ? TokenId::TAB
          : statement[offset] == '\n' ? TokenId::NEWLINE : TokenId::CARRIAGE_RETURN;
      push(id, offset, offset + 1);
      ++offset;
      continue;
    }
    if (std::isdigit(byte)) {
      push(static_cast<TokenId::Id>(TokenId::DIGIT_0 + statement[offset] - '0'), offset, offset + 1);
      ++offset;
      continue;
    }
    bool matched = false;
    for (const std::string_view syntax : {std::string_view(":="), std::string_view("::"),
                                           std::string_view("=>"), std::string_view("=="),
                                           std::string_view("!="), std::string_view("<="),
                                           std::string_view(">=")}) {
      if (statement.substr(offset).starts_with(syntax)) {
        push(fixedId(syntax), offset, offset + syntax.size());
        offset += syntax.size();
        matched = true;
        break;
      }
    }
    if (matched) continue;
    const TokenId::Id punctuation = fixedId(statement.substr(offset, 1));
    if (punctuation != TokenId::UNKNOWN) {
      push(punctuation, offset, offset + 1);
      ++offset;
      continue;
    }
    const std::size_t first = offset;
    while (offset < statement.size()) {
      const unsigned char current = static_cast<unsigned char>(statement[offset]);
      if (!(std::isalnum(current) || current == '_' || current >= 0x80)) break;
      ++offset;
    }
    if (first == offset) {
      push(TokenId::UNKNOWN, offset, offset + 1);
      ++offset;
      continue;
    }
    const std::string_view word = statement.substr(first, offset - first);
    const TokenId::Id keyword = fixedId(word);
    if (keyword != TokenId::UNKNOWN) {
      push(keyword, first, offset);
      continue;
    }
    for (const auto &piece : tokenizer_->encodeWithOffsets(word))
      push(static_cast<TokenId::Id>(piece.id), first + piece.begin, first + piece.end);
  }
  nextStatementBegin_ = end;
  complete_ = end == source_.size();
}

bool IntegerTokenList::has(std::size_t index) const {
  while (index >= entries_.size() && !complete_)
    encodeNextStatement();
  return index < entries_.size();
}

const IntegerTokenList::Entry &
IntegerTokenList::entry(std::size_t index) const {
  if (!has(index))
    throw std::out_of_range("word vocabulary token index is out of range");
  return entries_[index];
}

const std::vector<IntegerTokenList::Entry> &IntegerTokenList::entries() const {
  while (!complete_)
    encodeNextStatement();
  return entries_;
}

} // namespace Felidae
