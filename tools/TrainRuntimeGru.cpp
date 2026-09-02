#include "SentencePieceModel.h"
#include "form/RuntimeStateModel.h"
#include "form/RuntimeTraining.h"
#include "form/RuntimeTrainingCommand.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <random>
#include <tuple>

#include <torch/torch.h>

namespace {
using namespace Felidae;

std::size_t targetFor(const RuntimeTrainingRecord &record,
                      const std::vector<RuntimeOutputToken> &vocabulary) {
  const auto matches = [&](const RuntimeOutputToken &token) {
    switch (record.targetKind) {
    case RuntimeTrainingTargetKind::InputReference:
      return token.kind == RuntimeOutputTokenKind::InputReference;
    case RuntimeTrainingTargetKind::FactFromInput:
      return token.kind == RuntimeOutputTokenKind::FactFromInput;
    case RuntimeTrainingTargetKind::DegreeMilli:
      return token.kind == RuntimeOutputTokenKind::DegreeMilli;
    case RuntimeTrainingTargetKind::Nil:
      return token.kind == RuntimeOutputTokenKind::Nil;
    case RuntimeTrainingTargetKind::NumericTruth:
      return token.kind == RuntimeOutputTokenKind::NumericTruth;
    case RuntimeTrainingTargetKind::Score:
      return false;
    }
    return false;
  };
  for (std::size_t i = 0; i < vocabulary.size(); ++i) {
    const auto &token = vocabulary[i];
    if (matches(token) && token.value == record.targetValue)
      return i;
  }
  throw IrError(
      "runtime dataset result has no permitted finite GRU action target");
}
} // namespace

int Felidae::runRuntimeGruTraining(int argc, char **argv) {
  try {
    if (argc < 3 || argc > 5) {
      throw std::runtime_error(
          "internal error: VM training arguments are invalid");
    }
    const auto records = Felidae::loadRuntimeTrainingDataset(argv[1]);
    if (records.size() < 10)
      throw Felidae::IrError(
          "runtime training dataset requires at least ten records");
    const std::size_t epochs = argc >= 4 ? std::stoull(argv[3]) : 8;
    const double rate = argc == 5 ? std::stod(argv[4]) : 0.001;
    if (epochs == 0 || rate <= 0.0)
      throw Felidae::IrError("runtime training options are invalid");
    auto vocabulary = Felidae::defaultRuntimeOutputVocabulary();
    Felidae::GruRuntimeStateModel::Configuration configuration;
    configuration.inputVocabularySize =
        felidaeSentencePieceModel().GetPieceSize() +
        kRuntimeStructuralInputTokens;
    configuration.outputVocabularySize =
        static_cast<std::int64_t>(vocabulary.size());
    configuration.allowRandomInitialization = true;
    // Explicit CPU-first reproducibility: data ordering plus this seed
    // makes a repeated C++ training run comparable before benchmarking.
    torch::manual_seed(0);
    Felidae::GruRuntimeStateModel model(configuration, vocabulary);
    std::mt19937_64 random(0);
    // Split whole operation/input-shape/action families. A random
    // record-level split lets repeated context templates into multiple
    // partitions and therefore overstates generalization.
    using StructuralFamily =
        std::tuple<std::uint16_t, std::vector<RuntimeValueKind>, std::size_t>;
    using EvaluationBucket = std::pair<std::uint16_t, std::size_t>;
    std::map<StructuralFamily, std::vector<std::size_t>> families;
    for (std::size_t index = 0; index < records.size(); ++index) {
      families[{records[index].operationId, records[index].inputKinds,
                records[index].targetKind == RuntimeTrainingTargetKind::Score
                    ? vocabulary.size()
                    : targetFor(records[index], vocabulary)}]
          .push_back(index);
    }
    if (families.size() < 3)
      throw Felidae::IrError(
          "runtime dataset requires at least three operation/action families "
          "for train/validation/test splits");
    std::vector<std::vector<std::size_t> *> familyOrder;
    for (auto &[bucket, family] : families) {
      (void)bucket;
      familyOrder.push_back(&family);
    }
    std::shuffle(familyOrder.begin(), familyOrder.end(), random);
    std::vector<std::size_t> training, validation, test;
    for (std::size_t familyIndex = 0; familyIndex < familyOrder.size();
         ++familyIndex) {
      auto &destination = familyIndex % 10 == 0   ? test
                          : familyIndex % 10 == 1 ? validation
                                                  : training;
      destination.insert(destination.end(), familyOrder[familyIndex]->begin(),
                         familyOrder[familyIndex]->end());
    }
    if (training.empty() || validation.empty() || test.empty())
      throw Felidae::IrError(
          "runtime structural-family split produced an empty partition");
    std::cout << "records=" << records.size()
              << " structural_families=" << families.size()
              << " training=" << training.size()
              << " validation=" << validation.size() << " test=" << test.size()
              << "\n";
    for (std::size_t epoch = 0; epoch < epochs; ++epoch) {
      const auto epochStarted = std::chrono::steady_clock::now();
      std::shuffle(training.begin(), training.end(), random);
      double totalLoss = 0.0;
      for (const auto index : training) {
        const auto &record = records[index];
        totalLoss += record.targetKind == RuntimeTrainingTargetKind::Score
                         ? model.trainScore(record, rate)
                         : model.trainTeacherForced(
                               record, targetFor(record, vocabulary), rate);
      }
      std::size_t correct = 0;
      std::size_t validationClassifications = 0;
      std::size_t scoreCount = 0;
      double scoreAbsoluteError = 0.0;
      std::map<EvaluationBucket, std::pair<std::size_t, std::size_t>>
          bucketMetrics;
      for (const auto index : validation) {
        const auto &record = records[index];
        if (record.targetKind == RuntimeTrainingTargetKind::Score) {
          scoreAbsoluteError +=
              std::abs(model.predictScore(record) - record.targetScore);
          ++scoreCount;
          continue;
        }
        const auto target = targetFor(record, vocabulary);
        ++validationClassifications;
        const auto predicted = model.predictTeacherToken(record);
        auto &metric = bucketMetrics[{record.operationId, target}];
        ++metric.second;
        correct += predicted == target ? 1 : 0;
        if (predicted == target)
          ++metric.first;
      }
      const auto epochSeconds =
          std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                        epochStarted)
              .count();
      std::cout << "epoch=" << (epoch + 1)
                << " train_loss=" << (totalLoss / training.size())
                << " validation_accuracy="
                << (validationClassifications
                        ? static_cast<double>(correct) /
                              validationClassifications
                        : 0.0)
                << " validation_score_mae="
                << (scoreCount ? scoreAbsoluteError / scoreCount : 0.0)
                << " seconds=" << epochSeconds << " train_samples_per_second="
                << (epochSeconds > 0.0
                        ? static_cast<double>(training.size()) / epochSeconds
                        : 0.0)
                << "\n";
      for (const auto &[bucket, metric] : bucketMetrics) {
        std::cout << "  validation_operation=" << bucket.first
                  << " target_token=" << bucket.second
                  << " correct=" << metric.first << '/' << metric.second
                  << "\n";
      }
    }
    std::size_t testCorrect = 0;
    std::size_t testClassifications = 0;
    std::size_t testScores = 0;
    double testScoreAbsoluteError = 0.0;
    for (const auto index : test) {
      const auto &record = records[index];
      if (record.targetKind == RuntimeTrainingTargetKind::Score) {
        testScoreAbsoluteError +=
            std::abs(model.predictScore(record) - record.targetScore);
        ++testScores;
      } else {
        ++testClassifications;
        testCorrect +=
            model.predictTeacherToken(record) == targetFor(record, vocabulary)
                ? 1u
                : 0u;
      }
    }
    std::cout << "test_accuracy="
              << (testClassifications
                      ? static_cast<double>(testCorrect) / testClassifications
                      : 0.0)
              << " correct=" << testCorrect << '/' << testClassifications
              << " test_score_mae="
              << (testScores ? testScoreAbsoluteError / testScores : 0.0)
              << " score_records=" << testScores << "\n";
    const std::filesystem::path output(argv[2]);
    std::filesystem::create_directories(output);
    model.saveCheckpoint(output / "runtime-gru.ckpt");
    const auto sentencePieceIdentity = felidaeSentencePieceModelIdentity();
    model.exportTorchScript(output / "runtime-gru.pt", sentencePieceIdentity);
    // Treat export as successful only when the production manifest gate
    // can load these exact weights again.
    auto verified = Felidae::GruRuntimeStateModel::loadVersioned(
        configuration, vocabulary, output, sentencePieceIdentity);
    (void)verified;
    std::cout << std::filesystem::absolute(output / "runtime-gru.pt")
                     .lexically_normal()
                     .string()
              << "\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "error: " << error.what() << "\n";
    return 1;
  }
}
