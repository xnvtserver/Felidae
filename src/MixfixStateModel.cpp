#include "MixfixStateModel.h"

#include "form/BinaryIr.h"

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
    std::unique_ptr<torch::optim::Adam> optimizer;
    double optimizerLearningRate = 0.0;

    void prepareOptimizer(double learningRate) {
        if (!optimizer || optimizerLearningRate != learningRate) {
            optimizer = std::make_unique<torch::optim::Adam>(
                network->parameters(), torch::optim::AdamOptions(learningRate));
            optimizerLearningRate = learningRate;
        }
    }
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
    implementation_->prepareOptimizer(learningRate);
    implementation_->optimizer->zero_grad();
    const auto encoded = implementation_->network->encoder->forward(
        implementation_->network->embedding->forward(encoderInput));
    const auto decoded = implementation_->network->decoder->forward(
        implementation_->network->decoderEmbedding->forward(decoderInput), std::get<1>(encoded));
    const auto logits = implementation_->network->projection->forward(
        std::get<0>(decoded).squeeze(1));
    const auto loss = torch::nn::functional::cross_entropy(logits, target);
    loss.backward();
    implementation_->optimizer->step();
    implementation_->network->eval();
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

} // namespace Felidae
