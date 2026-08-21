#include "form/RuntimeStateModel.h"
#include "form/BinaryIr.h"
#include "form/RuntimeTraining.h"
#include "form/RuntimeTrainingCommand.h"

#include <filesystem>
#include <iostream>
#include <algorithm>
#include <map>
#include <random>

#include <torch/torch.h>

namespace {
using namespace Felidae;

std::size_t targetFor(const RuntimeTrainingRecord& record,
                      const std::vector<RuntimeOutputToken>& vocabulary) {
    const auto matches = [&](const RuntimeOutputToken& token) {
        switch (record.targetKind) {
        case RuntimeTrainingTargetKind::InputReference: return token.kind == RuntimeOutputTokenKind::InputReference;
        case RuntimeTrainingTargetKind::FactFromInput: return token.kind == RuntimeOutputTokenKind::FactFromInput;
        case RuntimeTrainingTargetKind::DegreeMilli: return token.kind == RuntimeOutputTokenKind::DegreeMilli;
        case RuntimeTrainingTargetKind::Nil: return token.kind == RuntimeOutputTokenKind::Nil;
        case RuntimeTrainingTargetKind::Boolean: return token.kind == RuntimeOutputTokenKind::Boolean;
        }
        return false;
    };
    for (std::size_t i = 0; i < vocabulary.size(); ++i) {
        const auto& token = vocabulary[i];
        if (matches(token) && token.value == record.targetValue) return i;
    }
    throw IrError("runtime dataset result has no permitted finite GRU action target");
}
}

int Felidae::runRuntimeGruTraining(int argc, char** argv) {
    try {
        if (argc < 3 || argc > 5) {
            throw std::runtime_error("internal error: VM training arguments are invalid");
        }
        const auto records = Felidae::loadRuntimeTrainingDataset(argv[1]);
        if (records.size() < 10) throw Felidae::IrError("runtime training dataset requires at least ten records");
        const std::size_t epochs = argc >= 4 ? std::stoull(argv[3]) : 8;
        const double rate = argc == 5 ? std::stod(argv[4]) : 0.001;
        if (epochs == 0 || rate <= 0.0) throw Felidae::IrError("runtime training options are invalid");
        auto vocabulary = Felidae::defaultRuntimeOutputVocabulary();
        Felidae::GruRuntimeStateModel::Configuration configuration;
        configuration.inputVocabularySize = 4096;
        configuration.outputVocabularySize = static_cast<std::int64_t>(vocabulary.size());
        configuration.allowRandomInitialization = true;
        // Explicit CPU-first reproducibility: data ordering plus this seed
        // makes a repeated C++ training run comparable before benchmarking.
        torch::manual_seed(0);
        Felidae::GruRuntimeStateModel model(configuration, vocabulary);
        std::mt19937_64 random(0);
        // Split whole operation/action families. A random record-level split
        // lets repeated context templates into both partitions and therefore
        // overstates generalization. Rare families stay training-only rather
        // than being presented as reliable held-out evidence.
        using EvaluationBucket = std::pair<IrSymbolRef, std::size_t>;
        std::map<EvaluationBucket, std::vector<std::size_t>> families;
        for (std::size_t index = 0; index < records.size(); ++index) {
            families[{records[index].operationSymbol, targetFor(records[index], vocabulary)}].push_back(index);
        }
        std::vector<std::size_t> training;
        std::vector<std::size_t> validation;
        for (auto& [bucket, family] : families) {
            (void)bucket;
            std::shuffle(family.begin(), family.end(), random);
            const auto heldOut = family.size() / 5;
            validation.insert(validation.end(), family.begin(), family.begin() + heldOut);
            training.insert(training.end(), family.begin() + heldOut, family.end());
        }
        if (validation.empty()) throw Felidae::IrError("runtime dataset has no operation/action family large enough for held-out validation");
        if (training.empty()) throw Felidae::IrError("runtime dataset has no training records after grouped split");
        std::cout << "records=" << records.size() << " operation_action_families=" << families.size()
                  << " training=" << training.size() << " validation=" << validation.size() << "\n";
        for (std::size_t epoch = 0; epoch < epochs; ++epoch) {
            std::shuffle(training.begin(), training.end(), random);
            double totalLoss = 0.0;
            for (const auto index : training) {
                const auto& record = records[index];
                totalLoss += model.trainTeacherForced(record, targetFor(record, vocabulary), rate);
            }
            std::size_t correct = 0;
            std::map<EvaluationBucket, std::pair<std::size_t, std::size_t>> bucketMetrics;
            for (const auto index : validation) {
                const auto& record = records[index];
                const auto target = targetFor(record, vocabulary);
                const auto predicted = model.predictTeacherToken(record);
                auto& metric = bucketMetrics[{record.operationSymbol, target}];
                ++metric.second;
                correct += predicted == target ? 1 : 0;
                if (predicted == target) ++metric.first;
            }
            std::cout << "epoch=" << (epoch + 1)
                      << " train_loss=" << (totalLoss / training.size())
                      << " validation_accuracy=" << (static_cast<double>(correct) / validation.size()) << "\n";
            for (const auto& [bucket, metric] : bucketMetrics) {
                std::cout << "  validation_operation=" << bucket.first << " target_token=" << bucket.second
                          << " correct=" << metric.first << '/' << metric.second << "\n";
            }
        }
        const std::filesystem::path output(argv[2]);
        std::filesystem::create_directories(output);
        model.saveArtifact(output / "runtime-gru.pt");
        // Treat export as successful only when the production manifest gate
        // can load these exact weights again.
        auto verified = Felidae::GruRuntimeStateModel::loadVersioned(configuration, vocabulary, output);
        (void)verified;
        std::cout << std::filesystem::absolute(output / "runtime-gru.pt").lexically_normal().string() << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
