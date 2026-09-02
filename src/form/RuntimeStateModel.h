#pragma once

#include "RegisterVm.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

namespace Felidae {

struct RuntimeTrainingRecord;

// The Form daemon's recurrent model is intentionally separate from the
// compiler's mixfix IR decoder. It selects only finite, typed VM outcomes.
enum class RuntimeOutputTokenKind : std::uint8_t {
    InputReference,
    FactFromInput,
    DegreeMilli,
    Nil,
    NumericTruth,
};

struct RuntimeOutputToken {
    RuntimeOutputTokenKind kind = RuntimeOutputTokenKind::Nil;
    std::size_t value = 0;
};

// The GRU may select one of these bounded argument positions.  This remains
// a finite decoder vocabulary and every selected position is range-checked
// against the current operation inputs before it enters a VM register.
inline constexpr std::size_t kRuntimeModelReferenceLimit = 16;
inline constexpr std::int64_t kRuntimeStructuralInputTokens = 48;
inline constexpr std::string_view kRuntimeArtifactFormatVersion = "2";
inline constexpr std::string_view kRuntimeModelFamily =
    "felidae-vm-runtime-gru";
inline constexpr std::string_view kRuntimeModelVersion = "runtime-gru-v2";
inline constexpr std::string_view kRuntimeTokenizerContract =
    "sentencepiece-structured-values-v2";
inline constexpr std::string_view kRuntimeDecoderContract =
    "typed-actions-plus-finite-score-v2";

// Single production vocabulary contract used by both the VM loader and the
// C++ trainer. A manifest's output_vocabulary is checked against this list.
std::vector<RuntimeOutputToken> defaultRuntimeOutputVocabulary();

class GruRuntimeStateModel final : public RuntimeStateModel {
public:
    struct Configuration {
        std::int64_t inputVocabularySize = 0;
        std::int64_t outputVocabularySize = 0;
        std::int64_t embeddingSize = 64;
        std::int64_t hiddenSize = 128;
        std::int64_t layerCount = 1;
        bool allowRandomInitialization = false;
    };

    GruRuntimeStateModel(Configuration configuration,
                         std::vector<RuntimeOutputToken> outputVocabulary);
    ~GruRuntimeStateModel() override;
    GruRuntimeStateModel(GruRuntimeStateModel&&) noexcept;
    GruRuntimeStateModel& operator=(GruRuntimeStateModel&&) noexcept;
    GruRuntimeStateModel(const GruRuntimeStateModel&) = delete;
    GruRuntimeStateModel& operator=(const GruRuntimeStateModel&) = delete;

    std::shared_ptr<void> createExecutionState() override;
    static GruRuntimeStateModel loadVersioned(Configuration configuration,
                                              std::vector<RuntimeOutputToken> outputVocabulary,
                                              const std::filesystem::path& artifactDirectory,
                                              std::string_view expectedSentencePieceIdentity);
    Value evaluate(const RuntimeOperation& operation, std::span<const Value> inputs,
                   RuntimeContext& context) override;
    double trainTeacherForced(const RuntimeTrainingRecord& record,
                              std::size_t targetToken, double learningRate);
    double trainScore(const RuntimeTrainingRecord& record,
                      double learningRate);
    // Deterministic validation path for a persisted JSONL record. It returns
    // a finite vocabulary index without constructing a runtime VmValue.
    std::size_t predictTeacherToken(const RuntimeTrainingRecord& record) const;
    double predictScore(const RuntimeTrainingRecord& record) const;
    void saveCheckpoint(const std::filesystem::path& checkpointPath) const;
    void exportTorchScript(const std::filesystem::path& artifactPath,
                           std::string_view sentencePieceIdentity) const;

private:
    struct VersionedArtifact {};
    GruRuntimeStateModel(Configuration configuration,
                         std::vector<RuntimeOutputToken> outputVocabulary,
                         const std::filesystem::path& artifactPath,
                         VersionedArtifact);
    class Implementation;
    Configuration configuration_;
    std::vector<RuntimeOutputToken> outputVocabulary_;
    std::unique_ptr<Implementation> implementation_;
};

} // namespace Felidae
