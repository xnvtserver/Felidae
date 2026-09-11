#include "IntegerTokenList.h"

#include <sentencepiece.pb.h>
#include <sentencepiece_processor.h>

#include <stdexcept>

namespace Felidae {
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

IntegerTokenList::IntegerTokenList(
    const sentencepiece::SentencePieceProcessor &processor, std::string source)
    : source_(std::move(source)), processor_(&processor),
      complete_(source_.empty()) {
  if (!complete_)
    encodeNextStatement();
}

void IntegerTokenList::encodeNextStatement() const {
  if (complete_)
    return;
  const auto begin = nextStatementBegin_;
  const auto end = statementEnd(source_, begin);
  const absl::string_view statement(source_.data() + begin, end - begin);
  sentencepiece::SentencePieceText encoded;
  const auto status = processor_->Encode(statement, &encoded);
  if (!status.ok()) {
    throw std::runtime_error("SentencePiece source encoding failed: " +
                             status.ToString());
  }
  ++encodeCount_;
  entries_.reserve(entries_.size() +
                   static_cast<std::size_t>(encoded.pieces_size()));
  for (const auto &piece : encoded.pieces()) {
    const auto relativeBegin = static_cast<std::size_t>(piece.begin());
    const auto relativeEnd = static_cast<std::size_t>(piece.end());
    if (relativeBegin > relativeEnd || relativeEnd > statement.size())
      throw std::runtime_error("SentencePiece returned an invalid source offset");
    const auto id = static_cast<TokenId::Id>(piece.id());
    entries_.push_back(
        Entry{id, begin + relativeBegin, begin + relativeEnd});
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
    throw std::out_of_range("SentencePiece token index is out of range");
  return entries_[index];
}

const std::vector<IntegerTokenList::Entry> &IntegerTokenList::entries() const {
  while (!complete_)
    encodeNextStatement();
  return entries_;
}

} // namespace Felidae
