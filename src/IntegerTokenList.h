#pragma once

#include "FelidaeGrammar.h"

#include <cstddef>
#include <string>
#include <vector>

namespace sentencepiece {
class SentencePieceProcessor;
}

namespace Felidae {

// The sole source-tokenization result. Entries are emitted verbatim from one
// SentencePieceProcessor::Encode call over the entire source; this type never
// assigns token categories or performs source scanning.
class IntegerTokenList {
public:
    struct Entry {
        TokenId::Id id = TokenId::UNKNOWN;
        std::size_t begin = 0;
        std::size_t end = 0;
    };

    IntegerTokenList() = default;
    IntegerTokenList(const sentencepiece::SentencePieceProcessor& processor,
                     std::string source);

    const std::string& source() const noexcept { return source_; }
    const std::vector<Entry>& entries() const noexcept { return entries_; }
    std::size_t encodeCount() const noexcept { return encodeCount_; }

private:
    std::string source_;
    std::vector<Entry> entries_;
    std::size_t encodeCount_ = 0;
    std::string to_string() const;
};

} // namespace Felidae
