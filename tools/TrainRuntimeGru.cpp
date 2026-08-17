#include "form/RuntimeStateModel.h"
#include "form/BinaryIr.h"
#include "form/RuntimeTraining.h"

#include <filesystem>
#include <iostream>

namespace {
using namespace Felidae;

std::size_t targetFor(const RuntimeTrainingRecord& record,
                      const std::vector<RuntimeOutputToken>& vocabulary) {
    // VmValue variant order: nil=0, bool=1, number=2, text=3, array=4,
    // map=5, fact=6. The finite action target is selected from verified
    // result kind; unsupported values intentionally fail rather than being
    // stringified or silently coerced.
    for (std::size_t i = 0; i < vocabulary.size(); ++i) {
        const auto& token = vocabulary[i];
        if (record.resultKind == 0 && token.kind == RuntimeOutputTokenKind::Nil) return i;
        if (record.resultKind == 1 && token.kind == RuntimeOutputTokenKind::Boolean) return i;
        if (record.resultKind == 6 && token.kind == RuntimeOutputTokenKind::FactFromInput && token.value == 0) return i;
        if (record.resultKind == record.inputKind && token.kind == RuntimeOutputTokenKind::InputReference && token.value == 0) return i;
    }
    throw IrError("runtime dataset result has no permitted finite GRU action target");
}
}

int main(int argc, char** argv) {
    try {
        if (argc < 3 || argc > 5) {
            throw std::runtime_error("usage: felidae_train_runtime_gru data.frtd models-dir [epochs] [learning-rate]");
        }
        const auto records = Felidae::loadRuntimeTrainingDataset(argv[1]);
        if (records.empty()) throw Felidae::IrError("runtime training dataset is empty");
        const std::size_t epochs = argc >= 4 ? std::stoull(argv[3]) : 8;
        const double rate = argc == 5 ? std::stod(argv[4]) : 0.001;
        if (epochs == 0 || rate <= 0.0) throw Felidae::IrError("runtime training options are invalid");
        std::vector<Felidae::RuntimeOutputToken> vocabulary{
            {Felidae::RuntimeOutputTokenKind::Nil, 0},
            {Felidae::RuntimeOutputTokenKind::Boolean, 0},
            {Felidae::RuntimeOutputTokenKind::InputReference, 0},
            {Felidae::RuntimeOutputTokenKind::FactFromInput, 0},
        };
        Felidae::GruRuntimeStateModel::Configuration configuration;
        configuration.inputVocabularySize = 4096;
        configuration.outputVocabularySize = static_cast<std::int64_t>(vocabulary.size());
        configuration.allowRandomInitialization = true;
        Felidae::GruRuntimeStateModel model(configuration, vocabulary);
        for (std::size_t epoch = 0; epoch < epochs; ++epoch) {
            double totalLoss = 0.0;
            for (const auto& record : records) {
                totalLoss += model.trainTeacherForced(record, targetFor(record, vocabulary), rate);
            }
            std::cout << "epoch=" << (epoch + 1) << " loss=" << (totalLoss / records.size()) << "\n";
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
