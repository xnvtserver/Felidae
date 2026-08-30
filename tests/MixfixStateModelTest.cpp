#include "MixfixStateModel.h"
#include "CompilerFrontend.h"
#include "SentencePieceModel.h"
#include "form/BinaryIr.h"
#include "form/RuntimeStateModel.h"
#include "form/RuntimeTraining.h"
#include "form/SemanticOperation.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <sentencepiece_processor.h>

namespace {

template <typename Action> bool rejects(Action &&action) {
  try {
    std::forward<Action>(action)();
  } catch (const Felidae::IrError &) {
    return true;
  }
  return false;
}

template <typename Action> std::string rejectionMessage(Action &&action) {
  try {
    std::forward<Action>(action)();
  } catch (const Felidae::IrError &error) {
    return error.what();
  }
  return {};
}

void writeMixfixManifest(
    const std::filesystem::path &directory,
    const Felidae::GruMixfixStateModel::Configuration &configuration,
    std::string_view sentencePieceIdentity,
    std::string_view modelVersion = Felidae::kMixfixModelVersion) {
  std::ofstream manifest(directory / "manifest.txt", std::ios::trunc);
  assert(manifest);
  manifest << "artifact_format_version="
           << Felidae::kMixfixArtifactFormatVersion
           << "\nmodel_family=" << Felidae::kMixfixModelFamily
           << "\nmodel_version=" << modelVersion
           << "\nbackend=torchscript-gru\ntokenizer_contract="
           << Felidae::kMixfixTokenizerContract
           << "\nsentencepiece_model_identity=" << sentencePieceIdentity
           << "\nir_vocabulary_version=" << Felidae::kMixfixIrVocabularyVersion
           << "\ndecoder_contract=" << Felidae::kMixfixDecoderContract
           << "\ninput_vocabulary=" << configuration.inputVocabularySize
           << "\noutput_vocabulary=" << configuration.outputVocabularySize
           << "\nbegin_token=" << configuration.beginToken
           << "\nembedding_size=" << configuration.embeddingSize
           << "\nhidden_size=" << configuration.hiddenSize
           << "\nlayer_count=" << configuration.layerCount << '\n';
}

Felidae::VmValue
executeDirect(const Felidae::FelidaeIr &program, Felidae::VmRuntime &runtime,
              std::unordered_map<Felidae::IrSymbolRef, Felidae::IrProcedure>
                  procedures = {}) {
  constexpr Felidae::IrSymbolRef kTestEntry = 1;
  if (procedures.contains(kTestEntry))
    throw Felidae::IrError("test procedure ID collision");
  Felidae::IrModule module;
  module.entryProcedure = kTestEntry;
  module.ir.registerCount = 1;
  module.ir.symbols = {kTestEntry};
  module.ir.words = {
      static_cast<Felidae::IrWord>(Felidae::IrOpcode::Call),
      0,
      0,
      0,
      static_cast<Felidae::IrWord>(Felidae::IrOpcode::Return),
      0,
      0,
      static_cast<Felidae::IrWord>(Felidae::IrOpcode::End),
  };
  module.procedures = std::move(procedures);
  module.procedures.emplace(kTestEntry,
                            Felidae::IrProcedure{program, {}, {}, {}});
  Felidae::IrSymbolRef largestSymbol = kTestEntry;
  const auto includeSymbols = [&](const Felidae::IrProcedure &procedure) {
    for (const auto symbol : procedure.ir.symbols)
      largestSymbol = std::max(largestSymbol, symbol);
    for (const auto symbol : procedure.positionalParameters)
      largestSymbol = std::max(largestSymbol, symbol);
    for (const auto symbol : procedure.namedParameters)
      largestSymbol = std::max(largestSymbol, symbol);
  };
  for (const auto symbol : module.ir.symbols)
    largestSymbol = std::max(largestSymbol, symbol);
  for (const auto &[symbol, procedure] : module.procedures) {
    largestSymbol = std::max(largestSymbol, symbol);
    includeSymbols(procedure);
  }
  module.sentencePieceModelIdentity = "sha256:test";
  module.symbolTable.reserve(largestSymbol);
  for (Felidae::IrSymbolRef symbol = 1; symbol <= largestSymbol; ++symbol) {
    module.symbolTable.push_back({symbol});
  }
  return Felidae::RegisterVm{}.executeMain(
      Felidae::verifyIrModule(std::move(module)), runtime);
}

bool rejectsBranchValue(Felidae::VmRuntime &runtime,
                        const Felidae::VmValue &value) {
  try {
    (void)runtime.shouldBranchFalse(value);
  } catch (const Felidae::IrError &) {
    return true;
  }
  return false;
}

} // namespace

int main() {
  using namespace Felidae;

  class FixedModel final : public MixfixStateModel {
  public:
    std::vector<MixfixVocabularyId> transform(std::span<const SentencePieceId>,
                                              const MixfixContext &) override {
      return {0, 1, 2, 3, 4};
    }
  };
  class InvalidModel final : public MixfixStateModel {
  public:
    std::vector<MixfixVocabularyId> transform(std::span<const SentencePieceId>,
                                              const MixfixContext &) override {
      return {0, 5, 2, 3, 4};
    }
  };
  class OversizedModel final : public MixfixStateModel {
  public:
    std::vector<MixfixVocabularyId> transform(std::span<const SentencePieceId>,
                                              const MixfixContext &) override {
      return {0, 1, 2, 3, 4};
    }
  };
  class UnknownVocabularyModel final : public MixfixStateModel {
  public:
    std::vector<MixfixVocabularyId> transform(std::span<const SentencePieceId>,
                                              const MixfixContext &) override {
      return {999};
    }
  };

  MixfixContext context;
  context.constantReferences = {0};
  context.outputVocabulary = {
      {MixfixIrTokenKind::Accept, 0},
      {MixfixIrTokenKind::Opcode, static_cast<IrWord>(IrOpcode::LoadConst)},
      {MixfixIrTokenKind::Register, 0},
      {MixfixIrTokenKind::ConstantReference, 0},
      {MixfixIrTokenKind::End, 0},
  };
  const std::vector<MixfixIrToken> valid = {
      {MixfixIrTokenKind::Accept, 0},
      {MixfixIrTokenKind::Opcode, static_cast<IrWord>(IrOpcode::LoadConst)},
      {MixfixIrTokenKind::Register, 0},
      {MixfixIrTokenKind::ConstantReference, 0},
      {MixfixIrTokenKind::End, 0},
  };
  const auto words = resolveMixfixIrTokens(valid, context);
  assert(
      (words == std::vector<IrWord>{static_cast<IrWord>(IrOpcode::LoadConst), 0,
                                    0, static_cast<IrWord>(IrOpcode::End)}));
  const auto rejectedMessage = rejectionMessage([&] {
    const std::array<MixfixIrToken, 1> decision{
        {{MixfixIrTokenKind::Reject, 0}}};
    (void)resolveMixfixIrTokens(decision, context);
  });
  assert(rejectedMessage.find("constants=1") != std::string::npos);
  assert(rejectedMessage.find("learned valid mixfix form") !=
         std::string::npos);
  const auto abstainedMessage = rejectionMessage([&] {
    const std::array<MixfixIrToken, 1> decision{
        {{MixfixIrTokenKind::Abstain, 0}}};
    (void)resolveMixfixIrTokens(decision, context);
  });
  assert(abstainedMessage.find("constants=1") != std::string::npos);
  assert(abstainedMessage.find("deterministic overload") != std::string::npos);

  FelidaeIr ir;
  ir.words = words;
  ir.constants = {{IrConstantKind::Number, encodeIrNumber(1.0)}};
  ir.registerCount = 1;
  IrVerifier::verify(ir);

  FixedModel model;
  const std::vector<SentencePieceId> input = {1};
  const auto verified = compileVerifiedMixfixIr(model, input, context, ir);
  assert(verified.words == words);

  InvalidModel invalidModel;
  bool verifierRejected = false;
  try {
    (void)compileVerifiedMixfixIr(invalidModel, input, context, ir);
  } catch (const IrError &) {
    verifierRejected = true;
  }
  assert(verifierRejected);

  MixfixContext tightContext = context;
  tightContext.maximumOutputWords = 1;
  OversizedModel oversizedModel;
  bool oversizedRejected = false;
  try {
    (void)compileVerifiedMixfixIr(oversizedModel, input, tightContext, ir);
  } catch (const IrError &) {
    oversizedRejected = true;
  }
  assert(oversizedRejected);
  UnknownVocabularyModel unknownVocabularyModel;
  bool vocabularyRejected = false;
  try {
    (void)compileVerifiedMixfixIr(unknownVocabularyModel, input, context, ir);
  } catch (const IrError &) {
    vocabularyRejected = true;
  }
  assert(vocabularyRejected);

#ifdef FELIDAE_HAS_TORCH
  // Capture the parser-bounded input and a verifier-safe structured teacher
  // from one genuinely unresolved source expression. The trained GRU must
  // later reproduce this sequence itself; the teacher is not used during
  // the production compilation below.
  class CapturingCompilerTeacher final : public MixfixStateModel {
  public:
    std::vector<SentencePieceId> boundedInput;
    std::vector<MixfixVocabularyId> target;

    std::vector<MixfixVocabularyId>
    transform(std::span<const SentencePieceId> inputIds,
              const MixfixContext &compilerContext) override {
      boundedInput.assign(inputIds.begin(), inputIds.end());
      const auto token = [&](MixfixIrTokenKind kind, IrWord value = 0) {
        const auto found = std::find_if(
            compilerContext.outputVocabulary.begin(),
            compilerContext.outputVocabulary.end(),
            [&](const MixfixIrToken &candidate) {
              return candidate.kind == kind && candidate.value == value;
            });
        if (found == compilerContext.outputVocabulary.end()) {
          throw IrError(
              "compiler E2E teacher token is outside the bounded vocabulary");
        }
        return static_cast<MixfixVocabularyId>(
            found - compilerContext.outputVocabulary.begin());
      };
      target = {
          token(MixfixIrTokenKind::Accept),
          token(MixfixIrTokenKind::Opcode,
                static_cast<IrWord>(IrOpcode::LoadSymbol)),
          token(MixfixIrTokenKind::Register, 1),
          token(MixfixIrTokenKind::SymbolReference, 0),
          token(MixfixIrTokenKind::Opcode, static_cast<IrWord>(IrOpcode::Call)),
          token(MixfixIrTokenKind::Register, 0),
          token(MixfixIrTokenKind::SymbolReference, 1),
          token(MixfixIrTokenKind::Register, 1),
          token(MixfixIrTokenKind::Register, 1),
          token(MixfixIrTokenKind::Opcode,
                static_cast<IrWord>(IrOpcode::Return)),
          token(MixfixIrTokenKind::Register, 0),
          token(MixfixIrTokenKind::Register, 0),
          token(MixfixIrTokenKind::End),
      };
      return target;
    }
  } compilerTeacher;
  const std::string compilerE2eSource =
      "@overload(\n"
      "    operator: pickValue,\n"
      "    pattern: \"pick {value}\",\n"
      "    type: prefix,\n"
      "    captures: {value: number},\n"
      "    result: number,\n"
      "    precedence: prefix,\n"
      "    associativity: right,\n"
      "    cardinality: one,\n"
      "    effects: pure,\n"
      "    visibility: private\n"
      ")\n"
      "pickNumber() => return value\n"
      "@overload(\n"
      "    operator: pickValue,\n"
      "    pattern: \"pick {value}\",\n"
      "    type: prefix,\n"
      "    captures: {value: string},\n"
      "    result: number,\n"
      "    precedence: prefix,\n"
      "    associativity: right,\n"
      "    cardinality: one,\n"
      "    effects: pure,\n"
      "    visibility: private\n"
      ")\n"
      "pickString() => return 0\n"
      "route(value: any) => return pick value\n"
      "main() => return route(value: 42)\n";
  CompilerOptions teacherOptions;
  teacherOptions.mixfixModel = &compilerTeacher;
  (void)compileProgramTextToIr(compilerE2eSource, teacherOptions);
  assert(!compilerTeacher.boundedInput.empty() &&
         !compilerTeacher.target.empty());

  GruMixfixStateModel::Configuration trainedCompilerConfiguration;
  trainedCompilerConfiguration.inputVocabularySize =
      felidaeSentencePieceModel().GetPieceSize();
  trainedCompilerConfiguration.outputVocabularySize =
      static_cast<std::int64_t>(kMixfixStructuralVocabularySize);
  trainedCompilerConfiguration.embeddingSize = 16;
  trainedCompilerConfiguration.hiddenSize = 24;
  trainedCompilerConfiguration.layerCount = 1;
  trainedCompilerConfiguration.beginToken = 0;
  trainedCompilerConfiguration.maximumDecodeSteps =
      compilerTeacher.target.size() + 2;
  trainedCompilerConfiguration.allowRandomInitialization = true;
  GruMixfixStateModel trainedCompiler(trainedCompilerConfiguration);
  std::vector<std::int64_t> compilerTarget(compilerTeacher.target.begin(),
                                           compilerTeacher.target.end());
  bool compilerSequenceLearned = false;
  for (std::size_t step = 0; step < 512 && !compilerSequenceLearned; ++step) {
    (void)trainedCompiler.trainTeacherForced(compilerTeacher.boundedInput,
                                             compilerTarget, 0.02);
    try {
      const auto decoded = trainedCompiler.transform(
          compilerTeacher.boundedInput, makeMixfixContext(FelidaeIr{}));
      compilerSequenceLearned = decoded == compilerTeacher.target;
    } catch (const IrError &) {
      // Early autoregressive outputs are expected to be structurally
      // invalid until this one bounded teacher sequence is learned.
    }
  }
  assert(compilerSequenceLearned);
  const auto trainedCompilerDirectory =
      std::filesystem::path(FELIDAE_MODEL_TEST_OUTPUT_DIR) /
      "compiler-ssm-e2e";
  std::filesystem::create_directories(trainedCompilerDirectory);
  const auto trainedCompilerArtifact =
      trainedCompilerDirectory / "mixfix-gru.pt";
  trainedCompiler.exportTorchScript(trainedCompilerArtifact);
  writeMixfixManifest(trainedCompilerDirectory, trainedCompilerConfiguration,
                      felidaeSentencePieceModelIdentity());
  auto reloadedCompiler = GruMixfixStateModel::loadVersioned(
      trainedCompilerConfiguration, trainedCompilerDirectory,
      felidaeSentencePieceModelIdentity());
  CompilerOptions productionCompilerOptions;
  productionCompilerOptions.mixfixModel = &reloadedCompiler;
  const auto trainedCompilerIr =
      compileProgramTextToIr(compilerE2eSource, productionCompilerOptions);
  const auto trainedCompilerBinary =
      std::filesystem::path(FELIDAE_MODEL_TEST_OUTPUT_DIR) /
      "compiler-ssm-e2e.bin";
  writeBinaryIr(trainedCompilerBinary,
                verifyIrModule(IrModule(trainedCompilerIr)));
  const auto loadedCompilerBinary =
      loadBinaryIr(trainedCompilerBinary, felidaeSentencePieceModelIdentity());
  FelidaeKnowledgeRuntime compilerE2eRuntime;
  const auto compilerE2eResult =
      RegisterVm{}.executeMain(loadedCompilerBinary, compilerE2eRuntime);
  assert(std::holds_alternative<double>(compilerE2eResult));
  assert(std::get<double>(compilerE2eResult) == 42.0);

  // A one-entry finite vocabulary makes autoregressive GRU decoding
  // deterministic while still exercising the actual LibTorch encoder,
  // recurrent state transfer, decoder, and projection path.
  GruMixfixStateModel::Configuration gruConfiguration;
  gruConfiguration.inputVocabularySize = 8;
  gruConfiguration.outputVocabularySize = 1;
  gruConfiguration.embeddingSize = 4;
  gruConfiguration.hiddenSize = 4;
  gruConfiguration.layerCount = 1;
  gruConfiguration.beginToken = 0;
  gruConfiguration.maximumDecodeSteps = 2;
  gruConfiguration.allowRandomInitialization = true;
  GruMixfixStateModel gruModel(gruConfiguration);
  MixfixContext gruContext;
  gruContext.maximumOutputWords = 2;
  gruContext.outputVocabulary = {{MixfixIrTokenKind::Reject, 0}};
  const auto gruTokens = gruModel.transform(input, gruContext);
  assert((gruTokens == std::vector<MixfixVocabularyId>{0}));
  MixfixContext boundedGruContext;
  boundedGruContext.maximumOutputWords = 2;
  boundedGruContext.outputVocabulary = {{MixfixIrTokenKind::Opcode, 1}};
  assert(gruModel.transform(input, boundedGruContext).size() == 2);

  // The VM recurrent backend has one shared finite output vocabulary for
  // both model loading and C++ training. It retains bounded references to
  // every supported input position rather than silently limiting models to
  // argument zero.
  const auto runtimeVocabulary = defaultRuntimeOutputVocabulary();
  assert(runtimeVocabulary.size() == 3 + 2 * kRuntimeModelReferenceLimit + 5);
  assert(runtimeVocabulary[3].kind == RuntimeOutputTokenKind::InputReference &&
         runtimeVocabulary[3].value == 0);
  assert(runtimeVocabulary[3 + kRuntimeModelReferenceLimit - 1].kind ==
             RuntimeOutputTokenKind::InputReference &&
         runtimeVocabulary[3 + kRuntimeModelReferenceLimit - 1].value ==
             kRuntimeModelReferenceLimit - 1);
  assert(runtimeVocabulary[3 + kRuntimeModelReferenceLimit].kind ==
             RuntimeOutputTokenKind::FactFromInput &&
         runtimeVocabulary[3 + kRuntimeModelReferenceLimit].value == 0);

  // The VM recurrent backend has its own finite output vocabulary.  A
  // reference token returns the original typed Value, so state-model use
  // never invents boolean truthiness or loses fact/text/container payloads.
  GruRuntimeStateModel::Configuration runtimeGruConfiguration;
  runtimeGruConfiguration.inputVocabularySize =
      felidaeSentencePieceModel().GetPieceSize() +
      kRuntimeStructuralInputTokens;
  runtimeGruConfiguration.outputVocabularySize = 1;
  runtimeGruConfiguration.embeddingSize = 4;
  runtimeGruConfiguration.hiddenSize = 4;
  runtimeGruConfiguration.layerCount = 1;
  runtimeGruConfiguration.allowRandomInitialization = true;
  GruRuntimeStateModel runtimeGru(
      runtimeGruConfiguration, {{RuntimeOutputTokenKind::InputReference, 0}});
  RuntimeContext runtimeGruContext;
  runtimeGruContext.executionState = runtimeGru.createExecutionState();
  const VmValue runtimeText = VmText{{101, 102, 103}};
  assert(std::get<VmText>(
             runtimeGru.evaluate(RuntimeOperation{static_cast<std::uint16_t>(
                                     SemanticOperationId::Identity)},
                                 {&runtimeText, 1}, runtimeGruContext))
             .pieces == PieceSequence({101, 102, 103}));
  auto runtimeFact = std::make_shared<VmFact>();
  runtimeFact->type = 42;
  const VmValue runtimeFactValue = runtimeFact;
  assert(std::get<VmFactPtr>(
             runtimeGru.evaluate(RuntimeOperation{static_cast<std::uint16_t>(
                                     SemanticOperationId::SelectFact)},
                                 {&runtimeFactValue, 1}, runtimeGruContext))
             ->type == 42);
  GruRuntimeStateModel softRuntimeGru(
      runtimeGruConfiguration, {{RuntimeOutputTokenKind::DegreeMilli, 725}});
  RuntimeContext softRuntimeContext;
  softRuntimeContext.executionState = softRuntimeGru.createExecutionState();
  const VmValue degreeInput = 0.5;
  const auto softResult =
      softRuntimeGru.evaluate(RuntimeOperation{static_cast<std::uint16_t>(
                                  SemanticOperationId::EvaluateDegree)},
                              {&degreeInput, 1}, softRuntimeContext);
  assert(std::get<VmDegree>(softResult).value == 0.725);

  // Production artifacts are genuine TorchScript modules. Native training
  // checkpoints remain separate and must not load through the inference
  // constructor.
  const auto artifactRoot =
      std::filesystem::path(FELIDAE_MODEL_TEST_OUTPUT_DIR) /
      "artifact-contract";
  std::filesystem::create_directories(artifactRoot);
  const auto mixfixArtifact = artifactRoot / "mixfix-gru.pt";
  const auto mixfixCheckpoint = artifactRoot / "mixfix-gru.ckpt";
  gruModel.saveCheckpoint(mixfixCheckpoint);
  gruModel.exportTorchScript(mixfixArtifact);
  writeMixfixManifest(artifactRoot, gruConfiguration,
                      felidaeSentencePieceModelIdentity());
  auto loadedMixfix = GruMixfixStateModel::loadVersioned(
      gruConfiguration, artifactRoot, felidaeSentencePieceModelIdentity());
  assert((loadedMixfix.transform(input, gruContext) ==
          std::vector<MixfixVocabularyId>{0}));
  assert(loadedMixfix.transform(input, boundedGruContext).size() == 2);
  writeMixfixManifest(artifactRoot, gruConfiguration,
                      felidaeSentencePieceModelIdentity(),
                      "incompatible-version");
  assert(rejects([&] {
    (void)GruMixfixStateModel::loadVersioned(
        gruConfiguration, artifactRoot, felidaeSentencePieceModelIdentity());
  }));
  writeMixfixManifest(artifactRoot, gruConfiguration,
                      felidaeSentencePieceModelIdentity());
  bool productionCheckpointRejected = false;
  try {
    loadedMixfix.saveCheckpoint(artifactRoot / "production.ckpt");
  } catch (const IrError &) {
    productionCheckpointRejected = true;
  }
  assert(productionCheckpointRejected);
  std::filesystem::copy_file(mixfixCheckpoint, mixfixArtifact,
                             std::filesystem::copy_options::overwrite_existing);
  assert(rejects([&] {
    (void)GruMixfixStateModel::loadVersioned(
        gruConfiguration, artifactRoot, felidaeSentencePieceModelIdentity());
  }));
  gruModel.exportTorchScript(mixfixArtifact);

  const auto runtimeArtifact = artifactRoot / "runtime-gru.pt";
  const auto runtimeCheckpoint = artifactRoot / "runtime-gru.ckpt";
  runtimeGru.saveCheckpoint(runtimeCheckpoint);
  runtimeGru.exportTorchScript(runtimeArtifact,
                               felidaeSentencePieceModelIdentity());
  auto loadedRuntime = GruRuntimeStateModel::loadVersioned(
      runtimeGruConfiguration, {{RuntimeOutputTokenKind::InputReference, 0}},
      artifactRoot, felidaeSentencePieceModelIdentity());
  RuntimeContext loadedRuntimeContext;
  loadedRuntimeContext.executionState = loadedRuntime.createExecutionState();
  assert(std::get<VmText>(
             loadedRuntime.evaluate(RuntimeOperation{static_cast<std::uint16_t>(
                                        SemanticOperationId::Identity)},
                                    {&runtimeText, 1}, loadedRuntimeContext))
             .pieces == PieceSequence({101, 102, 103}));
  std::filesystem::copy_file(runtimeCheckpoint, runtimeArtifact,
                             std::filesystem::copy_options::overwrite_existing);
  assert(rejects([&] {
    (void)GruRuntimeStateModel::loadVersioned(
        runtimeGruConfiguration,
        {{RuntimeOutputTokenKind::InputReference, 0}}, artifactRoot,
        felidaeSentencePieceModelIdentity());
  }));
  std::filesystem::remove_all(artifactRoot);

  // Real compiler-produced SemanticEval -> verified FELBIR -> trained C++
  // runtime GRU -> TorchScript export/reload -> direct register VM. This
  // uses the full production output vocabulary and requires the optimizer
  // to learn InputReference(0); it is not a one-class random-model shortcut.
  const auto trainedRuntimeVocabulary = defaultRuntimeOutputVocabulary();
  const auto trainedTarget = static_cast<std::size_t>(
      std::find_if(
          trainedRuntimeVocabulary.begin(), trainedRuntimeVocabulary.end(),
          [](const RuntimeOutputToken &token) {
            return token.kind == RuntimeOutputTokenKind::InputReference &&
                   token.value == 0;
          }) -
      trainedRuntimeVocabulary.begin());
  assert(trainedTarget < trainedRuntimeVocabulary.size());
  GruRuntimeStateModel::Configuration trainedRuntimeConfiguration;
  trainedRuntimeConfiguration.inputVocabularySize =
      felidaeSentencePieceModel().GetPieceSize() +
      kRuntimeStructuralInputTokens;
  trainedRuntimeConfiguration.outputVocabularySize =
      static_cast<std::int64_t>(trainedRuntimeVocabulary.size());
  trainedRuntimeConfiguration.embeddingSize = 8;
  trainedRuntimeConfiguration.hiddenSize = 8;
  trainedRuntimeConfiguration.layerCount = 1;
  trainedRuntimeConfiguration.allowRandomInitialization = true;
  GruRuntimeStateModel trainedRuntime(trainedRuntimeConfiguration,
                                      trainedRuntimeVocabulary);
  RuntimeTrainingRecord identityTeacher;
  identityTeacher.operationId =
      static_cast<std::uint16_t>(SemanticOperationId::Identity);
  identityTeacher.inputKinds = {RuntimeValueKind::Number};
  identityTeacher.targetKind = RuntimeTrainingTargetKind::InputReference;
  identityTeacher.targetValue = 0;
  verifyRuntimeTrainingRecord(identityTeacher);
  auto serializedTeacher = identityTeacher;
  serializedTeacher.factTypes = {{10, 11}};
  serializedTeacher.factTypeCounts = {{{10, 11}, 2}};
  serializedTeacher.hierarchyEdges = {{{10, 11}, {12}}};
  const auto runtimeDataset =
      std::filesystem::path(FELIDAE_MODEL_TEST_OUTPUT_DIR) /
      "runtime-sequence-roundtrip.jsonl";
  writeRuntimeTrainingDataset(runtimeDataset, {&serializedTeacher, 1});
  const auto loadedTeachers = loadRuntimeTrainingDataset(runtimeDataset);
  assert(loadedTeachers.size() == 1);
  assert(loadedTeachers.front().factTypes == serializedTeacher.factTypes);
  assert(loadedTeachers.front().factTypeCounts ==
         serializedTeacher.factTypeCounts);
  assert(loadedTeachers.front().hierarchyEdges ==
         serializedTeacher.hierarchyEdges);
  std::filesystem::remove(runtimeDataset);
  auto invalidTeacher = identityTeacher;
  invalidTeacher.operationId = 0xffffu;
  bool invalidTeacherRejected = false;
  try {
    verifyRuntimeTrainingRecord(invalidTeacher);
  } catch (const IrError &) {
    invalidTeacherRejected = true;
  }
  assert(invalidTeacherRejected);
  invalidTeacher = identityTeacher;
  invalidTeacher.inputKinds.clear();
  bool invalidTeacherArityRejected = false;
  try {
    verifyRuntimeTrainingRecord(invalidTeacher);
  } catch (const IrError &) {
    invalidTeacherArityRejected = true;
  }
  assert(invalidTeacherArityRejected);
  (void)trainedRuntime.trainTeacherForced(identityTeacher, trainedTarget, 0.01);
  for (std::size_t step = 1;
       step < 128 &&
       trainedRuntime.predictTeacherToken(identityTeacher) != trainedTarget;
       ++step) {
    (void)trainedRuntime.trainTeacherForced(identityTeacher, trainedTarget,
                                            0.01);
  }
  assert(trainedRuntime.predictTeacherToken(identityTeacher) == trainedTarget);
  const auto trainedArtifactDirectory =
      std::filesystem::path(FELIDAE_MODEL_TEST_OUTPUT_DIR) / "runtime-ssm-e2e";
  std::filesystem::create_directories(trainedArtifactDirectory);
  const auto trainedArtifact = trainedArtifactDirectory / "runtime-gru.pt";
  trainedRuntime.exportTorchScript(trainedArtifact,
                                   felidaeSentencePieceModelIdentity());
  auto reloadedTrainedRuntime = GruRuntimeStateModel::loadVersioned(
      trainedRuntimeConfiguration, trainedRuntimeVocabulary,
      trainedArtifactDirectory, felidaeSentencePieceModelIdentity());
  const auto trainedRuntimeModule = loadBinaryIr(
      FELIDAE_RUNTIME_SSM_E2E_BINARY, felidaeSentencePieceModelIdentity());
  assert(containsRuntimeSemanticOperation(trainedRuntimeModule));
  FelidaeKnowledgeRuntime trainedKnowledgeRuntime(&reloadedTrainedRuntime);
  const auto trainedResult =
      RegisterVm{}.executeMain(trainedRuntimeModule, trainedKnowledgeRuntime);
  assert(std::holds_alternative<double>(trainedResult));
  assert(std::get<double>(trainedResult) == 42.0);
#endif

  FelidaeIr arithmetic;
  arithmetic.registerCount = 3;
  arithmetic.constants = {{IrConstantKind::Number, encodeIrNumber(6.0)},
                          {IrConstantKind::Number, encodeIrNumber(7.0)}};
  arithmetic.words = {
      static_cast<IrWord>(IrOpcode::LoadConst),
      0,
      0,
      static_cast<IrWord>(IrOpcode::LoadConst),
      1,
      1,
      static_cast<IrWord>(IrOpcode::Mul),
      2,
      0,
      1,
      static_cast<IrWord>(IrOpcode::Return),
      2,
      0,
      static_cast<IrWord>(IrOpcode::End),
  };
  class NoRuntime : public VmRuntime {
  public:
    void installIrModule(const IrModule &) override {}
    IrSymbolRef resolveSymbol(IrSymbolRef symbol) const override {
      return symbol;
    }
    VmFactPtr retainFact(const VmFactPtr &fact) override { return fact; }
    VmFactPtr mutateFact(const VmFactPtr &fact, IrSymbolRef field,
                         const VmValue &value) override {
      if (!fact)
        throw IrError("test mutation requires a fact");
      auto updated = std::make_shared<VmFact>(*fact);
      const auto found =
          std::find_if(updated->fields.begin(), updated->fields.end(),
                       [&](const auto &entry) { return entry.first == field; });
      if (found == updated->fields.end())
        updated->fields.emplace_back(field, value);
      else
        found->second = value;
      return updated;
    }
  } noRuntime;
  const auto arithmeticResult = executeDirect(arithmetic, noRuntime);
  assert(std::get<double>(arithmeticResult) == 42.0);
  // The branch protocol is deliberately narrower than host-language
  // truthiness. Every non-boolean VM type remains usable data and must not
  // become an implicit control decision.
  assert(noRuntime.shouldBranchFalse(VmNil{}));
  assert(noRuntime.shouldBranchFalse(0.0));
  assert(!noRuntime.shouldBranchFalse(1.0));
  assert(rejectsBranchValue(noRuntime, 2.0));
  assert(rejectsBranchValue(noRuntime, VmDegree(0.0)));
  assert(rejectsBranchValue(noRuntime, VmText{{101}}));
  assert(rejectsBranchValue(noRuntime, std::make_shared<VmArray>()));
  assert(rejectsBranchValue(noRuntime, std::make_shared<VmMap>()));
  assert(rejectsBranchValue(noRuntime, std::make_shared<VmFact>()));

  FelidaeIr factProgram;
  factProgram.registerCount = 2;
  factProgram.symbols = {50, 51, 12};
  factProgram.constants = {{IrConstantKind::Number, encodeIrNumber(12.0)}};
  factProgram.words = {
      static_cast<IrWord>(IrOpcode::MakeFact),
      0,
      0,
      static_cast<IrWord>(IrOpcode::LoadConst),
      1,
      0,
      static_cast<IrWord>(IrOpcode::SetField),
      0,
      1,
      1,
      static_cast<IrWord>(IrOpcode::Return),
      0,
      0,
      static_cast<IrWord>(IrOpcode::End),
  };
  const auto factValue = executeDirect(factProgram, noRuntime);
  const auto fact = std::get<VmFactPtr>(factValue);
  assert(fact->type == 50 && fact->fields.size() == 1);
  assert(fact->fields.front().first == 51);
  assert(std::get<double>(fact->fields.front().second) == 12.0);

  class SymbolRuntime final : public NoRuntime {
  public:
    VmValue loadSymbol(IrSymbolRef symbol) override {
      return values.at(symbol);
    }
    void storeSymbol(IrSymbolRef symbol, const VmValue &value) override {
      values[symbol] = value;
    }
    std::unordered_map<IrSymbolRef, VmValue> values;
  } symbolRuntime;
  FelidaeIr symbolStore;
  symbolStore.registerCount = 1;
  symbolStore.symbols = {7};
  symbolStore.constants = {{IrConstantKind::Number, encodeIrNumber(9.0)}};
  symbolStore.words = {
      static_cast<IrWord>(IrOpcode::LoadConst),   0, 0,
      static_cast<IrWord>(IrOpcode::StoreSymbol), 0, 0,
      static_cast<IrWord>(IrOpcode::LoadSymbol),  0, 0,
      static_cast<IrWord>(IrOpcode::Return),      0, 0,
      static_cast<IrWord>(IrOpcode::End),
  };
  assert(std::get<double>(executeDirect(symbolStore, symbolRuntime)) == 9.0);

  FelidaeIr directProcedure;
  directProcedure.registerCount = 1;
  directProcedure.constants = {{IrConstantKind::Number, encodeIrNumber(7.0)}};
  directProcedure.words = {
      static_cast<IrWord>(IrOpcode::LoadConst), 0, 0,
      static_cast<IrWord>(IrOpcode::Return),    0, 0,
      static_cast<IrWord>(IrOpcode::End),
  };
  FelidaeIr directCaller;
  directCaller.registerCount = 1;
  directCaller.symbols = {99};
  directCaller.words = {
      static_cast<IrWord>(IrOpcode::Call),
      0,
      0,
      0,
      static_cast<IrWord>(IrOpcode::Return),
      0,
      0,
      static_cast<IrWord>(IrOpcode::End),
  };
  FelidaeKnowledgeRuntime procedureRuntime;
  assert(std::get<double>(executeDirect(
             directCaller, procedureRuntime,
             {{99, IrProcedure{directProcedure, {}, {}, {}}}})) == 7.0);
  FelidaeIr recursiveProcedure;
  recursiveProcedure.registerCount = 1;
  recursiveProcedure.symbols = {77};
  recursiveProcedure.words = {
      static_cast<IrWord>(IrOpcode::Call),
      0,
      0,
      0,
      static_cast<IrWord>(IrOpcode::Return),
      0,
      0,
      static_cast<IrWord>(IrOpcode::End),
  };
  FelidaeIr recursiveCaller = directCaller;
  recursiveCaller.symbols = {77};
  FelidaeKnowledgeRuntime boundedRecursionRuntime(nullptr, 1024, 2);
  bool recursionRejected = false;
  try {
    (void)executeDirect(recursiveCaller, boundedRecursionRuntime,
                        {{77, IrProcedure{recursiveProcedure, {}, {}, {}}}});
  } catch (const IrError &) {
    recursionRejected = true;
  }
  assert(recursionRejected);

  class StatefulSemanticModel final : public RuntimeStateModel {
  public:
    std::shared_ptr<void> createExecutionState() override {
      return std::make_shared<std::size_t>(0);
    }
    VmValue evaluate(const RuntimeOperation &operation,
                     std::span<const VmValue> inputs,
                     RuntimeContext &context) override {
      assert(operation.id ==
                 static_cast<std::uint16_t>(SemanticOperationId::Identity) &&
             inputs.size() == 1);
      auto state =
          std::static_pointer_cast<std::size_t>(context.executionState);
      // Generic test runtimes may not opt into the production lifecycle;
      // they still receive an execution-local context and never persist
      // this fallback state beyond this RegisterVm invocation.
      if (!state) {
        state = std::make_shared<std::size_t>(0);
        context.executionState = state;
      }
      ++*state;
      return std::get<double>(inputs.front()) + static_cast<double>(*state);
    }
  } semanticModel;
  class SemanticRuntime final : public NoRuntime {
  public:
    explicit SemanticRuntime(RuntimeStateModel &model) : model_(model) {}
    RuntimeStateModel *runtimeStateModel() override { return &model_; }
    RuntimeContext makeRuntimeContext(const VmValue &) const override {
      RuntimeContext context;
      context.maximumSemanticSteps = 2;
      return context;
    }

  private:
    RuntimeStateModel &model_;
  } semanticRuntime(semanticModel);
  FelidaeIr semanticProgram;
  semanticProgram.registerCount = 2;
  semanticProgram.constants = {{IrConstantKind::Number, encodeIrNumber(4.0)}};
  semanticProgram.words = {
      static_cast<IrWord>(IrOpcode::LoadConst),
      0,
      0,
      static_cast<IrWord>(IrOpcode::SemanticEval),
      1,
      static_cast<IrWord>(SemanticOperationId::Identity),
      1,
      0,
      static_cast<IrWord>(IrOpcode::Return),
      1,
      0,
      static_cast<IrWord>(IrOpcode::End),
  };
  assert(std::get<double>(executeDirect(semanticProgram, semanticRuntime)) ==
         5.0);
  // A new VM execution owns a new RuntimeContext; recurrent state cannot
  // leak across requests even when the backend instance is reused.
  assert(std::get<double>(executeDirect(semanticProgram, semanticRuntime)) ==
         5.0);
  FelidaeKnowledgeRuntime directSemanticRuntime(&semanticModel);
  assert(std::get<double>(
             executeDirect(semanticProgram, directSemanticRuntime)) == 5.0);
  // Facts asserted earlier in the same VM program are refreshed into the
  // learned-operation context; a model never trains on a snapshot it cannot
  // observe during production execution.
  class KnowledgeCheckingModel final : public RuntimeStateModel {
  public:
    VmValue evaluate(const RuntimeOperation &operation,
                     std::span<const VmValue> inputs,
                     RuntimeContext &context) override {
      assert(operation.id ==
                 static_cast<std::uint16_t>(SemanticOperationId::Identity) &&
             inputs.size() == 1);
      assert(context.knowledge.factTypes.size() == 1 &&
             context.knowledge.factTypes[0] == 77);
      assert(context.knowledge.factTypeCounts.size() == 1 &&
             context.knowledge.factTypeCounts[0].first == 77 &&
             context.knowledge.factTypeCounts[0].second == 1);
      assert(std::holds_alternative<VmFactPtr>(inputs.front()));
      return inputs.front();
    }
  } knowledgeModel;
  FelidaeIr knowledgeProgram;
  knowledgeProgram.registerCount = 2;
  knowledgeProgram.symbols = {77};
  knowledgeProgram.words = {
      static_cast<IrWord>(IrOpcode::MakeFact),
      0,
      0,
      static_cast<IrWord>(IrOpcode::SemanticEval),
      1,
      static_cast<IrWord>(SemanticOperationId::Identity),
      1,
      0,
      static_cast<IrWord>(IrOpcode::Return),
      1,
      0,
      static_cast<IrWord>(IrOpcode::End),
  };
  FelidaeKnowledgeRuntime knowledgeRuntime(&knowledgeModel);
  assert(std::holds_alternative<VmFactPtr>(
      executeDirect(knowledgeProgram, knowledgeRuntime)));
  // Nested calls are part of one top-level execution: the SSM state and
  // semantic budget are shared across their fresh register frames.
  FelidaeIr semanticCaller;
  semanticCaller.registerCount = 2;
  semanticCaller.symbols = {99};
  semanticCaller.words = {
      static_cast<IrWord>(IrOpcode::Call),
      0,
      0,
      0,
      static_cast<IrWord>(IrOpcode::Call),
      1,
      0,
      0,
      static_cast<IrWord>(IrOpcode::Return),
      1,
      0,
      static_cast<IrWord>(IrOpcode::End),
  };
  FelidaeKnowledgeRuntime nestedSemanticRuntime(&semanticModel, 2);
  assert(std::get<double>(executeDirect(
             semanticCaller, nestedSemanticRuntime,
             {{99, IrProcedure{semanticProgram, {}, {}, {}}}})) == 6.0);
  FelidaeIr overLimitSemanticProgram;
  overLimitSemanticProgram.registerCount = 4;
  overLimitSemanticProgram.constants = {
      {IrConstantKind::Number, encodeIrNumber(1.0)}};
  overLimitSemanticProgram.words = {
      static_cast<IrWord>(IrOpcode::LoadConst),
      0,
      0,
      static_cast<IrWord>(IrOpcode::SemanticEval),
      1,
      static_cast<IrWord>(SemanticOperationId::Identity),
      1,
      0,
      static_cast<IrWord>(IrOpcode::SemanticEval),
      2,
      static_cast<IrWord>(SemanticOperationId::Identity),
      1,
      1,
      static_cast<IrWord>(IrOpcode::SemanticEval),
      3,
      static_cast<IrWord>(SemanticOperationId::Identity),
      1,
      2,
      static_cast<IrWord>(IrOpcode::Return),
      3,
      0,
      static_cast<IrWord>(IrOpcode::End),
  };
  bool semanticLimitRejected = false;
  try {
    (void)executeDirect(overLimitSemanticProgram, semanticRuntime);
  } catch (const IrError &) {
    semanticLimitRejected = true;
  }
  assert(semanticLimitRejected);
  FelidaeKnowledgeRuntime oneStepSemanticRuntime(&semanticModel, 1);
  bool directSemanticLimitRejected = false;
  try {
    (void)executeDirect(overLimitSemanticProgram, oneStepSemanticRuntime);
  } catch (const IrError &) {
    directSemanticLimitRejected = true;
  }
  assert(directSemanticLimitRejected);
  bool unavailableSemanticRejected = false;
  try {
    (void)executeDirect(semanticProgram, noRuntime);
  } catch (const IrError &) {
    unavailableSemanticRejected = true;
  }
  assert(unavailableSemanticRejected);
  class InvalidSemanticModel final : public RuntimeStateModel {
  public:
    VmValue evaluate(const RuntimeOperation &, std::span<const VmValue>,
                     RuntimeContext &) override {
      return VmMapPtr{};
    }
  } invalidSemanticModel;
  SemanticRuntime invalidSemanticRuntime(invalidSemanticModel);
  bool invalidSemanticRejected = false;
  try {
    (void)executeDirect(semanticProgram, invalidSemanticRuntime);
  } catch (const IrError &) {
    invalidSemanticRejected = true;
  }
  assert(invalidSemanticRejected);
  class WrongIdentityTypeModel final : public RuntimeStateModel {
  public:
    VmValue evaluate(const RuntimeOperation &, std::span<const VmValue>,
                     RuntimeContext &) override {
      return VmText{{101, 102}};
    }
  } wrongIdentityTypeModel;
  SemanticRuntime wrongIdentityRuntime(wrongIdentityTypeModel);
  bool wrongIdentityTypeRejected = false;
  try {
    (void)executeDirect(semanticProgram, wrongIdentityRuntime);
  } catch (const IrError &) {
    wrongIdentityTypeRejected = true;
  }
  assert(wrongIdentityTypeRejected);

  FelidaeIr branchSkipsInitialization;
  branchSkipsInitialization.registerCount = 2;
  branchSkipsInitialization.constants = {{IrConstantKind::Boolean, 1}};
  branchSkipsInitialization.words = {
      static_cast<IrWord>(IrOpcode::LoadConst),   0, 0,
      static_cast<IrWord>(IrOpcode::JumpIfFalse), 0, 9,
      static_cast<IrWord>(IrOpcode::LoadConst),   1, 0,
      static_cast<IrWord>(IrOpcode::Return),      1, 0,
      static_cast<IrWord>(IrOpcode::End),
  };
  bool flowRejected = false;
  try {
    IrVerifier::verify(branchSkipsInitialization);
  } catch (const IrError &) {
    flowRejected = true;
  }
  assert(flowRejected);

  FelidaeIr overflowingCall;
  overflowingCall.registerCount = 1;
  overflowingCall.symbols = {0};
  overflowingCall.words = {static_cast<IrWord>(IrOpcode::Call), 0, 0,
                           std::numeric_limits<IrWord>::max()};
  bool overflowRejected = false;
  try {
    IrVerifier::verify(overflowingCall);
  } catch (const IrError &) {
    overflowRejected = true;
  }
  assert(overflowRejected);

  FelidaeIr invalidSemanticOperation;
  invalidSemanticOperation.registerCount = 1;
  invalidSemanticOperation.words = {
      static_cast<IrWord>(IrOpcode::SemanticEval),
      0,
      0xffffu,
      0,
      static_cast<IrWord>(IrOpcode::Return),
      0,
      0,
      static_cast<IrWord>(IrOpcode::End),
  };
  bool invalidSemanticOperationRejected = false;
  try {
    IrVerifier::verify(invalidSemanticOperation);
  } catch (const IrError &) {
    invalidSemanticOperationRejected = true;
  }
  assert(invalidSemanticOperationRejected);

  FelidaeIr overflowingSemanticOperation;
  overflowingSemanticOperation.registerCount = 1;
  overflowingSemanticOperation.symbols = {1};
  overflowingSemanticOperation.words = {
      static_cast<IrWord>(IrOpcode::SemanticEval),
      0,
      0,
      std::numeric_limits<IrWord>::max(),
  };
  bool overflowingSemanticRejected = false;
  try {
    IrVerifier::verify(overflowingSemanticOperation);
  } catch (const IrError &) {
    overflowingSemanticRejected = true;
  }
  assert(overflowingSemanticRejected);

  FelidaeIr badSourceMap = arithmetic;
  badSourceMap.sourceMap = {IrSourceMapEntry{1, {1, 1, 1, 2}}};
  bool sourceMapRejected = false;
  try {
    IrVerifier::verify(badSourceMap);
  } catch (const IrError &) {
    sourceMapRejected = true;
  }
  assert(sourceMapRejected);

  bool rejected = false;
  try {
    const std::vector<MixfixIrToken> noEnd = {
        {MixfixIrTokenKind::Opcode, kIrOpcodeCount}};
    (void)resolveMixfixIrTokens(noEnd, context);
  } catch (const IrError &) {
    rejected = true;
  }
  assert(rejected);
}
