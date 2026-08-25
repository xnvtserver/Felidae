#include "RuntimeStateModel.h"

#include "BinaryIsa.h"
#include "RuntimeTraining.h"

#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>

#ifdef FELIDAE_HAS_TORCH
#include <torch/csrc/jit/api/module.h>
#include <torch/csrc/jit/serialization/import.h>
#include <torch/torch.h>
#endif

namespace Felidae {
namespace {
std::uint64_t fnv1a(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw IrError("cannot read runtime GRU artifact for integrity validation");
    std::uint64_t hash = 14695981039346656037ull;
    char byte = 0;
    while (input.get(byte)) { hash ^= static_cast<unsigned char>(byte); hash *= 1099511628211ull; }
    return hash;
}
std::string manifestValue(const std::filesystem::path& manifestPath, const std::string& wanted) {
    std::ifstream manifest(manifestPath);
    if (!manifest) throw IrError("model manifest is unavailable: " + manifestPath.string());
    std::string line;
    while (std::getline(manifest, line)) {
        const auto separator = line.find('=');
        if (separator != std::string::npos && line.substr(0, separator) == wanted) return line.substr(separator + 1);
    }
    throw IrError("model manifest omits " + wanted);
}

std::int64_t runtimeInputToken(IrSymbolRef operation, RuntimeValueKind kind,
                               std::int64_t vocabularySize) {
    constexpr std::int64_t kReservedTokens = 16;
    if (vocabularySize <= kReservedTokens) throw IrError("runtime GRU input vocabulary is invalid");
    if (kind < RuntimeValueKind::Nil || kind > RuntimeValueKind::Symbol) {
        throw IrError("runtime GRU input value kind is invalid");
    }
    // Operation IDs occupy the model's dynamic range; stable value-kind IDs
    // remain literal 1..8. This exact rule is shared by training/inference.
    return static_cast<std::int64_t>(operation % static_cast<IrSymbolRef>(vocabularySize - kReservedTokens)) + kReservedTokens;
}

std::vector<std::int64_t> runtimeInputIds(
    std::uint16_t operation, std::span<const RuntimeValueKind> inputKinds,
    std::span<const IrSymbolRef> factTypes,
    std::span<const std::pair<IrSymbolRef, std::uint32_t>> factTypeCounts,
    std::span<const std::pair<IrSymbolRef, IrSymbolRef>> hierarchyEdges,
    std::int64_t vocabularySize) {
    constexpr std::size_t kMaximumContextItems = 1'024;
    constexpr std::int64_t kFactTypesMarker = 9;
    constexpr std::int64_t kHierarchyMarker = 10;
    constexpr std::int64_t kInputsMarker = 11;
    if (!isKnownSemanticOperation(operation) ||
        !semanticOperationAcceptsArity(operation, inputKinds.size()) ||
        inputKinds.size() > kMaximumContextItems ||
        factTypes.size() > kMaximumContextItems || factTypeCounts.size() > kMaximumContextItems ||
        hierarchyEdges.size() > kMaximumContextItems) {
        throw IrError("runtime GRU operation context is invalid or too large");
    }
    const auto inputKind = inputKinds.front();
    switch (static_cast<SemanticOperationId>(operation)) {
    case SemanticOperationId::Identity:
        break;
    case SemanticOperationId::SelectFact:
    case SemanticOperationId::DeriveFact:
        if (inputKind != RuntimeValueKind::Fact) {
            throw IrError("runtime GRU fact operation requires a fact input");
        }
        break;
    case SemanticOperationId::EvaluateDegree:
        if (inputKind != RuntimeValueKind::Number &&
            inputKind != RuntimeValueKind::Degree) {
            throw IrError("runtime GRU degree operation requires a numeric input");
        }
        break;
    }
    std::vector<std::int64_t> ids;
    ids.reserve(2 + inputKinds.size() + factTypes.size() + 2 * factTypeCounts.size() + 2 * hierarchyEdges.size());
    ids.push_back(runtimeInputToken(operation, RuntimeValueKind::Nil, vocabularySize));
    ids.push_back(kFactTypesMarker);
    for (const auto type : factTypes) {
        if (type == 0) throw IrError("runtime GRU fact type context is invalid");
        ids.push_back(runtimeInputToken(type, RuntimeValueKind::Nil, vocabularySize));
    }
    // Counts distinguish current fact population while keeping a fixed
    // integer vocabulary: one, two-to-four, and five-or-more facts.
    constexpr std::int64_t kFactCountsMarker = 12;
    constexpr std::int64_t kOneFactMarker = 13;
    constexpr std::int64_t kSeveralFactsMarker = 14;
    constexpr std::int64_t kManyFactsMarker = 15;
    ids.push_back(kFactCountsMarker);
    for (const auto& [type, count] : factTypeCounts) {
        if (type == 0) throw IrError("runtime GRU fact-count context is invalid");
        ids.push_back(runtimeInputToken(type, RuntimeValueKind::Nil, vocabularySize));
        ids.push_back(count == 1 ? kOneFactMarker : count < 5 ? kSeveralFactsMarker : kManyFactsMarker);
    }
    ids.push_back(kHierarchyMarker);
    for (const auto& [child, parent] : hierarchyEdges) {
        if (child == 0 || parent == 0) throw IrError("runtime GRU hierarchy context is invalid");
        ids.push_back(runtimeInputToken(child, RuntimeValueKind::Nil, vocabularySize));
        ids.push_back(runtimeInputToken(parent, RuntimeValueKind::Nil, vocabularySize));
    }
    ids.push_back(kInputsMarker);
    for (const auto kind : inputKinds) {
        if (kind < RuntimeValueKind::Nil || kind > RuntimeValueKind::Symbol) {
            throw IrError("runtime GRU input value kind is invalid");
        }
        ids.push_back(static_cast<std::int64_t>(kind));
    }
    return ids;
}
} // namespace

std::vector<RuntimeOutputToken> defaultRuntimeOutputVocabulary() {
    std::vector<RuntimeOutputToken> vocabulary;
    vocabulary.reserve(3 + 2 * kRuntimeModelReferenceLimit + 5);
    vocabulary.push_back({RuntimeOutputTokenKind::Nil, 0});
    vocabulary.push_back({RuntimeOutputTokenKind::NumericTruth, 0});
    vocabulary.push_back({RuntimeOutputTokenKind::NumericTruth, 1});
    for (std::size_t index = 0; index < kRuntimeModelReferenceLimit; ++index) {
        vocabulary.push_back({RuntimeOutputTokenKind::InputReference, index});
    }
    for (std::size_t index = 0; index < kRuntimeModelReferenceLimit; ++index) {
        vocabulary.push_back({RuntimeOutputTokenKind::FactFromInput, index});
    }
    // A compact degree lattice is intentionally explicit in the model output
    // vocabulary. Exact numerical similarity/membership remains deterministic
    // VM work; learned operations never masquerade as arbitrary arithmetic.
    for (const std::size_t milli : {0u, 250u, 500u, 750u, 1000u}) {
        vocabulary.push_back({RuntimeOutputTokenKind::DegreeMilli, milli});
    }
    return vocabulary;
}

class GruRuntimeStateModel::Implementation {
public:
#ifdef FELIDAE_HAS_TORCH
    struct Network final : torch::nn::Module {
        explicit Network(const Configuration& c) {
            embedding = register_module("embedding", torch::nn::Embedding(c.inputVocabularySize, c.embeddingSize));
            recurrent = register_module("recurrent", torch::nn::GRU(torch::nn::GRUOptions(c.embeddingSize, c.hiddenSize).num_layers(c.layerCount)));
            projection = register_module("projection", torch::nn::Linear(c.hiddenSize, c.outputVocabularySize));
        }
        torch::nn::Embedding embedding{nullptr}; torch::nn::GRU recurrent{nullptr}; torch::nn::Linear projection{nullptr};
    };
    struct ExecutionState { torch::Tensor hidden; };
    explicit Implementation(const Configuration& c, const std::filesystem::path& artifact) {
        if (artifact.empty() && c.allowRandomInitialization) { network=std::make_shared<Network>(c);network->eval();return; }
        if (artifact.empty() || !std::filesystem::is_regular_file(artifact)) throw IrError("runtime GRU artifact is unavailable: " + artifact.string());
        try { production=torch::jit::load(artifact.string());production->eval(); }
        catch(const c10::Error& error){throw IrError("runtime production artifact is not valid TorchScript: "+std::string(error.what_without_backtrace()));}
    }
    std::shared_ptr<Network> network;
    std::optional<torch::jit::Module> production;
    std::unique_ptr<torch::optim::Adam> optimizer;
    double optimizerLearningRate = 0.0;

    void prepareOptimizer(double learningRate) {
        if(!network)throw IrError("TorchScript inference artifact cannot be used as a training checkpoint");
        if (!optimizer || optimizerLearningRate != learningRate) {
            optimizer = std::make_unique<torch::optim::Adam>(
                network->parameters(), torch::optim::AdamOptions(learningRate));
            optimizerLearningRate = learningRate;
        }
    }
#else
    explicit Implementation(const Configuration&, const std::filesystem::path&) { throw IrError("runtime GRU backend requires FELIDAE_ENABLE_LIBTORCH=ON"); }
#endif
};

GruRuntimeStateModel::GruRuntimeStateModel(Configuration c, std::vector<RuntimeOutputToken> vocabulary,
                                           const std::filesystem::path& artifact)
    : configuration_(c), outputVocabulary_(std::move(vocabulary)), implementation_(std::make_unique<Implementation>(configuration_, artifact)) {
    if (configuration_.inputVocabularySize <= 16 || configuration_.outputVocabularySize <= 0 || configuration_.embeddingSize <= 0 ||
        configuration_.hiddenSize <= 0 || configuration_.layerCount <= 0 || outputVocabulary_.size() != static_cast<std::size_t>(configuration_.outputVocabularySize))
        throw IrError("runtime GRU configuration or finite output vocabulary is invalid");
    for (const auto& token : outputVocabulary_) if ((token.kind == RuntimeOutputTokenKind::NumericTruth && token.value > 1) ||
        (token.kind == RuntimeOutputTokenKind::DegreeMilli && token.value > 1000)) throw IrError("runtime GRU output token is invalid");
}
GruRuntimeStateModel::~GruRuntimeStateModel() = default;
GruRuntimeStateModel::GruRuntimeStateModel(GruRuntimeStateModel&&) noexcept = default;
GruRuntimeStateModel& GruRuntimeStateModel::operator=(GruRuntimeStateModel&&) noexcept = default;

GruRuntimeStateModel GruRuntimeStateModel::loadVersioned(Configuration c, std::vector<RuntimeOutputToken> vocabulary,
                                                         const std::filesystem::path& directory) {
    const auto manifest = directory / "runtime-manifest.txt";
    const auto expect = [&](const char* key, const char* value) { if (manifestValue(manifest, key) != value) throw IrError(std::string("runtime SSM manifest is incompatible: ") + key); };
    expect("model_version", "runtime-gru-v1"); expect("backend", "torchscript-gru"); expect("opcode_vocabulary_version", "felidae-isa-v1");
    expect("symbol_encoding", "felidae-fnv1a63-v1"); expect("architecture", "gru-runtime-v1"); expect("training_schema", "felidae-runtime-operation-v7");
    if (std::stoull(manifestValue(manifest, "ir_binary_version")) != kFelidaeBinaryVersion ||
        std::stoll(manifestValue(manifest, "input_vocabulary")) != c.inputVocabularySize || std::stoll(manifestValue(manifest, "output_vocabulary")) != c.outputVocabularySize ||
        std::stoll(manifestValue(manifest, "embedding_size")) != c.embeddingSize || std::stoll(manifestValue(manifest, "hidden_size")) != c.hiddenSize ||
        std::stoll(manifestValue(manifest, "layer_count")) != c.layerCount) throw IrError("runtime SSM manifest configuration is incompatible");
    const auto artifact = directory / "runtime-gru.pt"; std::ostringstream expected; expected << "fnv1a64:" << std::hex << fnv1a(artifact);
    if (manifestValue(manifest, "artifact_hash") != expected.str()) throw IrError("runtime SSM artifact integrity check failed");
    return GruRuntimeStateModel(std::move(c), std::move(vocabulary), artifact);
}

std::shared_ptr<void> GruRuntimeStateModel::createExecutionState() {
#ifdef FELIDAE_HAS_TORCH
    return std::make_shared<Implementation::ExecutionState>();
#else
    throw IrError("runtime GRU backend requires FELIDAE_ENABLE_LIBTORCH=ON");
#endif
}

Value GruRuntimeStateModel::evaluate(const RuntimeOperation& operation, std::span<const Value> inputs, RuntimeContext& context) {
#ifdef FELIDAE_HAS_TORCH
    if (!context.executionState) throw IrError("runtime GRU evaluation has no execution-local recurrent state");
    auto state = std::static_pointer_cast<Implementation::ExecutionState>(context.executionState);
    std::vector<RuntimeValueKind> inputKinds;
    inputKinds.reserve(inputs.size());
    for (const auto& input : inputs) inputKinds.push_back(runtimeValueKind(input));
    const auto ids = runtimeInputIds(operation.id, inputKinds, context.knowledge.factTypes, context.knowledge.factTypeCounts,
                                     context.knowledge.hierarchyEdges, configuration_.inputVocabularySize);
    torch::InferenceMode guard;const auto input=torch::tensor(ids,torch::TensorOptions().dtype(torch::kInt64)).reshape({-1,1});
    torch::Tensor logits;
    if(implementation_->production){if(!state->hidden.defined())state->hidden=torch::zeros({configuration_.layerCount,1,configuration_.hiddenSize});auto output=implementation_->production->forward({input,state->hidden}).toTuple();logits=output->elements().at(0).toTensor();state->hidden=output->elements().at(1).toTensor().detach();}
    else{const auto result=state->hidden.defined()?implementation_->network->recurrent->forward(implementation_->network->embedding->forward(input),state->hidden):implementation_->network->recurrent->forward(implementation_->network->embedding->forward(input));state->hidden=std::get<1>(result).detach();logits=implementation_->network->projection->forward(std::get<0>(result).select(0,std::get<0>(result).size(0)-1).select(0,0));}
    const auto outputId=logits.argmax().item<std::int64_t>();
    if (outputId < 0 || static_cast<std::size_t>(outputId) >= outputVocabulary_.size()) throw IrError("runtime GRU emitted a token outside its finite output vocabulary");
    const auto& token = outputVocabulary_[static_cast<std::size_t>(outputId)];
    switch (token.kind) {
    case RuntimeOutputTokenKind::InputReference: if (token.value >= inputs.size()) throw IrError("runtime GRU emitted an unavailable input reference"); return inputs[token.value];
    case RuntimeOutputTokenKind::FactFromInput: { if (token.value >= inputs.size()) throw IrError("runtime GRU emitted an unavailable fact input reference"); const auto fact = std::get_if<VmFactPtr>(&inputs[token.value]); if (!fact || !*fact) throw IrError("runtime GRU fact action requires a fact input"); auto inferred = std::make_shared<VmFact>(**fact); inferred->id = 0; return inferred; }
    case RuntimeOutputTokenKind::DegreeMilli: return VmDegree(static_cast<double>(token.value) / 1000.0);
    case RuntimeOutputTokenKind::Nil: return VmNil{}; case RuntimeOutputTokenKind::NumericTruth: return token.value != 0 ? 1.0 : 0.0;
    }
    throw IrError("runtime GRU emitted an invalid output token");
#else
    (void)operation; (void)inputs; (void)context; throw IrError("runtime GRU backend requires FELIDAE_ENABLE_LIBTORCH=ON");
#endif
}

double GruRuntimeStateModel::trainTeacherForced(const RuntimeTrainingRecord& record, std::size_t targetToken, double learningRate) {
#ifdef FELIDAE_HAS_TORCH
    verifyRuntimeTrainingRecord(record);
    if (targetToken >= outputVocabulary_.size() || learningRate <= 0.0) throw IrError("runtime GRU training target or learning rate is invalid");
    const auto ids = runtimeInputIds(record.operationId, record.inputKinds, record.factTypes, record.factTypeCounts,
                                     record.hierarchyEdges, configuration_.inputVocabularySize);
    if (ids.size() > 4096) throw IrError("runtime GRU training record is too large");
    implementation_->prepareOptimizer(learningRate);
    implementation_->network->train();
    implementation_->optimizer->zero_grad();
    const auto input = torch::tensor(ids, torch::TensorOptions().dtype(torch::kInt64)).reshape({-1, 1}); const auto recurrent = implementation_->network->recurrent->forward(implementation_->network->embedding->forward(input));
    const auto logits = implementation_->network->projection->forward(std::get<0>(recurrent).select(0, std::get<0>(recurrent).size(0) - 1).select(0, 0));
    const auto target = torch::tensor({static_cast<std::int64_t>(targetToken)}, torch::TensorOptions().dtype(torch::kInt64)); const auto loss = torch::nn::functional::cross_entropy(logits.unsqueeze(0), target); loss.backward();
    implementation_->optimizer->step();
    implementation_->network->eval();
    return loss.item<double>();
#else
    (void)record; (void)targetToken; (void)learningRate; throw IrError("runtime GRU training requires FELIDAE_ENABLE_LIBTORCH=ON");
#endif
}

std::size_t GruRuntimeStateModel::predictTeacherToken(const RuntimeTrainingRecord& record) const {
#ifdef FELIDAE_HAS_TORCH
    verifyRuntimeTrainingRecord(record);
    const auto ids = runtimeInputIds(record.operationId, record.inputKinds, record.factTypes, record.factTypeCounts,
                                     record.hierarchyEdges, configuration_.inputVocabularySize);
    torch::InferenceMode guard;
    const auto input = torch::tensor(ids, torch::TensorOptions().dtype(torch::kInt64)).reshape({-1, 1});
    const auto recurrent = implementation_->network->recurrent->forward(implementation_->network->embedding->forward(input));
    const auto output = implementation_->network->projection->forward(
        std::get<0>(recurrent).select(0, std::get<0>(recurrent).size(0) - 1).select(0, 0)).argmax().item<std::int64_t>();
    if (output < 0 || static_cast<std::size_t>(output) >= outputVocabulary_.size()) {
        throw IrError("runtime GRU validation emitted an invalid token");
    }
    return static_cast<std::size_t>(output);
#else
    (void)record; throw IrError("runtime GRU backend requires FELIDAE_ENABLE_LIBTORCH=ON");
#endif
}

void GruRuntimeStateModel::saveCheckpoint(const std::filesystem::path& artifactPath) const {
#ifdef FELIDAE_HAS_TORCH
    if(!implementation_->network)throw IrError("runtime checkpoint export requires a training model");if(artifactPath.empty())throw IrError("runtime GRU checkpoint path is empty");const auto parent=artifactPath.parent_path();if(!parent.empty())std::filesystem::create_directories(parent);torch::save(implementation_->network,artifactPath.string());
#else
    (void)artifactPath;throw IrError("runtime GRU checkpoint export requires FELIDAE_ENABLE_LIBTORCH=ON");
#endif
}

void GruRuntimeStateModel::exportTorchScript(const std::filesystem::path& artifactPath) const {
#ifdef FELIDAE_HAS_TORCH
    if(!implementation_->network)throw IrError("runtime TorchScript export requires a training model");if(artifactPath.empty())throw IrError("runtime TorchScript artifact path is empty");const auto parent=artifactPath.parent_path();if(!parent.empty())std::filesystem::create_directories(parent);
    torch::jit::Module module("FelidaeRuntimeGru");module.register_parameter("embedding",implementation_->network->embedding->weight.detach().clone(),false);module.register_parameter("projection_weight",implementation_->network->projection->weight.detach().clone(),false);module.register_parameter("projection_bias",implementation_->network->projection->bias.detach().clone(),false);
    std::ostringstream parameters;bool first=true;for(const auto& parameter:implementation_->network->recurrent->named_parameters(false)){const auto name="recurrent_"+parameter.key();module.register_parameter(name,parameter.value().detach().clone(),false);if(!first)parameters<<", ";parameters<<"self."<<name;first=false;}
    std::ostringstream source;source<<"def forward(self, input_ids: Tensor, hidden: Tensor) -> Tuple[Tensor, Tensor]:\n"<<"    embedded = torch.embedding(self.embedding, input_ids)\n"<<"    recurrent = torch.gru(embedded, hidden, ["<<parameters.str()<<"], True, "<<configuration_.layerCount<<", 0.0, False, False, False)\n"<<"    logits = torch.linear(recurrent[0][-1][0], self.projection_weight, self.projection_bias)\n"<<"    return logits, recurrent[1]\n";module.define(source.str());module.eval();module.save(artifactPath.string());
    std::ofstream manifest(parent/"runtime-manifest.txt",std::ios::trunc);if(!manifest)throw IrError("cannot write runtime SSM manifest");manifest<<"model_version=runtime-gru-v1\nbackend=torchscript-gru\nir_binary_version="<<kFelidaeBinaryVersion<<"\nopcode_vocabulary_version=felidae-isa-v1\nsymbol_encoding=felidae-fnv1a63-v1\narchitecture=gru-runtime-v1\ntraining_schema=felidae-runtime-operation-v7\ninput_vocabulary="<<configuration_.inputVocabularySize<<"\noutput_vocabulary="<<configuration_.outputVocabularySize<<"\nembedding_size="<<configuration_.embeddingSize<<"\nhidden_size="<<configuration_.hiddenSize<<"\nlayer_count="<<configuration_.layerCount<<"\nartifact_hash=fnv1a64:"<<std::hex<<fnv1a(artifactPath)<<"\n";
#else
    (void)artifactPath;throw IrError("runtime GRU TorchScript export requires FELIDAE_ENABLE_LIBTORCH=ON");
#endif
}
} // namespace Felidae
