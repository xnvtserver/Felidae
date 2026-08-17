#include "SentencePieceModel.h"

#include "FelidaeGrammar.h"

#include <sentencepiece.pb.h>
#include <sentencepiece_processor.h>

#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <filesystem>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Felidae {
namespace {

std::filesystem::path sentencePieceModelPath() {
    const std::filesystem::path configured(FELIDAE_SENTENCEPIECE_MODEL_PATH);
    if (std::filesystem::is_regular_file(configured)) return configured;
#ifdef _WIN32
    std::wstring buffer(32'768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length != 0 && length < buffer.size()) {
        buffer.resize(length);
        const auto portable = std::filesystem::path(buffer).parent_path() / "models" / "felidae.model";
        if (std::filesystem::is_regular_file(portable)) return portable;
    }
#endif
    throw std::runtime_error("Unable to locate fixed Felidae SentencePiece model");
}

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
        const auto modelPath = sentencePieceModelPath();
        const auto status = loaded->Load(modelPath.string());
        if (!status.ok()) {
            throw std::runtime_error("Unable to load fixed Felidae SentencePiece model: " +
                                     status.ToString());
        }
        validateBuiltins(*loaded);
        model = std::move(loaded);
    });
    return *model;
}

std::string felidaeSentencePieceModelHash() {
    // Force the same path/load validation used by every source encode before
    // an artifact is allowed to bind to this vocabulary.
    (void)felidaeSentencePieceModel();
    std::ifstream input(sentencePieceModelPath(), std::ios::binary);
    if (!input) throw std::runtime_error("Unable to hash fixed Felidae SentencePiece model");
    std::uint64_t hash = 14695981039346656037ull;
    char byte = 0;
    while (input.get(byte)) { hash ^= static_cast<unsigned char>(byte); hash *= 1099511628211ull; }
    std::ostringstream result;
    result << "fnv1a64:" << std::hex << hash;
    return result.str();
}

} // namespace Felidae
