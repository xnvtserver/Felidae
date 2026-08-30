#include "MixfixStateModel.h"
#include "ModelStore.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <sstream>
#include <utility>

#ifdef FELIDAE_HAS_TORCH
#include <torch/csrc/jit/api/module.h>
#include <torch/csrc/jit/serialization/import.h>
#include <torch/torch.h>
#endif

namespace Felidae {
namespace {

IrWord resolveReference(IrWord index, const std::vector<IrWord> &table,
                        const char *name) {
  if (index >= table.size()) {
    throw IrError(std::string("mixfix GRU emitted invalid ") + name +
                  " reference");
  }
  return table[index];
}

} // namespace

MixfixContext makeMixfixContext(const FelidaeIr &shell) {
  MixfixContext context;
  context.maximumOutputWords = 4096;
  context.outputVocabulary.push_back({MixfixIrTokenKind::Accept, 0});
  context.outputVocabulary.push_back({MixfixIrTokenKind::Reject, 0});
  context.outputVocabulary.push_back({MixfixIrTokenKind::Abstain, 0});
  context.outputVocabulary.push_back({MixfixIrTokenKind::End, 0});
  for (IrWord opcode = 1; opcode < kIrOpcodeCount; ++opcode) {
    context.outputVocabulary.push_back({MixfixIrTokenKind::Opcode, opcode});
  }
  for (IrWord index = 0; index < kMaximumMixfixRegisters; ++index) {
    context.outputVocabulary.push_back({MixfixIrTokenKind::Register, index});
  }
  for (IrWord index = 0; index < kMaximumMixfixReferences; ++index) {
    context.outputVocabulary.push_back(
        {MixfixIrTokenKind::ConstantReference, index});
  }
  for (IrWord index = 0; index < kMaximumMixfixReferences; ++index) {
    context.outputVocabulary.push_back(
        {MixfixIrTokenKind::SymbolReference, index});
  }
  for (IrWord index = 0; index < kMaximumMixfixReferences; ++index) {
    context.outputVocabulary.push_back(
        {MixfixIrTokenKind::FactReference, index});
  }
  for (IrWord index = 0; index < kMaximumMixfixReferences; ++index) {
    context.outputVocabulary.push_back(
        {MixfixIrTokenKind::ProgramReference, index});
  }
  for (IrWord index = 0; index < shell.constants.size(); ++index)
    context.constantReferences.push_back(index);
  for (IrWord index = 0; index < shell.symbols.size(); ++index)
    context.symbolReferences.push_back(index);
  return context;
}

std::vector<IrWord> resolveMixfixIrTokens(std::span<const MixfixIrToken> tokens,
                                          const MixfixContext &context) {
  if (context.maximumOutputWords == 0)
    throw IrError("mixfix IR output limit is zero");
  if (tokens.empty())
    throw IrError(
        "mixfix model emitted no ACCEPT, REJECT, or ABSTAIN decision");
  const auto contextSummary = [&] {
    return "constants=" + std::to_string(context.constantReferences.size()) +
           ", symbols=" + std::to_string(context.symbolReferences.size()) +
           ", facts=" + std::to_string(context.factReferences.size()) +
           ", programs=" + std::to_string(context.programReferences.size());
  };
  if (tokens.front().kind == MixfixIrTokenKind::Reject) {
    throw IrError("mixfix model rejected the input (" + contextSummary() +
                  "); it does not match a learned valid mixfix form");
  }
  if (tokens.front().kind == MixfixIrTokenKind::Abstain) {
    throw IrError(
        "mixfix model found no unique safe target (" + contextSummary() +
        "); declare a deterministic overload or retrain with this form");
  }
  if (tokens.front().kind != MixfixIrTokenKind::Accept)
    throw IrError(
        "mixfix model output must begin with ACCEPT, REJECT, or ABSTAIN");
  std::vector<IrWord> words;
  words.reserve(std::min(tokens.size(), context.maximumOutputWords));
  for (const auto &token : tokens.subspan(1)) {
    if (token.kind == MixfixIrTokenKind::End) {
      words.push_back(static_cast<IrWord>(IrOpcode::End));
      return words;
    }
    if (words.size() >= context.maximumOutputWords) {
      throw IrError("mixfix GRU exceeded its bounded decoder output");
    }
    switch (token.kind) {
    case MixfixIrTokenKind::Accept:
    case MixfixIrTokenKind::Reject:
    case MixfixIrTokenKind::Abstain:
      throw IrError("mixfix model emitted a decision token inside compiler IR");
    case MixfixIrTokenKind::Opcode:
      if (token.value >= kIrOpcodeCount) {
        throw IrError("mixfix GRU emitted invalid IR opcode");
      }
      words.push_back(token.value);
      break;
    case MixfixIrTokenKind::Register:
      words.push_back(token.value);
      break;
    case MixfixIrTokenKind::ConstantReference:
      words.push_back(resolveReference(token.value, context.constantReferences,
                                       "constant"));
      break;
    case MixfixIrTokenKind::SymbolReference:
      words.push_back(
          resolveReference(token.value, context.symbolReferences, "symbol"));
      break;
    case MixfixIrTokenKind::FactReference:
      words.push_back(
          resolveReference(token.value, context.factReferences, "fact"));
      break;
    case MixfixIrTokenKind::ProgramReference:
      words.push_back(
          resolveReference(token.value, context.programReferences, "program"));
      break;
    case MixfixIrTokenKind::End:
      break;
    }
  }
  throw IrError("mixfix GRU decoder did not emit IR_END");
}

std::vector<IrWord>
resolveMixfixVocabularyIds(std::span<const MixfixVocabularyId> ids,
                           const MixfixContext &context) {
  if (ids.size() > context.maximumOutputWords) {
    throw IrError("mixfix GRU exceeded its bounded decoder output");
  }
  std::vector<MixfixIrToken> tokens;
  tokens.reserve(ids.size());
  for (const auto id : ids) {
    if (id >= context.outputVocabulary.size()) {
      throw IrError(
          "mixfix GRU emitted a token outside the finite output vocabulary");
    }
    tokens.push_back(context.outputVocabulary[id]);
  }
  return resolveMixfixIrTokens(tokens, context);
}

FelidaeIr compileVerifiedMixfixIr(MixfixStateModel &model,
                                  std::span<const SentencePieceId> input,
                                  const MixfixContext &context,
                                  FelidaeIr irShell) {
  irShell.words =
      resolveMixfixVocabularyIds(model.transform(input, context), context);
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
    explicit Network(const Configuration &c) {
      embedding = register_module(
          "embedding",
          torch::nn::Embedding(c.inputVocabularySize, c.embeddingSize));
      decoderEmbedding = register_module(
          "decoder_embedding",
          torch::nn::Embedding(c.outputVocabularySize, c.embeddingSize));
      encoder = register_module(
          "encoder",
          torch::nn::GRU(torch::nn::GRUOptions(c.embeddingSize, c.hiddenSize)
                             .num_layers(c.layerCount)));
      decoder = register_module(
          "decoder",
          torch::nn::GRU(torch::nn::GRUOptions(c.embeddingSize, c.hiddenSize)
                             .num_layers(c.layerCount)));
      projection = register_module(
          "projection",
          torch::nn::Linear(c.hiddenSize, c.outputVocabularySize));
    }
    torch::nn::Embedding embedding{nullptr};
    torch::nn::Embedding decoderEmbedding{nullptr};
    torch::nn::GRU encoder{nullptr};
    torch::nn::GRU decoder{nullptr};
    torch::nn::Linear projection{nullptr};
  };

  Implementation(const Configuration &c,
                 const std::filesystem::path &artifact) {
    if (c.inputVocabularySize <= 0 || c.outputVocabularySize <= 0 ||
        c.embeddingSize <= 0 || c.hiddenSize <= 0 || c.layerCount <= 0 ||
        c.beginToken < 0 || c.beginToken >= c.outputVocabularySize) {
      throw IrError("mixfix GRU configuration is invalid");
    }
    if (artifact.empty() && c.allowRandomInitialization) {
      network = std::make_shared<Network>(c);
      network->eval();
      return;
    }
    if (artifact.empty() || !std::filesystem::is_regular_file(artifact)) {
      throw IrError("mixfix GRU artifact is unavailable: " + artifact.string());
    }
    if (artifact.extension() != ".pt") {
      throw IrError(
          "mixfix production artifact must be a .pt TorchScript module");
    }
    try {
      production = torch::jit::load(artifact.string());
      production->eval();
      torch::InferenceMode guard;
      const auto inputIds =
          torch::zeros({1, 1}, torch::TensorOptions().dtype(torch::kInt64));
      const auto decoderId = torch::full(
          {1, 1}, c.beginToken, torch::TensorOptions().dtype(torch::kInt64));
      const auto encoderHidden =
          production->get_method("encode")({inputIds}).toTensor();
      const auto output =
          production->get_method("decode_step")({decoderId, encoderHidden})
              .toTuple();
      if (output->elements().size() != 2) {
        throw IrError(
            "mixfix production artifact has an incompatible forward contract");
      }
      const auto logits = output->elements()[0].toTensor();
      const auto decoderHidden = output->elements()[1].toTensor();
      if (encoderHidden.dim() != 3 || encoderHidden.size(0) != c.layerCount ||
          encoderHidden.size(1) != 1 || encoderHidden.size(2) != c.hiddenSize ||
          logits.dim() != 1 || logits.size(0) != c.outputVocabularySize ||
          decoderHidden.sizes() != encoderHidden.sizes()) {
        throw IrError(
            "mixfix production artifact has an incompatible incremental "
            "decoder contract");
      }
    } catch (const c10::Error &error) {
      throw IrError("mixfix production artifact is not valid TorchScript: " +
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
  Implementation(const Configuration &, const std::filesystem::path &) {
    throw IrError("mixfix GRU backend requires FELIDAE_ENABLE_LIBTORCH=ON");
  }
#endif
};

GruMixfixStateModel::GruMixfixStateModel(Configuration c)
    : GruMixfixStateModel(std::move(c), {}, VersionedArtifact{}) {}

GruMixfixStateModel::GruMixfixStateModel(
    Configuration c, const std::filesystem::path &artifact, VersionedArtifact)
    : configuration_(c), implementation_(std::make_unique<Implementation>(
                             configuration_, artifact)) {}
GruMixfixStateModel::~GruMixfixStateModel() = default;
GruMixfixStateModel::GruMixfixStateModel(GruMixfixStateModel &&) noexcept =
    default;
GruMixfixStateModel &
GruMixfixStateModel::operator=(GruMixfixStateModel &&) noexcept = default;

GruMixfixStateModel GruMixfixStateModel::loadVersioned(
    Configuration configuration, const std::filesystem::path &artifactDirectory,
    std::string_view expectedSentencePieceIdentity,
    std::string_view expectedIrVocabularyVersion) {
  const auto manifest = readModelManifest(artifactDirectory / "manifest.txt");
  constexpr std::string_view modelName = "mixfix model";
  requireManifestValue(manifest, "artifact_format_version",
                       kMixfixArtifactFormatVersion, modelName);
  requireManifestValue(manifest, "model_family", kMixfixModelFamily, modelName);
  requireManifestValue(manifest, "model_version", kMixfixModelVersion,
                       modelName);
  requireManifestValue(manifest, "backend", "torchscript-gru", modelName);
  requireManifestValue(manifest, "tokenizer_contract", kMixfixTokenizerContract,
                       modelName);
  requireManifestValue(manifest, "ir_vocabulary_version",
                       expectedIrVocabularyVersion, modelName);
  requireManifestValue(manifest, "decoder_contract", kMixfixDecoderContract,
                       modelName);
  if (!expectedSentencePieceIdentity.empty() &&
      manifestValue(manifest, "sentencepiece_model_identity") !=
          expectedSentencePieceIdentity) {
    throw IrError("mixfix model SentencePiece vocabulary is incompatible");
  }
  if (manifestInteger(manifest, "input_vocabulary", modelName) !=
          configuration.inputVocabularySize ||
      manifestInteger(manifest, "output_vocabulary", modelName) !=
          configuration.outputVocabularySize ||
      manifestInteger(manifest, "begin_token", modelName) !=
          configuration.beginToken ||
      manifestInteger(manifest, "embedding_size", modelName) !=
          configuration.embeddingSize ||
      manifestInteger(manifest, "hidden_size", modelName) !=
          configuration.hiddenSize ||
      manifestInteger(manifest, "layer_count", modelName) !=
          configuration.layerCount) {
    throw IrError("mixfix model vocabulary configuration is incompatible");
  }
  const auto artifact = artifactDirectory / "mixfix-gru.pt";
  if (!std::filesystem::is_regular_file(artifact))
    throw IrError("mixfix model artifact is unavailable");
  return GruMixfixStateModel(std::move(configuration), artifact,
                             VersionedArtifact{});
}

std::vector<MixfixVocabularyId>
GruMixfixStateModel::transform(std::span<const SentencePieceId> input,
                               const MixfixContext &context) {
  if (input.empty())
    throw IrError("mixfix GRU input is empty");
  if (context.outputVocabulary.size() !=
      static_cast<std::size_t>(configuration_.outputVocabularySize)) {
    throw IrError("mixfix GRU output vocabulary does not match model artifact");
  }
#ifdef FELIDAE_HAS_TORCH
  std::vector<std::int64_t> ids;
  ids.reserve(input.size());
  for (const auto id : input) {
    if (id > static_cast<SentencePieceId>(
                 std::numeric_limits<std::int64_t>::max()) ||
        static_cast<std::int64_t>(id) >= configuration_.inputVocabularySize) {
      throw IrError(
          "mixfix GRU input SentencePiece ID is outside model vocabulary");
    }
    ids.push_back(static_cast<std::int64_t>(id));
  }
  torch::InferenceMode guard;
  const auto inputIds =
      torch::tensor(ids, torch::TensorOptions().dtype(torch::kInt64))
          .reshape({-1, 1});
  std::vector<MixfixVocabularyId> tokens;
  const auto limit =
      std::min(configuration_.maximumDecodeSteps, context.maximumOutputWords);
  torch::Tensor decoderHidden;
  if (implementation_->production) {
    decoderHidden =
        implementation_->production->get_method("encode")({inputIds})
            .toTensor();
  } else {
    decoderHidden = std::get<1>(implementation_->network->encoder->forward(
        implementation_->network->embedding->forward(inputIds)));
  }
  std::int64_t decoderId = configuration_.beginToken;
  for (std::size_t step = 0; step < limit; ++step) {
    torch::Tensor logits;
    const auto decoderInput =
        torch::tensor({decoderId}, torch::TensorOptions().dtype(torch::kInt64))
            .reshape({1, 1});
    if (implementation_->production) {
      const auto decoded =
          implementation_->production
              ->get_method("decode_step")({decoderInput, decoderHidden})
              .toTuple();
      logits = decoded->elements()[0].toTensor();
      decoderHidden = decoded->elements()[1].toTensor();
    } else {
      auto decoderResult = implementation_->network->decoder->forward(
          implementation_->network->decoderEmbedding->forward(decoderInput),
          decoderHidden);
      decoderHidden = std::get<1>(decoderResult);
      logits = implementation_->network->projection->forward(
          std::get<0>(decoderResult).select(0, 0).select(0, 0));
    }
    const auto tokenId = logits.argmax().item<std::int64_t>();
    if (tokenId < 0 ||
        static_cast<std::size_t>(tokenId) >= context.outputVocabulary.size()) {
      throw IrError("mixfix GRU emitted token outside finite vocabulary");
    }
    const auto vocabularyId = static_cast<MixfixVocabularyId>(tokenId);
    tokens.push_back(vocabularyId);
    const auto kind = context.outputVocabulary[vocabularyId].kind;
    if (kind == MixfixIrTokenKind::End || kind == MixfixIrTokenKind::Reject ||
        kind == MixfixIrTokenKind::Abstain)
      break;
    decoderId = tokenId;
  }
  return tokens;
#else
  (void)input;
  (void)context;
  throw IrError("mixfix GRU backend requires FELIDAE_ENABLE_LIBTORCH=ON");
#endif
}

double GruMixfixStateModel::trainTeacherForced(
    std::span<const SentencePieceId> input,
    std::span<const std::int64_t> targetTokenIds, double learningRate) {
  if (input.empty() || targetTokenIds.empty() || learningRate <= 0.0) {
    throw IrError("mixfix GRU teacher-forcing batch is invalid");
  }
#ifdef FELIDAE_HAS_TORCH
  std::vector<std::int64_t> encoderIds;
  encoderIds.reserve(input.size());
  for (const auto id : input) {
    if (id > static_cast<SentencePieceId>(
                 std::numeric_limits<std::int64_t>::max()) ||
        static_cast<std::int64_t>(id) >= configuration_.inputVocabularySize) {
      throw IrError(
          "teacher-forcing SentencePiece ID is outside model vocabulary");
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
  auto encoderInput =
      torch::tensor(encoderIds, torch::TensorOptions().dtype(torch::kInt64))
          .reshape({-1, 1});
  auto target = torch::tensor(
      std::vector<std::int64_t>(targetTokenIds.begin(), targetTokenIds.end()),
      torch::TensorOptions().dtype(torch::kInt64));
  auto decoderInput =
      torch::tensor(decoderIds, torch::TensorOptions().dtype(torch::kInt64))
          .reshape({-1, 1});
  implementation_->network->train();
  implementation_->prepareOptimizer(learningRate);
  implementation_->optimizer->zero_grad();
  const auto encoded = implementation_->network->encoder->forward(
      implementation_->network->embedding->forward(encoderInput));
  const auto decoded = implementation_->network->decoder->forward(
      implementation_->network->decoderEmbedding->forward(decoderInput),
      std::get<1>(encoded));
  const auto logits = implementation_->network->projection->forward(
      std::get<0>(decoded).squeeze(1));
  const auto loss = torch::nn::functional::cross_entropy(logits, target);
  loss.backward();
  implementation_->optimizer->step();
  implementation_->network->eval();
  return loss.item<double>();
#else
  (void)input;
  (void)targetTokenIds;
  (void)learningRate;
  throw IrError("mixfix GRU training requires FELIDAE_ENABLE_LIBTORCH=ON");
#endif
}

double GruMixfixStateModel::evaluateTeacherForced(
    std::span<const SentencePieceId> input,
    std::span<const std::int64_t> targetTokenIds) {
  if (input.empty() || targetTokenIds.empty()) {
    throw IrError("mixfix GRU validation batch is invalid");
  }
#ifdef FELIDAE_HAS_TORCH
  if (!implementation_->network) {
    throw IrError("mixfix teacher-forced validation requires a training model");
  }
  std::vector<std::int64_t> encoderIds;
  encoderIds.reserve(input.size());
  for (const auto id : input) {
    if (id > static_cast<SentencePieceId>(
                 std::numeric_limits<std::int64_t>::max()) ||
        static_cast<std::int64_t>(id) >= configuration_.inputVocabularySize) {
      throw IrError("validation SentencePiece ID is outside model vocabulary");
    }
    encoderIds.push_back(static_cast<std::int64_t>(id));
  }
  std::vector<std::int64_t> decoderIds{configuration_.beginToken};
  decoderIds.insert(decoderIds.end(), targetTokenIds.begin(),
                    targetTokenIds.end() - 1);
  for (const auto id : targetTokenIds) {
    if (id < 0 || id >= configuration_.outputVocabularySize) {
      throw IrError("validation IR token is outside finite vocabulary");
    }
  }
  torch::InferenceMode guard;
  implementation_->network->eval();
  const auto encoderInput =
      torch::tensor(encoderIds, torch::TensorOptions().dtype(torch::kInt64))
          .reshape({-1, 1});
  const auto decoderInput =
      torch::tensor(decoderIds, torch::TensorOptions().dtype(torch::kInt64))
          .reshape({-1, 1});
  const auto target = torch::tensor(
      std::vector<std::int64_t>(targetTokenIds.begin(), targetTokenIds.end()),
      torch::TensorOptions().dtype(torch::kInt64));
  const auto encoded = implementation_->network->encoder->forward(
      implementation_->network->embedding->forward(encoderInput));
  const auto decoded = implementation_->network->decoder->forward(
      implementation_->network->decoderEmbedding->forward(decoderInput),
      std::get<1>(encoded));
  const auto logits = implementation_->network->projection->forward(
      std::get<0>(decoded).squeeze(1));
  return torch::nn::functional::cross_entropy(logits, target).item<double>();
#else
  (void)input;
  (void)targetTokenIds;
  throw IrError("mixfix GRU validation requires FELIDAE_ENABLE_LIBTORCH=ON");
#endif
}

void GruMixfixStateModel::saveCheckpoint(
    const std::filesystem::path &artifactPath) const {
#ifdef FELIDAE_HAS_TORCH
  if (!implementation_->network)
    throw IrError("mixfix checkpoint export requires a training model");
  if (artifactPath.empty())
    throw IrError("mixfix GRU artifact path is empty");
  if (artifactPath.extension() != ".ckpt")
    throw IrError("mixfix training checkpoint must use the .ckpt extension");
  const auto parent = artifactPath.parent_path();
  if (!parent.empty())
    std::filesystem::create_directories(parent);
  torch::save(implementation_->network, artifactPath.string());
#else
  (void)artifactPath;
  throw IrError("mixfix GRU export requires FELIDAE_ENABLE_LIBTORCH=ON");
#endif
}

void GruMixfixStateModel::exportTorchScript(
    const std::filesystem::path &artifactPath) const {
#ifdef FELIDAE_HAS_TORCH
  if (!implementation_->network)
    throw IrError("mixfix TorchScript export requires a training model");
  if (artifactPath.empty())
    throw IrError("mixfix TorchScript artifact path is empty");
  if (artifactPath.extension() != ".pt")
    throw IrError("mixfix TorchScript artifact must use the .pt extension");
  const auto parent = artifactPath.parent_path();
  if (!parent.empty())
    std::filesystem::create_directories(parent);
  torch::jit::Module module("FelidaeMixfixGru");
  module.register_parameter(
      "encoder_embedding",
      implementation_->network->embedding->weight.detach().clone(), false);
  module.register_parameter(
      "decoder_embedding",
      implementation_->network->decoderEmbedding->weight.detach().clone(),
      false);
  module.register_parameter(
      "projection_weight",
      implementation_->network->projection->weight.detach().clone(), false);
  module.register_parameter(
      "projection_bias",
      implementation_->network->projection->bias.detach().clone(), false);
  std::ostringstream encoderParameters, decoderParameters;
  const auto registerGru = [&](const auto &gru, const char *prefix,
                               std::ostringstream &names) {
    bool first = true;
    for (const auto &parameter : gru->named_parameters(false)) {
      const auto name = std::string(prefix) + "_" + parameter.key();
      module.register_parameter(name, parameter.value().detach().clone(),
                                false);
      if (!first)
        names << ", ";
      names << "self." << name;
      first = false;
    }
  };
  registerGru(implementation_->network->encoder, "encoder", encoderParameters);
  registerGru(implementation_->network->decoder, "decoder", decoderParameters);
  std::ostringstream encoderSource;
  encoderSource
      << "def encode(self, input_ids: Tensor) -> Tensor:\n"
      << "    encoded_input = torch.embedding(self.encoder_embedding, "
         "input_ids)\n"
      << "    encoder_hidden = torch.zeros([" << configuration_.layerCount
      << ", input_ids.size(1), " << configuration_.hiddenSize
      << "], dtype=encoded_input.dtype)\n"
      << "    encoded = torch.gru(encoded_input, encoder_hidden, ["
      << encoderParameters.str() << "], True, " << configuration_.layerCount
      << ", 0.0, False, False, False)\n"
      << "    return encoded[1]\n";
  module.define(encoderSource.str());
  std::ostringstream decoderSource;
  decoderSource
      << "def decode_step(self, decoder_id: Tensor, hidden: Tensor) -> "
         "Tuple[Tensor, Tensor]:\n"
      << "    decoded_input = torch.embedding(self.decoder_embedding, "
         "decoder_id)\n"
      << "    decoded = torch.gru(decoded_input, hidden, ["
      << decoderParameters.str() << "], True, " << configuration_.layerCount
      << ", 0.0, False, False, False)\n"
      << "    logits = torch.linear(decoded[0][-1][0], "
         "self.projection_weight, self.projection_bias)\n"
      << "    return logits, decoded[1]\n";
  module.define(decoderSource.str());
  module.eval();
  module.save(artifactPath.string());
#else
  (void)artifactPath;
  throw IrError("mixfix GRU export requires FELIDAE_ENABLE_LIBTORCH=ON");
#endif
}

} // namespace Felidae
