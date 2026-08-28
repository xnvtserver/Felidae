#include "MixfixStateModel.h"
#include "MixfixTraining.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>
#include <torch/torch.h>

namespace {

std::vector<std::int64_t> ids(const nlohmann::json &record, const char *field) {
  if (!record.contains(field) || !record.at(field).is_array() ||
      record.at(field).empty()) {
    throw std::runtime_error(
        std::string("mixfix JSONL record requires non-empty ") + field);
  }
  std::vector<std::int64_t> result;
  for (const auto &value : record.at(field)) {
    if (!value.is_number_unsigned() ||
        value.get<std::uint64_t>() >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max())) {
      throw std::runtime_error(std::string("mixfix JSONL ") + field +
                               " must contain non-negative integer IDs");
    }
    result.push_back(static_cast<std::int64_t>(value.get<std::uint64_t>()));
  }
  return result;
}

std::vector<std::filesystem::path> datasetPaths(std::string_view encoded) {
  constexpr char separator = '\x1f';
  std::vector<std::filesystem::path> paths;
  std::size_t begin = 0;
  while (begin <= encoded.size()) {
    const auto end = encoded.find(separator, begin);
    const auto segment = encoded.substr(begin, end == std::string_view::npos
                                                   ? encoded.size() - begin
                                                   : end - begin);
    if (segment.empty())
      throw std::runtime_error(
          "mixfix training dataset list contains an empty path");
    paths.emplace_back(segment);
    if (end == std::string_view::npos)
      break;
    begin = end + 1;
  }
  return paths;
}

} // namespace

int Felidae::runMixfixGruTraining(int argc, char **argv) {
  // JSONL records: {"input_ids":[...],"target_ids":[...]}. The target
  // sequence includes the finite IR_END vocabulary token.
  if (argc != 9) {
    std::cerr << "internal error: compiler training arguments are invalid\n";
    return 2;
  }
  try {
    const auto dataPaths = datasetPaths(argv[1]);
    const auto outputDirectory = std::filesystem::path(argv[2]);
    for (const auto &dataPath : dataPaths) {
      if (dataPath.extension() != ".jsonl")
        throw std::runtime_error(
            "mixfix training dataset must use the .jsonl extension");
    }
    Felidae::GruMixfixStateModel::Configuration config;
    config.inputVocabularySize = std::stoll(argv[3]);
    config.outputVocabularySize = std::stoll(argv[4]);
    config.beginToken = std::stoll(argv[5]);
    config.allowRandomInitialization = true;
    const auto epochs = std::stoull(argv[6]);
    const auto learningRate = std::stod(argv[7]);
    if (config.inputVocabularySize <= 0 || config.outputVocabularySize <= 0 ||
        config.beginToken < 0 ||
        config.beginToken >= config.outputVocabularySize || epochs == 0 ||
        learningRate <= 0.0) {
      throw std::runtime_error("mixfix training configuration is invalid");
    }
    // The public compiler command obtains this value from its pinned
    // SentencePiece model before crossing into the isolated LibTorch
    // training library. Do not include generated Protobuf types here:
    // LibTorch and SentencePiece intentionally own separate dependencies.
    if (config.outputVocabularySize !=
        static_cast<std::int64_t>(Felidae::kMixfixStructuralVocabularySize)) {
      throw std::runtime_error(
          "mixfix output vocabulary must match the fixed structural IR "
          "vocabulary (expected " +
          std::to_string(Felidae::kMixfixStructuralVocabularySize) + ")");
    }
    const std::string expectedSentencePieceIdentity(argv[8]);
    if (!expectedSentencePieceIdentity.starts_with("sha256:") ||
        expectedSentencePieceIdentity.size() != 71) {
      throw std::runtime_error(
          "compiler training requires an exact SHA-256 SentencePiece identity");
    }
    struct Sample {
      std::vector<Felidae::SentencePieceId> input;
      std::vector<std::int64_t> target;
      Felidae::MixfixIrTokenKind decision = Felidae::MixfixIrTokenKind::Accept;
    };
    std::vector<Sample> samples;
    std::size_t rejectedRecords = 0;
    std::size_t abstainedRecords = 0;
    for (const auto &dataPath : dataPaths) {
      std::ifstream dataset(dataPath);
      if (!dataset)
        throw std::runtime_error("cannot open mixfix dataset: " +
                                 dataPath.string());
      std::string line;
      while (std::getline(dataset, line)) {
        if (line.empty())
          continue;
        const auto record = nlohmann::json::parse(line);
        if (!record.is_object())
          throw std::runtime_error("mixfix JSONL record must be an object");
        if (!record.contains("schema_version") ||
            !record.at("schema_version").is_number_unsigned() ||
            record.at("schema_version").get<std::uint64_t>() != 3) {
          throw std::runtime_error(
              "mixfix JSONL schema version is incompatible");
        }
        if (!record.contains("sentencepiece_model_identity") ||
            !record.at("sentencepiece_model_identity").is_string() ||
            record.at("sentencepiece_model_identity").get<std::string>() !=
                expectedSentencePieceIdentity) {
          throw std::runtime_error(
              "mixfix JSONL was generated by a different SentencePiece model");
        }
        if (!record.contains("compiler_ir_vocabulary") ||
            !record.at("compiler_ir_vocabulary").is_string() ||
            record.at("compiler_ir_vocabulary").get<std::string>() !=
                Felidae::kMixfixIrVocabularyVersion) {
          throw std::runtime_error(
              "mixfix JSONL compiler IR vocabulary is incompatible");
        }
        const auto input = ids(record, "input_ids");
        if (input.empty())
          throw std::runtime_error("mixfix JSONL input_ids cannot be empty");
        for (const auto id : input) {
          if (id < 0 || id >= config.inputVocabularySize) {
            throw std::runtime_error("mixfix JSONL input ID exceeds the "
                                     "configured SentencePiece vocabulary");
          }
        }
        if (record.contains("rejection_stage")) {
          if (!record.contains("target_ids"))
            throw std::runtime_error(
                "mixfix rejection record requires a REJECT teacher target");
          ++rejectedRecords;
        }
        auto target = ids(record, "target_ids");
        if (target.empty())
          throw std::runtime_error("mixfix JSONL target_ids cannot be empty");
        for (const auto id : target) {
          if (id < 0 || id >= config.outputVocabularySize) {
            throw std::runtime_error("mixfix JSONL target ID exceeds the "
                                     "configured structural vocabulary");
          }
        }
        const auto vocabulary =
            Felidae::makeMixfixContext(Felidae::FelidaeIr{}).outputVocabulary;
        const auto firstKind =
            vocabulary.at(static_cast<std::size_t>(target.front())).kind;
        const auto declaredDecision = record.value("decision", std::string{});
        if (record.contains("rejection_stage")) {
          if (declaredDecision != "REJECT" || target.size() != 1 ||
              firstKind != Felidae::MixfixIrTokenKind::Reject) {
            throw std::runtime_error(
                "mixfix rejection record target must be exactly REJECT");
          }
        } else if (declaredDecision == "ABSTAIN") {
          if (target.size() != 1 ||
              firstKind != Felidae::MixfixIrTokenKind::Abstain) {
            throw std::runtime_error(
                "mixfix abstention record target must be exactly ABSTAIN");
          }
          ++abstainedRecords;
        } else {
          if (!declaredDecision.empty() && declaredDecision != "ACCEPT") {
            throw std::runtime_error(
                "mixfix JSONL decision must be ACCEPT, REJECT, or ABSTAIN");
          }
          if (firstKind != Felidae::MixfixIrTokenKind::Accept) {
            throw std::runtime_error(
                "mixfix executable teacher target must begin with ACCEPT");
          }
        }
        Sample sample;
        sample.input.assign(input.begin(), input.end());
        sample.target = std::move(target);
        sample.decision = firstKind;
        samples.push_back(std::move(sample));
      }
    }
    if (samples.size() < 5) {
      throw std::runtime_error("mixfix dataset needs at least five corpus "
                               "samples for a held-out validation split");
    }

    // Seed before allocation so this explicit training command is
    // reproducible. Existing artifacts are never implicit checkpoints:
    // a resumed run must be a separate, deliberate trainer feature.
    torch::manual_seed(0);
    const auto artifact = outputDirectory / "mixfix-gru.pt";
    const auto checkpoint = outputDirectory / "mixfix-gru.ckpt";
    Felidae::GruMixfixStateModel model(config, {});
    std::mt19937 generator(0);
    // Keep one complete structural target shape in exactly one partition.
    // A random record split makes generated numeric variations of the same
    // mixfix form appear in both sets, which makes validation loss look
    // better than the model's ability to generalize to another form.
    std::map<std::vector<std::int64_t>, std::vector<std::size_t>>
        targetFamilies;
    for (std::size_t index = 0; index < samples.size(); ++index) {
      targetFamilies[samples[index].target].push_back(index);
    }
    if (targetFamilies.size() < 3)
      throw std::runtime_error(
          "mixfix dataset requires at least three structural families for "
          "train/validation/test splits");
    std::vector<std::vector<std::size_t> *> familyOrder;
    for (auto &[target, family] : targetFamilies) {
      (void)target;
      familyOrder.push_back(&family);
    }
    std::shuffle(familyOrder.begin(), familyOrder.end(), generator);
    std::vector<std::size_t> training, validation, test;
    std::size_t heldOutIndex = 0;
    const auto splitDecisionFamily = [&](std::vector<std::size_t> &family) {
      std::shuffle(family.begin(), family.end(), generator);
      if (family.size() < 3) {
        training.insert(training.end(), family.begin(), family.end());
        return;
      }
      test.push_back(family[0]);
      validation.push_back(family[1]);
      training.insert(training.end(), family.begin() + 2, family.end());
    };
    for (auto *family : familyOrder) {
      const auto decision = samples[family->front()].decision;
      if (decision != Felidae::MixfixIrTokenKind::Accept) {
        // REJECT and ABSTAIN share one-token structural targets, so a
        // target-family-only split would leave safety unmeasured.
        splitDecisionFamily(*family);
        continue;
      }
      auto *destination = &training;
      const auto partition = heldOutIndex++ % 10;
      if (partition == 0)
        destination = &test;
      else if (partition == 1)
        destination = &validation;
      destination->insert(destination->end(), family->begin(), family->end());
    }
    if (training.empty() || validation.empty() || test.empty())
      throw std::runtime_error(
          "mixfix structural-family split produced an empty partition");
    std::cout << "datasets=" << dataPaths.size()
              << " samples=" << samples.size()
              << " rejection_records=" << rejectedRecords
              << " abstention_records=" << abstainedRecords
              << " structural_families=" << targetFamilies.size()
              << " training=" << training.size()
              << " validation=" << validation.size() << " test=" << test.size()
              << "\n";
    const auto evaluationContext =
        Felidae::makeMixfixContext(Felidae::FelidaeIr{});
    for (std::size_t epoch = 0; epoch < epochs; ++epoch) {
      const auto epochStarted = std::chrono::steady_clock::now();
      std::shuffle(training.begin(), training.end(), generator);
      double total = 0.0;
      for (const auto index : training) {
        const auto &sample = samples[index];
        total +=
            model.trainTeacherForced(sample.input, sample.target, learningRate);
      }
      double validationLoss = 0.0;
      std::size_t exactSequences = 0;
      std::size_t correctDecisions = 0;
      std::size_t invalidSequences = 0;
      for (const auto index : validation) {
        const auto &sample = samples[index];
        validationLoss +=
            model.evaluateTeacherForced(sample.input, sample.target);
        try {
          const auto decoded = model.transform(sample.input, evaluationContext);
          std::vector<Felidae::MixfixVocabularyId> expected;
          expected.reserve(sample.target.size());
          for (const auto token : sample.target) {
            expected.push_back(static_cast<Felidae::MixfixVocabularyId>(token));
          }
          if (!decoded.empty() && decoded.front() == expected.front())
            ++correctDecisions;
          if (decoded == expected)
            ++exactSequences;
        } catch (const Felidae::IrError &) {
          ++invalidSequences;
        }
      }
      const auto epochSeconds =
          std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                        epochStarted)
              .count();
      std::cout << "epoch=" << (epoch + 1)
                << " train_loss=" << (total / training.size())
                << " validation_loss=" << (validationLoss / validation.size())
                << " decision_accuracy=" << correctDecisions << '/'
                << validation.size()
                << " autoregressive_exact=" << exactSequences << '/'
                << validation.size()
                << " invalid_autoregressive=" << invalidSequences << '/'
                << validation.size() << " seconds=" << epochSeconds
                << " train_samples_per_second="
                << (epochSeconds > 0.0
                        ? static_cast<double>(training.size()) / epochSeconds
                        : 0.0)
                << "\n";
    }
    double testLoss = 0.0;
    std::size_t testExact = 0, testDecisions = 0, testInvalid = 0;
    for (const auto index : test) {
      const auto &sample = samples[index];
      testLoss += model.evaluateTeacherForced(sample.input, sample.target);
      try {
        const auto decoded = model.transform(sample.input, evaluationContext);
        std::vector<Felidae::MixfixVocabularyId> expected;
        for (const auto value : sample.target)
          expected.push_back(static_cast<Felidae::MixfixVocabularyId>(value));
        if (!decoded.empty() && decoded.front() == expected.front())
          ++testDecisions;
        if (decoded == expected)
          ++testExact;
      } catch (const Felidae::IrError &) {
        ++testInvalid;
      }
    }
    std::cout << "test_loss=" << (testLoss / test.size())
              << " decision_accuracy=" << testDecisions << '/' << test.size()
              << " autoregressive_exact=" << testExact << '/' << test.size()
              << " invalid_autoregressive=" << testInvalid << '/' << test.size()
              << "\n";
    std::filesystem::create_directories(outputDirectory);
    model.saveCheckpoint(checkpoint);
    model.exportTorchScript(artifact);
    std::ofstream manifest(outputDirectory / "manifest.txt", std::ios::trunc);
    if (!manifest)
      throw std::runtime_error("cannot write compiler model manifest");
    manifest << "model_version=gru-v1\n"
             << "backend=torchscript-gru\n"
             << "sentencepiece_model_identity=" << expectedSentencePieceIdentity
             << "\n"
             << "ir_vocabulary_version=" << Felidae::kMixfixIrVocabularyVersion
             << "\n"
             << "input_vocabulary=" << config.inputVocabularySize << "\n"
             << "output_vocabulary=" << config.outputVocabularySize << "\n"
             << "begin_token=" << config.beginToken << "\n";
    manifest.close();
    if (!manifest)
      throw std::runtime_error("cannot finalize compiler model manifest");
    auto verified = Felidae::GruMixfixStateModel::loadVersioned(
        config, outputDirectory, expectedSentencePieceIdentity);
    (void)verified;
    std::cout << std::filesystem::absolute(artifact).lexically_normal().string()
              << "\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "error: " << error.what() << "\n";
    return 1;
  }
}
