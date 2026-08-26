#include "IntegerTokenList.h"

#include <sentencepiece.pb.h>
#include <sentencepiece_processor.h>

#include <stdexcept>

namespace Felidae {

IntegerTokenList::IntegerTokenList(const sentencepiece::SentencePieceProcessor& processor,
                                   std::string source)
    : source_(std::move(source)) {
    sentencepiece::SentencePieceText encoded;
    const auto status = processor.Encode(source_, &encoded);
    if (!status.ok()) {
        throw std::runtime_error("SentencePiece source encoding failed: " + status.ToString());
    }
    ++encodeCount_;
    entries_.reserve(encoded.pieces_size());
    for (const auto& piece : encoded.pieces()) {
        const auto begin = static_cast<std::size_t>(piece.begin());
        const auto end = static_cast<std::size_t>(piece.end());
        if (begin > end || end > source_.size()) {
            throw std::runtime_error("SentencePiece returned an invalid source offset");
        }
        const auto id = static_cast<TokenId::Id>(piece.id());
        entries_.push_back(Entry{id, begin, end});
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
