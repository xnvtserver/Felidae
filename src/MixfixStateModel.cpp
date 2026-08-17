#include "MixfixStateModel.h"

#include "form/BinaryIr.h"
#include "form/RuntimeTraining.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

#ifdef FELIDAE_HAS_TORCH
#include <torch/torch.h>
#endif

namespace Felidae {
namespace {

std::uint64_t fnv1a(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw IrError("cannot read mixfix GRU artifact for integrity validation");
    std::uint64_t hash = 14695981039346656037ull;
    char byte = 0;
    while (input.get(byte)) {
        hash ^= static_cast<unsigned char>(byte);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string manifestValue(const std::filesystem::path& manifestPath, const std::string& wanted) {
    std::ifstream manifest(manifestPath);
    if (!manifest) throw IrError("model manifest is unavailable: " + manifestPath.string());
    std::string line;
    while (std::getline(manifest, line)) {
        const auto separator = line.find('=');
        if (separator != std::string::npos && line.substr(0, separator) == wanted) {
            return line.substr(separator + 1);
        }
    }
    throw IrError("model manifest omits " + wanted);
}

IrWord resolveReference(IrWord index, const std::vector<IrWord>& table,
                        const char* name) {
    if (index >= table.size()) {
        throw IrError(std::string("mixfix GRU emitted invalid ") + name + " reference");
    }
    return table[index];
}

} // namespace

std::vector<IrWord> resolveMixfixIrTokens(
    std::span<const MixfixIrToken> tokens, const MixfixContext& context) {
    if (context.maximumOutputWords == 0) throw IrError("mixfix IR output limit is zero");
    std::vector<IrWord> words;
    words.reserve(std::min(tokens.size(), context.maximumOutputWords));
    for (const auto& token : tokens) {
        if (token.kind == MixfixIrTokenKind::End) {
            words.push_back(static_cast<IrWord>(IrOpcode::End));
            return words;
        }
        if (words.size() >= context.maximumOutputWords) {
            throw IrError("mixfix GRU exceeded its bounded decoder output");
        }
        switch (token.kind) {
        case MixfixIrTokenKind::Opcode:
            if (token.value >= kIrOpcodeCount) {
                throw IrError("mixfix GRU emitted invalid IR opcode");
            }
            words.push_back(token.value); break;
        case MixfixIrTokenKind::Register: words.push_back(token.value); break;
        case MixfixIrTokenKind::ConstantReference:
            words.push_back(resolveReference(token.value, context.constantReferences, "constant")); break;
        case MixfixIrTokenKind::SymbolReference:
            words.push_back(resolveReference(token.value, context.symbolReferences, "symbol")); break;
        case MixfixIrTokenKind::FactReference:
            words.push_back(resolveReference(token.value, context.factReferences, "fact")); break;
        case MixfixIrTokenKind::ProgramReference:
            words.push_back(resolveReference(token.value, context.programReferences, "program")); break;
        case MixfixIrTokenKind::End: break;
        }
    }
    throw IrError("mixfix GRU decoder did not emit IR_END");
}

std::vector<IrWord> resolveMixfixVocabularyIds(
    std::span<const MixfixVocabularyId> ids, const MixfixContext& context) {
    if (ids.size() > context.maximumOutputWords) {
        throw IrError("mixfix GRU exceeded its bounded decoder output");
    }
    std::vector<MixfixIrToken> tokens;
    tokens.reserve(ids.size());
    for (const auto id : ids) {
        if (id >= context.outputVocabulary.size()) {
            throw IrError("mixfix GRU emitted a token outside the finite output vocabulary");
        }
        tokens.push_back(context.outputVocabulary[id]);
    }
    return resolveMixfixIrTokens(tokens, context);
}

FelidaeIr compileVerifiedMixfixIr(MixfixStateModel& model,
                                  std::span<const SentencePieceId> input,
                                  const MixfixContext& context,
                                  FelidaeIr irShell) {
    irShell.words = resolveMixfixVocabularyIds(model.transform(input, context), context);
    if (irShell.words.size() > context.maximumOutputWords) {
        throw IrError("mixfix model exceeded its bounded IR output");
    }
    IrVerifier::verify(irShell);
    return irShell;
}

class GruMixfixStateModel::Implementation {
public:
#ifdef FELIDAE_HAS_TORCH
    struct Network final : torch::nn::Module {
        explicit Network(const Configuration& c)
        {
            embedding = register_module("embedding", torch::nn::Embedding(c.inputVocabularySize, c.embeddingSize));
            decoderEmbedding = register_module("decoder_embedding", torch::nn::Embedding(c.outputVocabularySize, c.embeddingSize));
            encoder = register_module("encoder", torch::nn::GRU(
                torch::nn::GRUOptions(c.embeddingSize, c.hiddenSize).num_layers(c.layerCount)));
            decoder = register_module("decoder", torch::nn::GRU(
                torch::nn::GRUOptions(c.embeddingSize, c.hiddenSize).num_layers(c.layerCount)));
            projection = register_module("projection", torch::nn::Linear(c.hiddenSize, c.outputVocabularySize));
        }
        torch::nn::Embedding embedding{nullptr};
        torch::nn::Embedding decoderEmbedding{nullptr};
        torch::nn::GRU encoder{nullptr};
        torch::nn::GRU decoder{nullptr};
        torch::nn::Linear projection{nullptr};
    };

    Implementation(const Configuration& c, const std::filesystem::path& artifact) : network(std::make_shared<Network>(c)) {
        if (c.inputVocabularySize <= 0 || c.outputVocabularySize <= 0 || c.embeddingSize <= 0 ||
            c.hiddenSize <= 0 || c.layerCount <= 0 || c.beginToken < 0 || c.beginToken >= c.outputVocabularySize) {
            throw IrError("mixfix GRU configuration is invalid");
        }
        if (artifact.empty() && c.allowRandomInitialization) {
            network->eval();
            return;
        }
        if (artifact.empty() || !std::filesystem::is_regular_file(artifact)) {
            throw IrError("mixfix GRU artifact is unavailable: " + artifact.string());
        }
        torch::load(network, artifact.string());
        network->eval();
    }
    std::shared_ptr<Network> network;
#else
    Implementation(const Configuration&, const std::filesystem::path&) {
        throw IrError("mixfix GRU backend requires FELIDAE_ENABLE_LIBTORCH=ON");
    }
#endif
};

GruMixfixStateModel::GruMixfixStateModel(Configuration c, const std::filesystem::path& artifact)
    : configuration_(c), implementation_(std::make_unique<Implementation>(configuration_, artifact)) {}
GruMixfixStateModel::~GruMixfixStateModel() = default;
GruMixfixStateModel::GruMixfixStateModel(GruMixfixStateModel&&) noexcept = default;
GruMixfixStateModel& GruMixfixStateModel::operator=(GruMixfixStateModel&&) noexcept = default;

GruMixfixStateModel GruMixfixStateModel::loadVersioned(
    Configuration configuration, const std::filesystem::path& artifactDirectory,
    std::string_view expectedSentencePieceHash, std::string_view expectedIrVocabularyVersion) {
    const auto manifest = artifactDirectory / "manifest.txt";
    if (manifestValue(manifest, "backend") != "libtorch-gru") {
        throw IrError("mixfix model manifest backend is incompatible");
    }
    if (manifestValue(manifest, "ir_vocabulary_version") != expectedIrVocabularyVersion) {
        throw IrError("mixfix model IR vocabulary version is incompatible");
    }
    if (!expectedSentencePieceHash.empty() &&
        manifestValue(manifest, "sentencepiece_model_hash") != expectedSentencePieceHash) {
        throw IrError("mixfix model SentencePiece vocabulary is incompatible");
    }
    if (std::stoll(manifestValue(manifest, "input_vocabulary")) != configuration.inputVocabularySize ||
        std::stoll(manifestValue(manifest, "output_vocabulary")) != configuration.outputVocabularySize ||
        std::stoll(manifestValue(manifest, "begin_token")) != configuration.beginToken) {
        throw IrError("mixfix model vocabulary configuration is incompatible");
    }
    const auto artifact = artifactDirectory / "mixfix-gru.pt";
    std::ostringstream expected;
    expected << "fnv1a64:" << std::hex << fnv1a(artifact);
    if (manifestValue(manifest, "artifact_hash") != expected.str()) {
        throw IrError("mixfix model artifact integrity check failed");
    }
    return GruMixfixStateModel(std::move(configuration), artifact);
}

std::vector<MixfixVocabularyId> GruMixfixStateModel::transform(
    std::span<const SentencePieceId> input, const MixfixContext& context) {
    if (input.empty()) throw IrError("mixfix GRU input is empty");
    if (context.outputVocabulary.size() != static_cast<std::size_t>(configuration_.outputVocabularySize)) {
        throw IrError("mixfix GRU output vocabulary does not match model artifact");
    }
#ifdef FELIDAE_HAS_TORCH
    std::vector<std::int64_t> ids;
    ids.reserve(input.size());
    for (const auto id : input) {
        if (id > static_cast<SentencePieceId>(std::numeric_limits<std::int64_t>::max()) || static_cast<std::int64_t>(id) >= configuration_.inputVocabularySize) {
            throw IrError("mixfix GRU input SentencePiece ID is outside model vocabulary");
        }
        ids.push_back(static_cast<std::int64_t>(id));
    }
    torch::NoGradGuard guard;
    auto inputIds = torch::tensor(ids, torch::TensorOptions().dtype(torch::kInt64)).reshape({-1, 1});
    auto encoderResult = implementation_->network->encoder->forward(implementation_->network->embedding->forward(inputIds));
    auto state = std::get<1>(encoderResult);
    auto decoderInput = torch::full({1, 1}, configuration_.beginToken, torch::TensorOptions().dtype(torch::kInt64));
    std::vector<MixfixVocabularyId> tokens;
    const auto limit = std::min(configuration_.maximumDecodeSteps, context.maximumOutputWords);
    for (std::size_t step = 0; step < limit; ++step) {
        auto decoderResult = implementation_->network->decoder->forward(implementation_->network->decoderEmbedding->forward(decoderInput), state);
        state = std::get<1>(decoderResult);
        const auto tokenId = implementation_->network->projection->forward(std::get<0>(decoderResult).select(0, 0).select(0, 0)).argmax().item<std::int64_t>();
        if (tokenId < 0 || static_cast<std::size_t>(tokenId) >= context.outputVocabulary.size()) {
            throw IrError("mixfix GRU emitted token outside finite vocabulary");
        }
        const auto vocabularyId = static_cast<MixfixVocabularyId>(tokenId);
        tokens.push_back(vocabularyId);
        if (context.outputVocabulary[vocabularyId].kind == MixfixIrTokenKind::End) break;
        decoderInput.fill_(tokenId);
    }
    return tokens;
#else
    (void)input; (void)context;
    throw IrError("mixfix GRU backend requires FELIDAE_ENABLE_LIBTORCH=ON");
#endif
}

double GruMixfixStateModel::trainTeacherForced(
    std::span<const SentencePieceId> input,
    std::span<const std::int64_t> targetTokenIds,
    double learningRate) {
    if (input.empty() || targetTokenIds.empty() || learningRate <= 0.0) {
        throw IrError("mixfix GRU teacher-forcing batch is invalid");
    }
#ifdef FELIDAE_HAS_TORCH
    std::vector<std::int64_t> encoderIds;
    encoderIds.reserve(input.size());
    for (const auto id : input) {
        if (id > static_cast<SentencePieceId>(std::numeric_limits<std::int64_t>::max()) ||
            static_cast<std::int64_t>(id) >= configuration_.inputVocabularySize) {
            throw IrError("teacher-forcing SentencePiece ID is outside model vocabulary");
        }
        encoderIds.push_back(static_cast<std::int64_t>(id));
    }
    std::vector<std::int64_t> decoderIds;
    decoderIds.reserve(targetTokenIds.size());
    decoderIds.push_back(configuration_.beginToken);
    for (std::size_t index = 1; index < targetTokenIds.size(); ++index) {
        decoderIds.push_back(targetTokenIds[index - 1]);
    }
    for (const auto id : targetTokenIds) {
        if (id < 0 || id >= configuration_.outputVocabularySize) {
            throw IrError("teacher-forcing IR token is outside finite vocabulary");
        }
    }
    auto encoderInput = torch::tensor(encoderIds, torch::TensorOptions().dtype(torch::kInt64)).reshape({-1, 1});
    auto target = torch::tensor(std::vector<std::int64_t>(targetTokenIds.begin(), targetTokenIds.end()),
                                torch::TensorOptions().dtype(torch::kInt64));
    auto decoderInput = torch::tensor(decoderIds, torch::TensorOptions().dtype(torch::kInt64)).reshape({-1, 1});
    implementation_->network->train();
    torch::optim::Adam optimizer(implementation_->network->parameters(),
                                 torch::optim::AdamOptions(learningRate));
    optimizer.zero_grad();
    const auto encoded = implementation_->network->encoder->forward(
        implementation_->network->embedding->forward(encoderInput));
    const auto decoded = implementation_->network->decoder->forward(
        implementation_->network->decoderEmbedding->forward(decoderInput), std::get<1>(encoded));
    const auto logits = implementation_->network->projection->forward(
        std::get<0>(decoded).squeeze(1));
    const auto loss = torch::nn::functional::cross_entropy(logits, target);
    loss.backward();
    optimizer.step();
    return loss.item<double>();
#else
    (void)input; (void)targetTokenIds; (void)learningRate;
    throw IrError("mixfix GRU training requires FELIDAE_ENABLE_LIBTORCH=ON");
#endif
}

void GruMixfixStateModel::saveArtifact(const std::filesystem::path& artifactPath) const {
#ifdef FELIDAE_HAS_TORCH
    if (artifactPath.empty()) throw IrError("mixfix GRU artifact path is empty");
    const auto parent = artifactPath.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    torch::save(implementation_->network, artifactPath.string());
#else
    (void)artifactPath;
    throw IrError("mixfix GRU export requires FELIDAE_ENABLE_LIBTORCH=ON");
#endif
}

// Runtime recurrent execution belongs to src/form/RuntimeStateModel.cpp.
// Keep this excluded migration copy only until downstream out-of-tree users
// have rebuilt; no Felidae target compiles or links it.
#if 0
class GruRuntimeStateModel::Implementation {
public:
#ifdef FELIDAE_HAS_TORCH
    struct Network final : torch::nn::Module {
        explicit Network(const Configuration& configuration) {
            embedding = register_module("embedding", torch::nn::Embedding(
                configuration.inputVocabularySize, configuration.embeddingSize));
            recurrent = register_module("recurrent", torch::nn::GRU(torch::nn::GRUOptions(
                configuration.embeddingSize, configuration.hiddenSize).num_layers(configuration.layerCount)));
            projection = register_module("projection", torch::nn::Linear(
                configuration.hiddenSize, configuration.outputVocabularySize));
        }
        torch::nn::Embedding embedding{nullptr};
        torch::nn::GRU recurrent{nullptr};
        torch::nn::Linear projection{nullptr};
    };

    struct ExecutionState {
        torch::Tensor hidden;
    };

    explicit Implementation(const Configuration& configuration, const std::filesystem::path& artifact)
        : network(std::make_shared<Network>(configuration)) {
        if (artifact.empty() && configuration.allowRandomInitialization) {
            network->eval();
            return;
        }
        if (artifact.empty() || !std::filesystem::is_regular_file(artifact)) {
            throw IrError("runtime GRU artifact is unavailable: " + artifact.string());
        }
        torch::load(network, artifact.string());
        network->eval();
    }
    std::shared_ptr<Network> network;
#else
    explicit Implementation(const Configuration&, const std::filesystem::path&) {
        throw IrError("runtime GRU backend requires FELIDAE_ENABLE_LIBTORCH=ON");
    }
#endif
};

GruRuntimeStateModel::GruRuntimeStateModel(Configuration configuration,
                                           std::vector<RuntimeOutputToken> outputVocabulary,
                                           const std::filesystem::path& artifactPath)
    : configuration_(configuration), outputVocabulary_(std::move(outputVocabulary)),
      implementation_(std::make_unique<Implementation>(configuration_, artifactPath)) {
    if (configuration_.inputVocabularySize <= 8 || configuration_.outputVocabularySize <= 0 ||
        configuration_.embeddingSize <= 0 || configuration_.hiddenSize <= 0 ||
        configuration_.layerCount <= 0 ||
        outputVocabulary_.size() != static_cast<std::size_t>(configuration_.outputVocabularySize)) {
        throw IrError("runtime GRU configuration or finite output vocabulary is invalid");
    }
    for (const auto& token : outputVocabulary_) {
        if (token.kind == RuntimeOutputTokenKind::Boolean && token.value > 1) {
            throw IrError("runtime GRU boolean output token is invalid");
        }
    }
}

GruRuntimeStateModel::~GruRuntimeStateModel() = default;
GruRuntimeStateModel::GruRuntimeStateModel(GruRuntimeStateModel&&) noexcept = default;
GruRuntimeStateModel& GruRuntimeStateModel::operator=(GruRuntimeStateModel&&) noexcept = default;

GruRuntimeStateModel GruRuntimeStateModel::loadVersioned(
    Configuration configuration, std::vector<RuntimeOutputToken> outputVocabulary,
    const std::filesystem::path& artifactDirectory) {
    const auto manifest = artifactDirectory / "runtime-manifest.txt";
    const auto expect = [&](const char* key, const char* value) {
        if (manifestValue(manifest, key) != value) {
            throw IrError(std::string("runtime SSM manifest is incompatible: ") + key);
        }
    };
    expect("model_version", "runtime-gru-v1");
    expect("backend", "libtorch-gru");
    expect("opcode_vocabulary_version", "felidae-form-opcode-v1");
    expect("symbol_encoding", "felidae-fnv1a63-v1");
    expect("architecture", "gru-runtime-v1");
    expect("trace_schema", "felidae-runtime-trace-v1");
    if (std::stoull(manifestValue(manifest, "ir_binary_version")) != kBinaryIrVersion ||
        std::stoll(manifestValue(manifest, "input_vocabulary")) != configuration.inputVocabularySize ||
        std::stoll(manifestValue(manifest, "output_vocabulary")) != configuration.outputVocabularySize ||
        std::stoll(manifestValue(manifest, "embedding_size")) != configuration.embeddingSize ||
        std::stoll(manifestValue(manifest, "hidden_size")) != configuration.hiddenSize ||
        std::stoll(manifestValue(manifest, "layer_count")) != configuration.layerCount) {
        throw IrError("runtime SSM manifest configuration is incompatible");
    }
    const auto artifact = artifactDirectory / "runtime-gru.pt";
    std::ostringstream expected;
    expected << "fnv1a64:" << std::hex << fnv1a(artifact);
    if (manifestValue(manifest, "artifact_hash") != expected.str()) {
        throw IrError("runtime SSM artifact integrity check failed");
    }
    return GruRuntimeStateModel(std::move(configuration), std::move(outputVocabulary), artifact);
}

std::shared_ptr<void> GruRuntimeStateModel::createExecutionState() {
#ifdef FELIDAE_HAS_TORCH
    return std::make_shared<Implementation::ExecutionState>();
#else
    throw IrError("runtime GRU backend requires FELIDAE_ENABLE_LIBTORCH=ON");
#endif
}

Value GruRuntimeStateModel::evaluate(const RuntimeOperation& operation,
                                     std::span<const Value> inputs,
                                     RuntimeContext& context) {
#ifdef FELIDAE_HAS_TORCH
    // IDs are feature categories, not serialized values.  The finite decoder
    // can only return a controlled input reference, nil, or an explicit bool.
    const auto category = [](const Value& value) -> std::int64_t {
        if (std::holds_alternative<VmNil>(value)) return 1;
        if (std::holds_alternative<bool>(value)) return 2;
        if (std::holds_alternative<double>(value)) return 3;
        if (std::holds_alternative<VmText>(value)) return 4;
        if (std::holds_alternative<VmArrayPtr>(value)) return 5;
        if (std::holds_alternative<VmMapPtr>(value)) return 6;
        return 7; // VmFactPtr
    };
    if (!context.executionState) {
        throw IrError("runtime GRU evaluation has no execution-local recurrent state");
    }
    auto state = std::static_pointer_cast<Implementation::ExecutionState>(context.executionState);
    const auto operationId = static_cast<std::int64_t>(operation.symbol %
        static_cast<IrSymbolRef>(configuration_.inputVocabularySize - 8)) + 8;
    std::vector<std::int64_t> ids;
    ids.reserve(inputs.size() + 1);
    ids.push_back(operationId);
    for (const auto& input : inputs) ids.push_back(category(input));
    torch::NoGradGuard guard;
    const auto tensor = torch::tensor(ids, torch::TensorOptions().dtype(torch::kInt64)).reshape({-1, 1});
    auto result = state->hidden.defined()
        ? implementation_->network->recurrent->forward(implementation_->network->embedding->forward(tensor), state->hidden)
        : implementation_->network->recurrent->forward(implementation_->network->embedding->forward(tensor));
    state->hidden = std::get<1>(result).detach();
    const auto outputId = implementation_->network->projection->forward(
        std::get<0>(result).select(0, std::get<0>(result).size(0) - 1).select(0, 0)).argmax().item<std::int64_t>();
    if (outputId < 0 || static_cast<std::size_t>(outputId) >= outputVocabulary_.size()) {
        throw IrError("runtime GRU emitted a token outside its finite output vocabulary");
    }
    const auto& token = outputVocabulary_[static_cast<std::size_t>(outputId)];
    switch (token.kind) {
    case RuntimeOutputTokenKind::InputReference:
        if (token.value >= inputs.size()) throw IrError("runtime GRU emitted an unavailable input reference");
        return inputs[token.value];
    case RuntimeOutputTokenKind::FactFromInput: {
        if (token.value >= inputs.size()) throw IrError("runtime GRU emitted an unavailable fact input reference");
        const auto fact = std::get_if<VmFactPtr>(&inputs[token.value]);
        if (!fact || !*fact) throw IrError("runtime GRU fact action requires a fact input");
        auto inferred = std::make_shared<VmFact>(**fact);
        inferred->id = 0;
        return inferred;
    }
    case RuntimeOutputTokenKind::DegreeMilli:
        if (token.value > 1000) throw IrError("runtime GRU emitted an invalid degree token");
        return VmDegree(static_cast<double>(token.value) / 1000.0);
    case RuntimeOutputTokenKind::Nil: return VmNil{};
    case RuntimeOutputTokenKind::Boolean: return token.value != 0;
    }
    throw IrError("runtime GRU emitted an invalid output token");
#else
    (void)operation; (void)inputs; (void)context;
    throw IrError("runtime GRU backend requires FELIDAE_ENABLE_LIBTORCH=ON");
#endif
}

double GruRuntimeStateModel::trainTeacherForced(const RuntimeTrainingRecord& record,
                                                std::size_t targetToken,
                                                double learningRate) {
#ifdef FELIDAE_HAS_TORCH
    if (targetToken >= outputVocabulary_.size() || learningRate <= 0.0) {
        throw IrError("runtime GRU training target or learning rate is invalid");
    }
    // Training features are derived from observed state, not raw bytecode:
    // module identity, query/result category, fact types, and VM trace kinds.
    const auto bounded = [&](std::uint64_t value) -> std::int64_t {
        return static_cast<std::int64_t>(value % static_cast<std::uint64_t>(configuration_.inputVocabularySize - 8)) + 8;
    };
    std::vector<std::int64_t> ids{bounded(record.moduleEntry),
                                  static_cast<std::int64_t>(record.inputKind % 8),
                                  static_cast<std::int64_t>(record.resultKind % 8)};
    for (const auto type : record.factTypes) ids.push_back(bounded(type));
    for (const auto& event : record.trace) ids.push_back(bounded(static_cast<std::uint64_t>(event.kind) + event.symbol));
    if (ids.size() > 4'096) throw IrError("runtime GRU training record is too large");
    implementation_->network->zero_grad();
    const auto input = torch::tensor(ids, torch::TensorOptions().dtype(torch::kInt64)).reshape({-1, 1});
    const auto recurrent = implementation_->network->recurrent->forward(
        implementation_->network->embedding->forward(input));
    const auto logits = implementation_->network->projection->forward(
        std::get<0>(recurrent).select(0, std::get<0>(recurrent).size(0) - 1).select(0, 0));
    const auto target = torch::tensor({static_cast<std::int64_t>(targetToken)}, torch::TensorOptions().dtype(torch::kInt64));
    const auto loss = torch::nn::functional::cross_entropy(logits.unsqueeze(0), target);
    loss.backward();
    torch::NoGradGuard guard;
    for (auto& parameter : implementation_->network->parameters()) {
        if (parameter.grad().defined()) parameter.add_(parameter.grad(), -learningRate);
    }
    implementation_->network->eval();
    return loss.item<double>();
#else
    (void)record; (void)targetToken; (void)learningRate;
    throw IrError("runtime GRU training requires FELIDAE_ENABLE_LIBTORCH=ON");
#endif
}

void GruRuntimeStateModel::saveArtifact(const std::filesystem::path& artifactPath) const {
#ifdef FELIDAE_HAS_TORCH
    if (artifactPath.empty()) throw IrError("runtime GRU artifact path is empty");
    const auto parent = artifactPath.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    torch::save(implementation_->network, artifactPath.string());
    const auto manifestPath = artifactPath.parent_path() / "runtime-manifest.txt";
    std::ofstream manifest(manifestPath, std::ios::trunc);
    if (!manifest) throw IrError("cannot write runtime SSM manifest");
    manifest << "model_version=runtime-gru-v1\n"
             << "backend=libtorch-gru\n"
             << "ir_binary_version=" << kBinaryIrVersion << "\n"
             << "opcode_vocabulary_version=felidae-form-opcode-v1\n"
             << "symbol_encoding=felidae-fnv1a63-v1\n"
             << "architecture=gru-runtime-v1\n"
             << "trace_schema=felidae-runtime-trace-v1\n"
             << "input_vocabulary=" << configuration_.inputVocabularySize << "\n"
             << "output_vocabulary=" << configuration_.outputVocabularySize << "\n"
             << "embedding_size=" << configuration_.embeddingSize << "\n"
             << "hidden_size=" << configuration_.hiddenSize << "\n"
             << "layer_count=" << configuration_.layerCount << "\n"
             << "artifact_hash=fnv1a64:" << std::hex << fnv1a(artifactPath) << "\n";
#else
    (void)artifactPath;
    throw IrError("runtime GRU export requires FELIDAE_ENABLE_LIBTORCH=ON");
#endif
}

#endif
} // namespace Felidae
