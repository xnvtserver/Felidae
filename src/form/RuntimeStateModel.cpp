#include "RuntimeStateModel.h"

#include "BinaryIr.h"
#include "RuntimeTraining.h"
#include "SemanticOperation.h"
#include "../ModelStore.h"

#include <cmath>
#include <fstream>
#include <optional>
#include <sstream>

#ifdef FELIDAE_HAS_TORCH
#include <torch/csrc/jit/api/module.h>
#include <torch/csrc/jit/serialization/import.h>
#include <torch/torch.h>
#endif

namespace Felidae {
namespace {
std::vector<std::int64_t> runtimeInputIds(
    std::uint16_t operation, std::span<const RuntimeValueKind> inputKinds,
    std::span<const std::vector<std::uint32_t>> inputValues,
    std::span<const PieceSequence> factTypes,
    std::span<const std::pair<PieceSequence, std::uint32_t>> factTypeCounts,
    std::span<const std::pair<PieceSequence, PieceSequence>> hierarchyEdges,
    std::int64_t vocabularySize) {
  constexpr std::size_t kMaximumContextItems = 1'024;
  if (vocabularySize <= kRuntimeStructuralInputTokens)
    throw IrError("runtime GRU input vocabulary is invalid");
  const auto structural = [&](std::int64_t value) {
    return vocabularySize - kRuntimeStructuralInputTokens + value;
  };
  const auto appendValueTokens = [&](std::vector<std::int64_t> &ids,
                                     const auto &tokens) {
    constexpr std::uint32_t kStructuralBit = 0x80000000u;
    for (const auto token : tokens) {
      if ((token & kStructuralBit) != 0) {
        const auto marker = token & ~kStructuralBit;
        if (marker >= kRuntimeStructuralInputTokens)
          throw IrError("runtime GRU value marker exceeds its vocabulary");
        ids.push_back(structural(marker));
      } else {
        if (token >= static_cast<std::uint32_t>(
                         vocabularySize - kRuntimeStructuralInputTokens))
          throw IrError("runtime GRU value PieceId exceeds its vocabulary");
        ids.push_back(token);
      }
    }
  };
  const auto appendPieces = [&](std::vector<std::int64_t> &ids,
                                const PieceSequence &pieces) {
    if (pieces.empty())
      throw IrError("runtime GRU symbol has no SentencePiece IDs");
    for (const auto piece : pieces) {
      if (piece >= static_cast<PieceId>(vocabularySize -
                                        kRuntimeStructuralInputTokens)) {
        throw IrError("runtime GRU SentencePiece ID exceeds its vocabulary");
      }
      ids.push_back(piece);
    }
  };
  const auto kFactTypesMarker = structural(8);
  const auto kHierarchyMarker = structural(9);
  const auto kInputsMarker = structural(10);
  if (!isKnownSemanticOperation(operation) ||
      !semanticOperationAcceptsArity(operation, inputKinds.size()) ||
      (!inputValues.empty() && inputValues.size() != inputKinds.size()) ||
      inputKinds.size() > kMaximumContextItems ||
      factTypes.size() > kMaximumContextItems ||
      factTypeCounts.size() > kMaximumContextItems ||
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
  case SemanticOperationId::Suggest:
    break;
  }
  std::vector<std::int64_t> ids;
  ids.reserve(2 + inputKinds.size() + factTypes.size() +
              2 * factTypeCounts.size() + 2 * hierarchyEdges.size());
  ids.push_back(structural(static_cast<std::int64_t>(operation) - 1));
  ids.push_back(kFactTypesMarker);
  for (const auto &type : factTypes)
    appendPieces(ids, type);
  // Counts distinguish current fact population while keeping a fixed
  // integer vocabulary: one, two-to-four, and five-or-more facts.
  const auto kFactCountsMarker = structural(11);
  const auto kOneFactMarker = structural(12);
  const auto kSeveralFactsMarker = structural(13);
  const auto kManyFactsMarker = structural(14);
  ids.push_back(kFactCountsMarker);
  for (const auto &[type, count] : factTypeCounts) {
    appendPieces(ids, type);
    ids.push_back(count == 1  ? kOneFactMarker
                  : count < 5 ? kSeveralFactsMarker
                              : kManyFactsMarker);
  }
  ids.push_back(kHierarchyMarker);
  for (const auto &[child, parent] : hierarchyEdges) {
    appendPieces(ids, child);
    appendPieces(ids, parent);
  }
  ids.push_back(kInputsMarker);
  for (const auto kind : inputKinds) {
    if (kind < RuntimeValueKind::Nil || kind > RuntimeValueKind::TextMap) {
      throw IrError("runtime GRU input value kind is invalid");
    }
    ids.push_back(structural(15 + static_cast<std::int64_t>(kind)));
  }
  for (const auto &value : inputValues)
    appendValueTokens(ids, value);
  if (ids.size() > 4096)
    throw IrError("runtime GRU input sequence exceeds its bound");
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
    explicit Network(const Configuration &c) {
      embedding = register_module(
          "embedding",
          torch::nn::Embedding(c.inputVocabularySize, c.embeddingSize));
      recurrent = register_module(
          "recurrent",
          torch::nn::GRU(torch::nn::GRUOptions(c.embeddingSize, c.hiddenSize)
                             .num_layers(c.layerCount)));
      projection = register_module(
          "projection",
          torch::nn::Linear(c.hiddenSize, c.outputVocabularySize));
      scoreProjection = register_module("score_projection",
                                        torch::nn::Linear(c.hiddenSize, 1));
    }
    torch::nn::Embedding embedding{nullptr};
    torch::nn::GRU recurrent{nullptr};
    torch::nn::Linear projection{nullptr};
    torch::nn::Linear scoreProjection{nullptr};
  };
  struct ExecutionState {
    torch::Tensor hidden;
  };
  explicit Implementation(const Configuration &c,
                          const std::filesystem::path &artifact) {
    if (c.inputVocabularySize <= kRuntimeStructuralInputTokens ||
        c.outputVocabularySize <= 0 || c.embeddingSize <= 0 ||
        c.hiddenSize <= 0 || c.layerCount <= 0) {
      throw IrError("runtime GRU configuration is invalid");
    }
    if (artifact.empty() && c.allowRandomInitialization) {
      network = std::make_shared<Network>(c);
      network->eval();
      return;
    }
    if (artifact.empty() || !std::filesystem::is_regular_file(artifact)) {
      throw IrError("runtime GRU artifact is unavailable: " +
                    artifact.string());
    }
    if (artifact.extension() != ".pt") {
      throw IrError(
          "runtime production artifact must be a .pt TorchScript module");
    }
    try {
      production = torch::jit::load(artifact.string());
      production->eval();
      torch::InferenceMode guard;
      const auto input =
          torch::zeros({1, 1}, torch::TensorOptions().dtype(torch::kInt64));
      const auto hidden = torch::zeros({c.layerCount, 1, c.hiddenSize});
      const auto output = production->forward({input, hidden}).toTuple();
      if (output->elements().size() != 3) {
        throw IrError(
            "runtime production artifact has an incompatible forward contract");
      }
      const auto logits = output->elements()[0].toTensor();
      const auto score = output->elements()[1].toTensor();
      const auto nextHidden = output->elements()[2].toTensor();
      if (logits.dim() != 1 || logits.size(0) != c.outputVocabularySize ||
          score.numel() != 1 ||
          nextHidden.dim() != 3 || nextHidden.size(0) != c.layerCount ||
          nextHidden.size(1) != 1 || nextHidden.size(2) != c.hiddenSize) {
        throw IrError(
            "runtime production artifact has an incompatible forward contract");
      }
    } catch (const c10::Error &error) {
      throw IrError("runtime production artifact is not valid TorchScript: " +
                    std::string(error.what_without_backtrace()));
    }
  }
  std::shared_ptr<Network> network;
  std::optional<torch::jit::Module> production;
  std::unique_ptr<torch::optim::Adam> optimizer;
  double optimizerLearningRate = 0.0;

  void prepareOptimizer(double learningRate) {
    if (!network)
      throw IrError("TorchScript inference artifact cannot be used as a "
                    "training checkpoint");
    if (!optimizer || optimizerLearningRate != learningRate) {
      optimizer = std::make_unique<torch::optim::Adam>(
          network->parameters(), torch::optim::AdamOptions(learningRate));
      optimizerLearningRate = learningRate;
    }
  }
#else
  explicit Implementation(const Configuration &,
                          const std::filesystem::path &) {
    throw IrError("runtime GRU backend requires FELIDAE_ENABLE_LIBTORCH=ON");
  }
#endif
};

GruRuntimeStateModel::GruRuntimeStateModel(
    Configuration c, std::vector<RuntimeOutputToken> vocabulary)
    : GruRuntimeStateModel(std::move(c), std::move(vocabulary), {},
                           VersionedArtifact{}) {}

GruRuntimeStateModel::GruRuntimeStateModel(
    Configuration c, std::vector<RuntimeOutputToken> vocabulary,
    const std::filesystem::path &artifact, VersionedArtifact)
    : configuration_(c), outputVocabulary_(std::move(vocabulary)),
      implementation_(
          std::make_unique<Implementation>(configuration_, artifact)) {
  if (configuration_.inputVocabularySize <= kRuntimeStructuralInputTokens ||
      configuration_.outputVocabularySize <= 0 ||
      configuration_.embeddingSize <= 0 || configuration_.hiddenSize <= 0 ||
      configuration_.layerCount <= 0 ||
      outputVocabulary_.size() !=
          static_cast<std::size_t>(configuration_.outputVocabularySize))
    throw IrError(
        "runtime GRU configuration or finite output vocabulary is invalid");
  for (const auto &token : outputVocabulary_)
    if ((token.kind == RuntimeOutputTokenKind::NumericTruth &&
         token.value > 1) ||
        (token.kind == RuntimeOutputTokenKind::DegreeMilli &&
         token.value > 1000))
      throw IrError("runtime GRU output token is invalid");
}
GruRuntimeStateModel::~GruRuntimeStateModel() = default;
GruRuntimeStateModel::GruRuntimeStateModel(GruRuntimeStateModel &&) noexcept =
    default;
GruRuntimeStateModel &
GruRuntimeStateModel::operator=(GruRuntimeStateModel &&) noexcept = default;

GruRuntimeStateModel GruRuntimeStateModel::loadVersioned(
    Configuration c, std::vector<RuntimeOutputToken> vocabulary,
    const std::filesystem::path &directory,
    std::string_view expectedSentencePieceIdentity) {
  const auto manifest =
      readModelManifest(directory / "runtime-manifest.txt");
  constexpr std::string_view modelName = "runtime SSM";
  requireManifestValue(manifest, "artifact_format_version",
                       kRuntimeArtifactFormatVersion, modelName);
  requireManifestValue(manifest, "model_family", kRuntimeModelFamily,
                       modelName);
  requireManifestValue(manifest, "model_version", kRuntimeModelVersion,
                       modelName);
  requireManifestValue(manifest, "backend", "torchscript-gru", modelName);
  requireManifestValue(manifest, "tokenizer_contract",
                       kRuntimeTokenizerContract, modelName);
  requireManifestValue(manifest, "sentencepiece_model_identity",
                       expectedSentencePieceIdentity, modelName);
  requireManifestValue(manifest, "decoder_contract", kRuntimeDecoderContract,
                       modelName);
  // Checked against LANGUAGE_VERSION (Version.h), the one source of truth
  // for every version this project reports -- there is no separate binary
  // IR format version to keep in sync with it.
  requireManifestValue(manifest, "ir_binary_version", LANGUAGE_VERSION,
                       modelName);
  requireManifestValue(manifest, "opcode_vocabulary_version", "felidae-ir-v13",
                       modelName);
  requireManifestValue(manifest, "training_schema",
                       "felidae-runtime-operation-v9", modelName);
  if (manifestInteger(manifest, "input_vocabulary", modelName) !=
          c.inputVocabularySize ||
      manifestInteger(manifest, "output_vocabulary", modelName) !=
          c.outputVocabularySize ||
      manifestInteger(manifest, "embedding_size", modelName) !=
          c.embeddingSize ||
      manifestInteger(manifest, "hidden_size", modelName) != c.hiddenSize ||
      manifestInteger(manifest, "layer_count", modelName) != c.layerCount)
    throw IrError("runtime SSM manifest configuration is incompatible");
  const auto artifact = directory / "runtime-gru.pt";
  if (!std::filesystem::is_regular_file(artifact))
    throw IrError("runtime SSM artifact is unavailable");
  return GruRuntimeStateModel(std::move(c), std::move(vocabulary), artifact,
                              VersionedArtifact{});
}

std::shared_ptr<void> GruRuntimeStateModel::createExecutionState() {
#ifdef FELIDAE_HAS_TORCH
  return std::make_shared<Implementation::ExecutionState>();
#else
  throw IrError("runtime GRU backend requires FELIDAE_ENABLE_LIBTORCH=ON");
#endif
}

Value GruRuntimeStateModel::evaluate(const RuntimeOperation &operation,
                                     std::span<const Value> inputs,
                                     RuntimeContext &context) {
#ifdef FELIDAE_HAS_TORCH
  if (!context.executionState)
    throw IrError(
        "runtime GRU evaluation has no execution-local recurrent state");
  auto state = std::static_pointer_cast<Implementation::ExecutionState>(
      context.executionState);
  std::vector<RuntimeValueKind> inputKinds;
  inputKinds.reserve(inputs.size());
  for (const auto &input : inputs)
    inputKinds.push_back(runtimeValueKind(input));
  const auto symbols =
      context.symbolTable ? std::span<const PieceSequence>(*context.symbolTable)
                          : std::span<const PieceSequence>{};
  const auto knowledge = runtimeKnowledgePieces(context.knowledge, symbols);
  std::vector<std::vector<std::uint32_t>> inputValues;
  inputValues.reserve(inputs.size());
  for (const auto &value : inputs)
    inputValues.push_back(runtimeValueEncoding(value, symbols));
  const auto ids = runtimeInputIds(
      operation.id, inputKinds, inputValues, knowledge.factTypes,
      knowledge.factTypeCounts, knowledge.hierarchyEdges,
      configuration_.inputVocabularySize);
  torch::InferenceMode guard;
  const auto input =
      torch::tensor(ids, torch::TensorOptions().dtype(torch::kInt64))
          .reshape({-1, 1});
  torch::Tensor logits;
  torch::Tensor score;
  if (implementation_->production) {
    if (!state->hidden.defined())
      state->hidden = torch::zeros(
          {configuration_.layerCount, 1, configuration_.hiddenSize});
    auto output =
        implementation_->production->forward({input, state->hidden}).toTuple();
    logits = output->elements().at(0).toTensor();
    score = output->elements().at(1).toTensor();
    state->hidden = output->elements().at(2).toTensor().detach();
  } else {
    const auto result =
        state->hidden.defined()
            ? implementation_->network->recurrent->forward(
                  implementation_->network->embedding->forward(input),
                  state->hidden)
            : implementation_->network->recurrent->forward(
                  implementation_->network->embedding->forward(input));
    state->hidden = std::get<1>(result).detach();
    const auto last = std::get<0>(result)
                          .select(0, std::get<0>(result).size(0) - 1)
                          .select(0, 0);
    logits = implementation_->network->projection->forward(last);
    score = implementation_->network->scoreProjection->forward(last);
  }
  if (operation.id == static_cast<std::uint16_t>(SemanticOperationId::Suggest)) {
    const auto value = score.item<double>();
    if (!std::isfinite(value))
      throw IrError("runtime GRU scoring head produced a non-finite value");
    return value;
  }
  const auto outputId = logits.argmax().item<std::int64_t>();
  if (outputId < 0 ||
      static_cast<std::size_t>(outputId) >= outputVocabulary_.size())
    throw IrError(
        "runtime GRU emitted a token outside its finite output vocabulary");
  const auto &token = outputVocabulary_[static_cast<std::size_t>(outputId)];
  switch (token.kind) {
  case RuntimeOutputTokenKind::InputReference:
    if (token.value >= inputs.size())
      throw IrError("runtime GRU emitted an unavailable input reference");
    return inputs[token.value];
  case RuntimeOutputTokenKind::FactFromInput: {
    if (token.value >= inputs.size())
      throw IrError("runtime GRU emitted an unavailable fact input reference");
    const auto fact = std::get_if<VmFactPtr>(&inputs[token.value]);
    if (!fact || !*fact)
      throw IrError("runtime GRU fact action requires a fact input");
    auto inferred = std::make_shared<VmFact>(**fact);
    inferred->id = 0;
    return inferred;
  }
  case RuntimeOutputTokenKind::DegreeMilli:
    if (operation.id == static_cast<std::uint16_t>(SemanticOperationId::Suggest))
      return static_cast<double>(token.value) / 1000.0;
    return VmDegree(static_cast<double>(token.value) / 1000.0);
  case RuntimeOutputTokenKind::Nil:
    return VmNil{};
  case RuntimeOutputTokenKind::NumericTruth:
    return token.value != 0 ? 1.0 : 0.0;
  }
  throw IrError("runtime GRU emitted an invalid output token");
#else
  (void)operation;
  (void)inputs;
  (void)context;
  throw IrError("runtime GRU backend requires FELIDAE_ENABLE_LIBTORCH=ON");
#endif
}

double
GruRuntimeStateModel::trainTeacherForced(const RuntimeTrainingRecord &record,
                                         std::size_t targetToken,
                                         double learningRate) {
#ifdef FELIDAE_HAS_TORCH
  verifyRuntimeTrainingRecord(record);
  if (targetToken >= outputVocabulary_.size() || learningRate <= 0.0)
    throw IrError("runtime GRU training target or learning rate is invalid");
  const auto ids =
      runtimeInputIds(record.operationId, record.inputKinds, record.inputValues,
                      record.factTypes,
                      record.factTypeCounts, record.hierarchyEdges,
                      configuration_.inputVocabularySize);
  if (ids.size() > 4096)
    throw IrError("runtime GRU training record is too large");
  implementation_->prepareOptimizer(learningRate);
  implementation_->network->train();
  implementation_->optimizer->zero_grad();
  const auto input =
      torch::tensor(ids, torch::TensorOptions().dtype(torch::kInt64))
          .reshape({-1, 1});
  const auto recurrent = implementation_->network->recurrent->forward(
      implementation_->network->embedding->forward(input));
  const auto logits = implementation_->network->projection->forward(
      std::get<0>(recurrent)
          .select(0, std::get<0>(recurrent).size(0) - 1)
          .select(0, 0));
  const auto target =
      torch::tensor({static_cast<std::int64_t>(targetToken)},
                    torch::TensorOptions().dtype(torch::kInt64));
  const auto loss =
      torch::nn::functional::cross_entropy(logits.unsqueeze(0), target);
  loss.backward();
  implementation_->optimizer->step();
  implementation_->network->eval();
  return loss.item<double>();
#else
  (void)record;
  (void)targetToken;
  (void)learningRate;
  throw IrError("runtime GRU training requires FELIDAE_ENABLE_LIBTORCH=ON");
#endif
}

double GruRuntimeStateModel::trainScore(const RuntimeTrainingRecord &record,
                                        double learningRate) {
#ifdef FELIDAE_HAS_TORCH
  verifyRuntimeTrainingRecord(record);
  if (record.operationId !=
          static_cast<std::uint16_t>(SemanticOperationId::Suggest) ||
      record.targetKind != RuntimeTrainingTargetKind::Score ||
      !std::isfinite(record.targetScore) || learningRate <= 0.0)
    throw IrError("runtime GRU score target or learning rate is invalid");
  const auto ids = runtimeInputIds(
      record.operationId, record.inputKinds, record.inputValues, record.factTypes,
      record.factTypeCounts, record.hierarchyEdges,
      configuration_.inputVocabularySize);
  implementation_->prepareOptimizer(learningRate);
  implementation_->network->train();
  implementation_->optimizer->zero_grad();
  const auto input =
      torch::tensor(ids, torch::TensorOptions().dtype(torch::kInt64))
          .reshape({-1, 1});
  const auto recurrent = implementation_->network->recurrent->forward(
      implementation_->network->embedding->forward(input));
  const auto last = std::get<0>(recurrent)
                        .select(0, std::get<0>(recurrent).size(0) - 1)
                        .select(0, 0);
  const auto predicted =
      implementation_->network->scoreProjection->forward(last).reshape({});
  const auto expected =
      torch::tensor(record.targetScore, predicted.options());
  const auto loss = torch::mse_loss(predicted, expected);
  const auto lossValue = loss.item<double>();
  if (!std::isfinite(lossValue))
    throw IrError("runtime GRU score training produced non-finite loss");
  loss.backward();
  implementation_->optimizer->step();
  implementation_->network->eval();
  return lossValue;
#else
  (void)record;
  (void)learningRate;
  throw IrError("runtime GRU score training requires FELIDAE_ENABLE_LIBTORCH=ON");
#endif
}

std::size_t GruRuntimeStateModel::predictTeacherToken(
    const RuntimeTrainingRecord &record) const {
#ifdef FELIDAE_HAS_TORCH
  if (!implementation_->network)
    throw IrError("runtime teacher prediction requires a training model");
  verifyRuntimeTrainingRecord(record);
  const auto ids =
      runtimeInputIds(record.operationId, record.inputKinds, record.inputValues,
                      record.factTypes,
                      record.factTypeCounts, record.hierarchyEdges,
                      configuration_.inputVocabularySize);
  torch::InferenceMode guard;
  const auto input =
      torch::tensor(ids, torch::TensorOptions().dtype(torch::kInt64))
          .reshape({-1, 1});
  const auto recurrent = implementation_->network->recurrent->forward(
      implementation_->network->embedding->forward(input));
  const auto output =
      implementation_->network->projection
          ->forward(std::get<0>(recurrent)
                        .select(0, std::get<0>(recurrent).size(0) - 1)
                        .select(0, 0))
          .argmax()
          .item<std::int64_t>();
  if (output < 0 ||
      static_cast<std::size_t>(output) >= outputVocabulary_.size()) {
    throw IrError("runtime GRU validation emitted an invalid token");
  }
  return static_cast<std::size_t>(output);
#else
  (void)record;
  throw IrError("runtime GRU backend requires FELIDAE_ENABLE_LIBTORCH=ON");
#endif
}

double GruRuntimeStateModel::predictScore(
    const RuntimeTrainingRecord &record) const {
#ifdef FELIDAE_HAS_TORCH
  if (!implementation_->network ||
      record.operationId !=
          static_cast<std::uint16_t>(SemanticOperationId::Suggest))
    throw IrError("runtime score prediction requires a Suggest training record");
  verifyRuntimeTrainingRecord(record);
  const auto ids = runtimeInputIds(
      record.operationId, record.inputKinds, record.inputValues, record.factTypes,
      record.factTypeCounts, record.hierarchyEdges,
      configuration_.inputVocabularySize);
  torch::InferenceMode guard;
  const auto input =
      torch::tensor(ids, torch::TensorOptions().dtype(torch::kInt64))
          .reshape({-1, 1});
  const auto recurrent = implementation_->network->recurrent->forward(
      implementation_->network->embedding->forward(input));
  const auto value = implementation_->network->scoreProjection
                         ->forward(std::get<0>(recurrent)
                                       .select(0, std::get<0>(recurrent).size(0) - 1)
                                       .select(0, 0))
                         .item<double>();
  if (!std::isfinite(value))
    throw IrError("runtime GRU scoring head produced a non-finite value");
  return value;
#else
  (void)record;
  throw IrError("runtime GRU backend requires FELIDAE_ENABLE_LIBTORCH=ON");
#endif
}

void GruRuntimeStateModel::saveCheckpoint(
    const std::filesystem::path &artifactPath) const {
#ifdef FELIDAE_HAS_TORCH
  if (!implementation_->network)
    throw IrError("runtime checkpoint export requires a training model");
  if (artifactPath.empty())
    throw IrError("runtime GRU checkpoint path is empty");
  if (artifactPath.extension() != ".ckpt") {
    throw IrError("runtime training checkpoint must use the .ckpt extension");
  }
  const auto parent = artifactPath.parent_path();
  if (!parent.empty())
    std::filesystem::create_directories(parent);
  torch::save(implementation_->network, artifactPath.string());
#else
  (void)artifactPath;
  throw IrError(
      "runtime GRU checkpoint export requires FELIDAE_ENABLE_LIBTORCH=ON");
#endif
}

void GruRuntimeStateModel::exportTorchScript(
    const std::filesystem::path &artifactPath,
    std::string_view sentencePieceIdentity) const {
#ifdef FELIDAE_HAS_TORCH
  if (!implementation_->network)
    throw IrError("runtime TorchScript export requires a training model");
  if (artifactPath.empty())
    throw IrError("runtime TorchScript artifact path is empty");
  if (artifactPath.extension() != ".pt") {
    throw IrError("runtime TorchScript artifact must use the .pt extension");
  }
  if (!sentencePieceIdentity.starts_with("sha256:") ||
      sentencePieceIdentity.size() != 71) {
    throw IrError("runtime TorchScript export requires an exact SHA-256 "
                  "SentencePiece identity");
  }
  const auto parent = artifactPath.parent_path();
  if (!parent.empty())
    std::filesystem::create_directories(parent);
  torch::jit::Module module("FelidaeRuntimeGru");
  module.register_parameter(
      "embedding", implementation_->network->embedding->weight.detach().clone(),
      false);
  module.register_parameter(
      "projection_weight",
      implementation_->network->projection->weight.detach().clone(), false);
  module.register_parameter(
      "projection_bias",
      implementation_->network->projection->bias.detach().clone(), false);
  module.register_parameter(
      "score_projection_weight",
      implementation_->network->scoreProjection->weight.detach().clone(),
      false);
  module.register_parameter(
      "score_projection_bias",
      implementation_->network->scoreProjection->bias.detach().clone(), false);
  std::ostringstream parameters;
  bool first = true;
  for (const auto &parameter :
       implementation_->network->recurrent->named_parameters(false)) {
    const auto name = "recurrent_" + parameter.key();
    module.register_parameter(name, parameter.value().detach().clone(), false);
    if (!first)
      parameters << ", ";
    parameters << "self." << name;
    first = false;
  }
  std::ostringstream source;
  source << "def forward(self, input_ids: Tensor, hidden: Tensor) -> "
            "Tuple[Tensor, Tensor, Tensor]:\n"
         << "    embedded = torch.embedding(self.embedding, input_ids)\n"
         << "    recurrent = torch.gru(embedded, hidden, [" << parameters.str()
         << "], True, " << configuration_.layerCount
         << ", 0.0, False, False, False)\n"
         << "    logits = torch.linear(recurrent[0][-1][0], "
            "self.projection_weight, self.projection_bias)\n"
         << "    score = torch.linear(recurrent[0][-1][0], "
            "self.score_projection_weight, self.score_projection_bias)\n"
         << "    return logits, score, recurrent[1]\n";
  module.define(source.str());
  module.eval();
  module.save(artifactPath.string());
  std::ofstream manifest(parent / "runtime-manifest.txt", std::ios::trunc);
  if (!manifest)
    throw IrError("cannot write runtime SSM manifest");
  manifest << "artifact_format_version=" << kRuntimeArtifactFormatVersion
           << "\nmodel_family=" << kRuntimeModelFamily
           << "\nmodel_version=" << kRuntimeModelVersion
           << "\nbackend=torchscript-gru\ntokenizer_contract="
           << kRuntimeTokenizerContract
           << "\nsentencepiece_model_identity=" << sentencePieceIdentity
           << "\ndecoder_contract=" << kRuntimeDecoderContract
           << "\nir_binary_version=" << LANGUAGE_VERSION
           << "\nopcode_vocabulary_version=felidae-ir-v13\ntraining_schema="
              "felidae-runtime-operation-v9\ninput_vocabulary="
           << configuration_.inputVocabularySize
           << "\noutput_vocabulary=" << configuration_.outputVocabularySize
           << "\nembedding_size=" << configuration_.embeddingSize
           << "\nhidden_size=" << configuration_.hiddenSize
           << "\nlayer_count=" << configuration_.layerCount << "\n";
#else
  (void)artifactPath;
  (void)sentencePieceIdentity;
  throw IrError(
      "runtime GRU TorchScript export requires FELIDAE_ENABLE_LIBTORCH=ON");
#endif
}
} // namespace Felidae
