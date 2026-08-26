#include "IntegerTokenList.h"

#include <sentencepiece.pb.h>
#include <sentencepiece_processor.h>

#include <stdexcept>

namespace Felidae {

IntegerTokenList::IntegerTokenList(const sentencepiece::SentencePieceProcessor& processor,
                                   std::string source)
    : source_(std::move(source)) {
    std::size_t lineBegin = 0;
    std::size_t lineNumber = 1;
    while (lineBegin < source_.size()) {
        const auto newline = source_.find_first_of("\r\n", lineBegin);
        auto lineEnd = newline == std::string::npos ? source_.size() : newline + 1;
        if (newline != std::string::npos && source_[newline] == '\r' &&
            lineEnd < source_.size() && source_[lineEnd] == '\n') {
            ++lineEnd;
        }
        const auto line = source_.substr(lineBegin, lineEnd - lineBegin);
        sentencepiece::SentencePieceText encoded;
        const auto status = processor.Encode(line, &encoded);
        if (!status.ok()) {
            throw std::runtime_error("SentencePiece encoding failed on source line " +
                                     std::to_string(lineNumber) + ": " + status.ToString());
        }
        ++encodeCount_;
        entries_.reserve(entries_.size() + static_cast<std::size_t>(encoded.pieces_size()));
        for (const auto& piece : encoded.pieces()) {
            const auto relativeBegin = static_cast<std::size_t>(piece.begin());
            const auto relativeEnd = static_cast<std::size_t>(piece.end());
            if (relativeBegin > relativeEnd || relativeEnd > line.size()) {
                throw std::runtime_error("SentencePiece returned an invalid offset on source line " +
                                         std::to_string(lineNumber));
            }
            const auto id = static_cast<TokenId::Id>(piece.id());
            entries_.push_back(Entry{id, lineBegin + relativeBegin, lineBegin + relativeEnd});
        }
        lineBegin = lineEnd;
        ++lineNumber;
    }
}

std::string IntegerTokenList::to_string() const
{
    std::string result = "[";

    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const auto& entry = entries_[i];

        if (i > 0) {
            result += ", ";
        }

        result += std::to_string(entry.id);
    }

    result += "] (source length: ";
    result += std::to_string(source_.size());
    result += ")";

    return result;
}

} // namespace Felidae
