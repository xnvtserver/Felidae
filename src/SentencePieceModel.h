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
// Exact executable-vocabulary identity used by FELBIR trust boundaries.
std::string felidaeSentencePieceModelIdentity();

} // namespace Felidae
