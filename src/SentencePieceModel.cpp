#include "SentencePieceModel.h"

#include "FelidaeGrammar.h"

#include <sentencepiece.pb.h>
#include <sentencepiece_processor.h>

#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

namespace Felidae {
namespace {

void validateBuiltins(const sentencepiece::SentencePieceProcessor& model) {
    for (std::size_t index = 0; index < std::size(kBuiltinTokens); ++index) {
        const auto& builtin = kBuiltinTokens[index];
        const int expected = kFelidaeBuiltinSentencePieceIds[index];
        const int actual = model.PieceToId(std::string(builtin.spelling));
        int matchingPieceCount = 0;
        for (int id = 0; id < model.GetPieceSize(); ++id) {
            if (model.IdToPiece(id) == builtin.spelling) ++matchingPieceCount;
        }
        if (actual != expected || actual == TokenId::UNKNOWN ||
            model.IdToPiece(actual) != builtin.spelling || matchingPieceCount != 1) {
            throw std::runtime_error("felidae.model built-in ID mismatch for '" +
                                     std::string(builtin.spelling) + "'");
        }
        sentencepiece::SentencePieceText encoded;
        const auto status = model.Encode(std::string(builtin.spelling), &encoded);
        if (!status.ok() || encoded.pieces_size() != 1 ||
            static_cast<int>(encoded.pieces(0).id()) != actual ||
            encoded.pieces(0).begin() != 0 ||
            encoded.pieces(0).end() != builtin.spelling.size()) {
            throw std::runtime_error("felidae.model splits or duplicates built-in '" +
                                     std::string(builtin.spelling) + "'");
        }
    }
}

} // namespace

const sentencepiece::SentencePieceProcessor& felidaeSentencePieceModel() {
    static std::once_flag once;
    static std::unique_ptr<sentencepiece::SentencePieceProcessor> model;
    std::call_once(once, [] {
        auto loaded = std::make_unique<sentencepiece::SentencePieceProcessor>();
        const auto status = loaded->Load(FELIDAE_SENTENCEPIECE_MODEL_PATH);
        if (!status.ok()) {
            throw std::runtime_error("Unable to load fixed Felidae SentencePiece model: " +
                                     status.ToString());
        }
        validateBuiltins(*loaded);
        model = std::move(loaded);
    });
    return *model;
}

} // namespace Felidae
