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
#include <array>
#include <span>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Felidae {
namespace {

class Sha256 final {
public:
    void update(std::span<const std::uint8_t> bytes) {
        for (const auto byte : bytes) {
            block_[blockSize_++] = byte;
            bitCount_ += 8;
            if (blockSize_ == block_.size()) { transform(); blockSize_ = 0; }
        }
    }
    std::array<std::uint8_t, 32> finish() {
        block_[blockSize_++] = 0x80;
        if (blockSize_ > 56) {
            while (blockSize_ < block_.size()) block_[blockSize_++] = 0;
            transform(); blockSize_ = 0;
        }
        while (blockSize_ < 56) block_[blockSize_++] = 0;
        for (int shift = 56; shift >= 0; shift -= 8) block_[blockSize_++] = static_cast<std::uint8_t>(bitCount_ >> shift);
        transform();
        std::array<std::uint8_t, 32> digest{};
        for (std::size_t index = 0; index < state_.size(); ++index) {
            digest[4 * index] = static_cast<std::uint8_t>(state_[index] >> 24);
            digest[4 * index + 1] = static_cast<std::uint8_t>(state_[index] >> 16);
            digest[4 * index + 2] = static_cast<std::uint8_t>(state_[index] >> 8);
            digest[4 * index + 3] = static_cast<std::uint8_t>(state_[index]);
        }
        return digest;
    }
private:
    static constexpr std::array<std::uint32_t, 64> k{
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    static std::uint32_t rotate(std::uint32_t value, unsigned count) { return (value >> count) | (value << (32u - count)); }
    void transform() {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t index = 0; index < 16; ++index) {
            words[index] = (static_cast<std::uint32_t>(block_[4 * index]) << 24) |
                           (static_cast<std::uint32_t>(block_[4 * index + 1]) << 16) |
                           (static_cast<std::uint32_t>(block_[4 * index + 2]) << 8) |
                           block_[4 * index + 3];
        }
        for (std::size_t index = 16; index < words.size(); ++index) {
            const auto s0 = rotate(words[index - 15], 7) ^ rotate(words[index - 15], 18) ^ (words[index - 15] >> 3);
            const auto s1 = rotate(words[index - 2], 17) ^ rotate(words[index - 2], 19) ^ (words[index - 2] >> 10);
            words[index] = words[index - 16] + s0 + words[index - 7] + s1;
        }
        auto a=state_[0],b=state_[1],c=state_[2],d=state_[3],e=state_[4],f=state_[5],g=state_[6],h=state_[7];
        for (std::size_t index = 0; index < words.size(); ++index) {
            const auto s1=rotate(e,6)^rotate(e,11)^rotate(e,25), choice=(e&f)^((~e)&g);
            const auto temporary1=h+s1+choice+k[index]+words[index];
            const auto s0=rotate(a,2)^rotate(a,13)^rotate(a,22), majority=(a&b)^(a&c)^(b&c);
            const auto temporary2=s0+majority;
            h=g;g=f;f=e;e=d+temporary1;d=c;c=b;b=a;a=temporary1+temporary2;
        }
        state_[0]+=a;state_[1]+=b;state_[2]+=c;state_[3]+=d;state_[4]+=e;state_[5]+=f;state_[6]+=g;state_[7]+=h;
    }
    std::array<std::uint32_t, 8> state_{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::array<std::uint8_t, 64> block_{};
    std::size_t blockSize_ = 0;
    std::uint64_t bitCount_ = 0;
};

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

std::string felidaeSentencePieceModelIdentity() {
    (void)felidaeSentencePieceModel();
    std::ifstream input(sentencePieceModelPath(), std::ios::binary);
    if (!input) throw std::runtime_error("Unable to hash fixed Felidae SentencePiece model");
    Sha256 sha;
    std::array<std::uint8_t, 64 * 1024> buffer{};
    while (input) {
        input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        const auto size = input.gcount();
        if (size > 0) sha.update(std::span<const std::uint8_t>(buffer.data(), static_cast<std::size_t>(size)));
    }
    if (!input.eof()) throw std::runtime_error("Unable to read fixed Felidae SentencePiece model");
    std::ostringstream result;
    result << "sha256:" << std::hex << std::setfill('0');
    for (const auto byte : sha.finish()) result << std::setw(2) << static_cast<unsigned>(byte);
    return result.str();
}

} // namespace Felidae
