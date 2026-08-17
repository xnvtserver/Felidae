#pragma once

#include "RegisterVm.h"

#include <cstdint>
#include <filesystem>
#include <memory>
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
    Boolean,
};

struct RuntimeOutputToken {
    RuntimeOutputTokenKind kind = RuntimeOutputTokenKind::Nil;
    std::size_t value = 0;
};

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
                         std::vector<RuntimeOutputToken> outputVocabulary,
                         const std::filesystem::path& artifactPath = {});
    ~GruRuntimeStateModel() override;
    GruRuntimeStateModel(GruRuntimeStateModel&&) noexcept;
    GruRuntimeStateModel& operator=(GruRuntimeStateModel&&) noexcept;
    GruRuntimeStateModel(const GruRuntimeStateModel&) = delete;
    GruRuntimeStateModel& operator=(const GruRuntimeStateModel&) = delete;

    std::shared_ptr<void> createExecutionState() override;
    static GruRuntimeStateModel loadVersioned(Configuration configuration,
                                              std::vector<RuntimeOutputToken> outputVocabulary,
                                              const std::filesystem::path& artifactDirectory);
    Value evaluate(const RuntimeOperation& operation, std::span<const Value> inputs,
                   RuntimeContext& context) override;
    double trainTeacherForced(const RuntimeTrainingRecord& record,
                              std::size_t targetToken, double learningRate);
    void saveArtifact(const std::filesystem::path& artifactPath) const;

private:
    class Implementation;
    Configuration configuration_;
    std::vector<RuntimeOutputToken> outputVocabulary_;
    std::unique_ptr<Implementation> implementation_;
};

} // namespace Felidae
