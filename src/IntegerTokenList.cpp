#include "IntegerTokenList.h"

#include <sentencepiece.pb.h>
#include <sentencepiece_processor.h>

#include <stdexcept>

namespace Felidae {

IntegerTokenList::IntegerTokenList(
    const sentencepiece::SentencePieceProcessor &processor, std::string source)
    : source_(std::move(source)), processor_(&processor),
      complete_(source_.empty()) {
  if (!complete_)
    encodeNextLine();
}

void IntegerTokenList::encodeNextLine() const {
  if (complete_)
    return;
  if (!processor_)
    throw std::runtime_error("SentencePiece processor is unavailable");
  const auto lineBegin = nextLineBegin_;
  const auto lineNumber = nextLineNumber_;
  const auto newline = source_.find_first_of("\r\n", lineBegin);
  auto lineEnd = newline == std::string::npos ? source_.size() : newline + 1;
  if (newline != std::string::npos && source_[newline] == '\r' &&
      lineEnd < source_.size() && source_[lineEnd] == '\n') {
    ++lineEnd;
  }
  const absl::string_view line(source_.data() + lineBegin,
                               lineEnd - lineBegin);
  sentencepiece::SentencePieceText encoded;
  const auto status = processor_->Encode(line, &encoded);
  if (!status.ok()) {
    throw std::runtime_error("SentencePiece encoding failed on source line " +
                             std::to_string(lineNumber) + ": " +
                             status.ToString());
  }
  ++encodeCount_;
  entries_.reserve(entries_.size() +
                   static_cast<std::size_t>(encoded.pieces_size()));
  for (const auto &piece : encoded.pieces()) {
    const auto relativeBegin = static_cast<std::size_t>(piece.begin());
    const auto relativeEnd = static_cast<std::size_t>(piece.end());
    if (relativeBegin > relativeEnd || relativeEnd > line.size()) {
      throw std::runtime_error(
          "SentencePiece returned an invalid offset on source line " +
          std::to_string(lineNumber));
    }
    const auto id = static_cast<TokenId::Id>(piece.id());
    entries_.push_back(
        Entry{id, lineBegin + relativeBegin, lineBegin + relativeEnd});
  }
  // lineEnd is strictly greater than lineBegin whenever complete_ is false,
  // including blank and CRLF lines, so every lazy-load call advances.
  nextLineBegin_ = lineEnd;
  ++nextLineNumber_;
  complete_ = nextLineBegin_ >= source_.size();
}

bool IntegerTokenList::has(std::size_t index) const {
  while (index >= entries_.size() && !complete_)
    encodeNextLine();
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
    encodeNextLine();
  return entries_;
}

} // namespace Felidae
