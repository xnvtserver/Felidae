#include "IntegerTokenList.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace Felidae {
IntegerTokenList::IntegerTokenList(std::shared_ptr<ByteTokenizer> tokenizer,
                                   std::string source)
    : source_(std::move(source)), tokenizer_(std::move(tokenizer)) {
  if (!tokenizer_)
    throw std::invalid_argument("IntegerTokenList requires a tokenizer");
  encodeSource();
}

void IntegerTokenList::encodeSource() {
  const std::string_view statement(source_);
  if (!statement.empty()) ++encodeCount_;
  const auto push = [&](TokenId::Id id, std::size_t first, std::size_t last) {
    entries_.push_back(Entry{id, first, last});
  };
  const auto fixedId = [](std::string_view spelling) -> TokenId::Id {
    for (std::size_t index = 0; index < std::size(kBuiltinTokens); ++index) {
      if (kBuiltinTokens[index].spelling == spelling)
        return kFelidaeBuiltinTokenIds[index];
    }
    if (spelling == "class") return TokenId::CLASS;
    if (spelling == "end") return TokenId::END;
    if (spelling == "index") return TokenId::INDEX;
    if (spelling == "extends") return TokenId::EXTENDS;
    if (spelling == "elif") return TokenId::ELIF;
    if (spelling == "new") return TokenId::NEW;
    if (spelling == "for") return TokenId::FOR;
    if (spelling == "in") return TokenId::IN;
    if (spelling == "while") return TokenId::WHILE;
    if (spelling == "switch") return TokenId::SWITCH;
    if (spelling == "case") return TokenId::CASE;
    if (spelling == "default") return TokenId::DEFAULT;
    if (spelling == "break") return TokenId::BREAK;
    if (spelling == "continue") return TokenId::CONTINUE;
    if (spelling == "def") return TokenId::DEF;
    if (spelling == "this") return TokenId::THIS;
    if (spelling == "super") return TokenId::SUPER;
    if (spelling == "try") return TokenId::TRY;
    if (spelling == "catch") return TokenId::CATCH;
    if (spelling == "=") return TokenId::EQUAL;
    return TokenId::UNKNOWN;
  };
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
    if (statement[offset] == '\'') {
      push(TokenId::ATOM_QUOTE, offset, offset + 1);
      const std::size_t content = ++offset;
      while (offset < statement.size() && statement[offset] != '\'') ++offset;
      if (content != offset) push(TokenId::UNKNOWN, content, offset);
      if (offset < statement.size()) {
        push(TokenId::ATOM_QUOTE, offset, offset + 1);
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
                                           std::string_view("=>"),
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
    if (keyword != TokenId::UNKNOWN || word == "class" || word == "end" ||
        word == "index" || word == "extends" || word == "elif" || word == "new" ||
        word == "for" || word == "in" || word == "while" || word == "switch" ||
        word == "case" || word == "default" || word == "break" ||
        word == "continue" || word == "def" ||
        word == "try" || word == "catch") {
      push(keyword, first, offset);
      continue;
    }
    for (const auto &piece : tokenizer_->encodeWithOffsets(word))
      push(static_cast<TokenId::Id>(piece.id), first + piece.begin, first + piece.end);
  }
}

IntegerTokenList::LineColumn IntegerTokenList::lineColumn(std::size_t offset) const {
  if (lineStarts_.empty()) {
    lineStarts_.push_back(0);
    for (std::size_t index = 0; index < source_.size(); ++index) {
      if (source_[index] == '\r') {
        if (index + 1 < source_.size() && source_[index + 1] == '\n') ++index;
        lineStarts_.push_back(index + 1);
      } else if (source_[index] == '\n') {
        lineStarts_.push_back(index + 1);
      }
    }
  }
  offset = std::min(offset, source_.size());
  const auto next = std::upper_bound(lineStarts_.begin(), lineStarts_.end(), offset);
  const auto line = static_cast<std::size_t>(next - lineStarts_.begin());
  return LineColumn{static_cast<int>(line),
                    static_cast<int>(offset - lineStarts_[line - 1] + 1)};
}

bool IntegerTokenList::has(std::size_t index) const {
  return index < entries_.size();
}

const IntegerTokenList::Entry &
IntegerTokenList::entry(std::size_t index) const {
  if (!has(index))
    throw std::out_of_range("byte tokenizer token index is out of range");
  return entries_[index];
}

const std::vector<IntegerTokenList::Entry> &IntegerTokenList::entries() const {
  return entries_;
}

} // namespace Felidae
