#pragma once

#include <FelidaeSentencePieceIds.h>

#include <string>

namespace sentencepiece {
class SentencePieceProcessor;
}

namespace Felidae {

// Loads the fixed, checked-in model once.  This performs no training and
// starts no worker threads; callers only use it to encode complete sources.
const sentencepiece::SentencePieceProcessor& felidaeSentencePieceModel();
// Stable artifact compatibility identity for compiler mixfix models.
std::string felidaeSentencePieceModelHash();

} // namespace Felidae
