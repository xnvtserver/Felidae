#pragma once

#include "FelidaeIr.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Felidae {

// The only value emitted by a neural decoder. It is an index into the
// parser-owned finite output vocabulary, never an IR word or table index.
using MixfixVocabularyId = std::uint32_t;

// A decoder token never carries an unrestricted machine word.  References are
// indices into the parser-owned tables below and are resolved only after the
// finite-vocabulary decoder has stopped.
enum class MixfixIrTokenKind : std::uint8_t {
    End,
    Opcode,
    Register,
    ConstantReference,
    SymbolReference,
    FactReference,
    ProgramReference,
};

struct MixfixIrToken {
    MixfixIrTokenKind kind = MixfixIrTokenKind::End;
    IrWord value = 0;
};

struct MixfixContext {
    // Indexed by the decoder's finite vocabulary IDs.  This table is made by
    // the parser for one selected mixfix span; the model cannot invent table
    // entries, registers, source ranges, or raw size_t values.
    std::vector<MixfixIrToken> outputVocabulary;
    std::vector<IrWord> constantReferences;
    std::vector<IrWord> symbolReferences;
    std::vector<IrWord> factReferences;
    std::vector<IrWord> programReferences;
    std::size_t maximumOutputWords = 4096;
};

class MixfixStateModel {
public:
    virtual ~MixfixStateModel() = default;

    virtual std::vector<MixfixVocabularyId> transform(
        std::span<const SentencePieceId> input,
        const MixfixContext& context) = 0;
};

// A C++ LibTorch encoder/decoder GRU.  Its recurrent state is the only state
// that crosses SentencePiece fragments; it is deliberately independent of the
// parser, verifier, and VM so an SSM backend can replace it later.
class GruMixfixStateModel final : public MixfixStateModel {
public:
    struct Configuration {
        std::int64_t inputVocabularySize = 0;
        std::int64_t outputVocabularySize = 0;
        std::int64_t embeddingSize = 128;
        std::int64_t hiddenSize = 256;
        std::int64_t layerCount = 1;
        std::int64_t beginToken = 0;
        std::size_t maximumDecodeSteps = 4096;
        // Only the explicit training tool may opt into an untrained model.
        // Inference keeps this false and therefore requires an artifact.
        bool allowRandomInitialization = false;
    };

    // artifactPath is a LibTorch C++ archive created with torch::save; model
    // training/export is C++-only.  Constructing this backend in a build
    // without FELIDAE_ENABLE_LIBTORCH gives a clear runtime error.
    GruMixfixStateModel(Configuration configuration,
                        const std::filesystem::path& artifactPath);
    ~GruMixfixStateModel() override;

    GruMixfixStateModel(GruMixfixStateModel&&) noexcept;
    GruMixfixStateModel& operator=(GruMixfixStateModel&&) noexcept;
    GruMixfixStateModel(const GruMixfixStateModel&) = delete;
    GruMixfixStateModel& operator=(const GruMixfixStateModel&) = delete;

    std::vector<MixfixVocabularyId> transform(std::span<const SentencePieceId> input,
                                              const MixfixContext& context) override;

    // Validates the C++-generated manifest and artifact hash before loading.
    // expectedSentencePieceHash is supplied by the parser/model owner.
    static GruMixfixStateModel loadVersioned(
        Configuration configuration, const std::filesystem::path& artifactDirectory,
        std::string_view expectedSentencePieceHash,
        std::string_view expectedIrVocabularyVersion = "felidae-ir-v8");

    // One C++ LibTorch teacher-forcing optimization step. targetTokenIds must
    // contain the terminating IR_END vocabulary token.  This intentionally is
    // not called by parsing, normal builds, or inference.
    double trainTeacherForced(std::span<const SentencePieceId> input,
                              std::span<const std::int64_t> targetTokenIds,
                              double learningRate);

    void saveArtifact(const std::filesystem::path& artifactPath) const;

private:
    class Implementation;
    Configuration configuration_;
    std::unique_ptr<Implementation> implementation_;
};

// Shared by every backend: converts a finite decoder token stream to legal IR
// words and refuses unbounded output or missing IR_END.
std::vector<IrWord> resolveMixfixIrTokens(
    std::span<const MixfixIrToken> tokens, const MixfixContext& context);
std::vector<IrWord> resolveMixfixVocabularyIds(
    std::span<const MixfixVocabularyId> ids, const MixfixContext& context);

// The sole model-to-runtime boundary.  Callers supply the parser-built IR
// tables and source map; this function installs model words then verifies the
// complete canonical IR before it can reach RegisterVm.
FelidaeIr compileVerifiedMixfixIr(MixfixStateModel& model,
                                  std::span<const SentencePieceId> input,
                                  const MixfixContext& context,
                                  FelidaeIr irShell);

} // namespace Felidae
