#include "MixfixStateModel.h"
#include "MixfixTraining.h"
#include "SentencePieceModel.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <torch/torch.h>
#include <nlohmann/json.hpp>
#include <sentencepiece_processor.h>

#ifndef FELIDAE_SENTENCEPIECE_MODEL_PATH
#error "The C++ mixfix trainer requires the generated SentencePiece model path"
#endif

namespace {

std::vector<std::int64_t> ids(const nlohmann::json& record, const char* field) {
    if (!record.contains(field) || !record.at(field).is_array() || record.at(field).empty()) {
        throw std::runtime_error(std::string("mixfix JSONL record requires non-empty ") + field);
    }
    std::vector<std::int64_t> result;
    for (const auto& value : record.at(field)) {
        if (!value.is_number_unsigned() || value.get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            throw std::runtime_error(std::string("mixfix JSONL ") + field + " must contain non-negative integer IDs");
        }
        result.push_back(static_cast<std::int64_t>(value.get<std::uint64_t>()));
    }
    return result;
}

std::uint64_t fnv1a(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot hash exported artifact");
    std::uint64_t hash = 14695981039346656037ull;
    char byte = 0;
    while (input.get(byte)) {
        hash ^= static_cast<unsigned char>(byte);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::vector<std::filesystem::path> datasetPaths(std::string_view encoded) {
    constexpr char separator = '\x1f';
    std::vector<std::filesystem::path> paths;
    std::size_t begin = 0;
    while (begin <= encoded.size()) {
        const auto end = encoded.find(separator, begin);
        const auto segment = encoded.substr(begin, end == std::string_view::npos ? encoded.size() - begin : end - begin);
        if (segment.empty()) throw std::runtime_error("mixfix training dataset list contains an empty path");
        paths.emplace_back(segment);
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return paths;
}

} // namespace

int Felidae::runMixfixGruTraining(int argc, char** argv) {
    // JSONL records: {"input_ids":[...],"target_ids":[...]}. The target
    // sequence includes the finite IR_END vocabulary token.
    if (argc != 8) {
        std::cerr << "internal error: compiler training arguments are invalid\n";
        return 2;
    }
    try {
        const auto dataPaths = datasetPaths(argv[1]);
        const auto outputDirectory = std::filesystem::path(argv[2]);
        for (const auto& dataPath : dataPaths) {
            if (dataPath.extension() != ".jsonl") throw std::runtime_error("mixfix training dataset must use the .jsonl extension");
        }
        Felidae::GruMixfixStateModel::Configuration config;
        config.inputVocabularySize = std::stoll(argv[3]);
        config.outputVocabularySize = std::stoll(argv[4]);
        config.beginToken = std::stoll(argv[5]);
        config.allowRandomInitialization = true;
        const auto epochs = std::stoull(argv[6]);
        const auto learningRate = std::stod(argv[7]);
        if (config.inputVocabularySize <= 0 || config.outputVocabularySize <= 0 ||
            config.beginToken < 0 || config.beginToken >= config.outputVocabularySize ||
            epochs == 0 || learningRate <= 0.0) {
            throw std::runtime_error("mixfix training configuration is invalid");
        }
        const auto sentencePieceVocabulary = Felidae::felidaeSentencePieceModel().GetPieceSize();
        if (config.inputVocabularySize != sentencePieceVocabulary) {
            throw std::runtime_error("mixfix input vocabulary must match the configured SentencePiece model (expected " +
                                     std::to_string(sentencePieceVocabulary) + ")");
        }
        if (config.outputVocabularySize != static_cast<std::int64_t>(Felidae::kMixfixStructuralVocabularySize)) {
            throw std::runtime_error("mixfix output vocabulary must match the fixed structural IR vocabulary (expected " +
                                     std::to_string(Felidae::kMixfixStructuralVocabularySize) + ")");
        }
        struct Sample { std::vector<Felidae::SentencePieceId> input; std::vector<std::int64_t> target; };
        std::vector<Sample> samples;
        std::size_t rejectedRecords = 0;
        for (const auto& dataPath : dataPaths) {
            std::ifstream dataset(dataPath);
            if (!dataset) throw std::runtime_error("cannot open mixfix dataset: " + dataPath.string());
            std::string line;
            while (std::getline(dataset, line)) {
                if (line.empty()) continue;
                const auto record = nlohmann::json::parse(line);
                if (!record.is_object()) throw std::runtime_error("mixfix JSONL record must be an object");
                if (!record.contains("schema_version") || !record.at("schema_version").is_number_unsigned() ||
                    record.at("schema_version").get<std::uint64_t>() != 1) {
                    throw std::runtime_error("mixfix JSONL schema version is incompatible");
                }
                const auto input = ids(record, "input_ids");
                for (const auto id : input) {
                    if (id < 0 || id >= config.inputVocabularySize) {
                        throw std::runtime_error("mixfix JSONL input ID exceeds the configured SentencePiece vocabulary");
                    }
                }
                if (record.contains("rejection_stage")) {
                    if (record.contains("target_ids")) throw std::runtime_error("mixfix rejection record must not contain executable target IDs");
                    ++rejectedRecords;
                    continue;
                }
                auto target = ids(record, "target_ids");
                for (const auto id : target) {
                    if (id < 0 || id >= config.outputVocabularySize) {
                        throw std::runtime_error("mixfix JSONL target ID exceeds the configured structural vocabulary");
                    }
                }
                Sample sample;
                sample.input.assign(input.begin(), input.end());
                sample.target = std::move(target);
                samples.push_back(std::move(sample));
            }
        }
        if (samples.size() < 5) {
            throw std::runtime_error("mixfix dataset needs at least five corpus samples for a held-out validation split");
        }

        // Seed before allocation so this explicit training command is
        // reproducible. Existing artifacts are never implicit checkpoints:
        // a resumed run must be a separate, deliberate trainer feature.
        torch::manual_seed(0);
        const auto artifact = outputDirectory / "mixfix-gru.pt";
        Felidae::GruMixfixStateModel model(config, {});
        std::mt19937 generator(0);
        // Keep one complete structural target shape in exactly one partition.
        // A random record split makes generated numeric variations of the same
        // mixfix form appear in both sets, which makes validation loss look
        // better than the model's ability to generalize to another form.
        std::map<std::vector<std::int64_t>, std::vector<std::size_t>> targetFamilies;
        for (std::size_t index = 0; index < samples.size(); ++index) {
            targetFamilies[samples[index].target].push_back(index);
        }
        std::vector<std::size_t> training;
        std::vector<std::size_t> validation;
        for (auto& [target, family] : targetFamilies) {
            (void)target;
            std::shuffle(family.begin(), family.end(), generator);
            // Families smaller than five remain training-only.  Reporting this
            // explicitly is more honest than evaluating an isolated example
            // as if it established generalization.
            const auto heldOut = family.size() / 5;
            validation.insert(validation.end(), family.begin(), family.begin() + heldOut);
            training.insert(training.end(), family.begin() + heldOut, family.end());
        }
        if (validation.empty()) {
            throw std::runtime_error("mixfix dataset has no structural family large enough for held-out validation");
        }
        if (training.empty()) throw std::runtime_error("mixfix dataset has no training samples after grouped split");
        std::cout << "datasets=" << dataPaths.size() << " samples=" << samples.size()
                  << " rejection_records=" << rejectedRecords << " structural_families=" << targetFamilies.size()
                  << " training=" << training.size() << " validation=" << validation.size() << "\n";
        const auto evaluationContext = Felidae::makeMixfixContext(Felidae::FelidaeIr{});
        for (std::size_t epoch = 0; epoch < epochs; ++epoch) {
            std::shuffle(training.begin(), training.end(), generator);
            double total = 0.0;
            for (const auto index : training) {
                const auto& sample = samples[index];
                total += model.trainTeacherForced(sample.input, sample.target, learningRate);
            }
            double validationLoss = 0.0;
            std::size_t exactSequences = 0;
            std::size_t invalidSequences = 0;
            for (const auto index : validation) {
                const auto& sample = samples[index];
                validationLoss += model.evaluateTeacherForced(sample.input, sample.target);
                try {
                    const auto decoded = model.transform(sample.input, evaluationContext);
                    std::vector<Felidae::MixfixVocabularyId> expected;
                    expected.reserve(sample.target.size());
                    for (const auto token : sample.target) {
                        expected.push_back(static_cast<Felidae::MixfixVocabularyId>(token));
                    }
                    if (decoded == expected) ++exactSequences;
                } catch (const Felidae::IrError&) {
                    ++invalidSequences;
                }
            }
            std::cout << "epoch=" << (epoch + 1)
                      << " train_loss=" << (total / training.size())
                      << " validation_loss=" << (validationLoss / validation.size())
                      << " autoregressive_exact=" << exactSequences << '/' << validation.size()
                      << " invalid_autoregressive=" << invalidSequences << '/' << validation.size() << "\n";
        }
        std::filesystem::create_directories(outputDirectory);
        model.saveArtifact(artifact);
        std::ofstream manifest(outputDirectory / "manifest.txt", std::ios::trunc);
        manifest << "model_version=gru-v1\n"
                 << "backend=libtorch-gru\n"
                 << "sentencepiece_model_hash=fnv1a64:" << std::hex
                 << fnv1a(FELIDAE_SENTENCEPIECE_MODEL_PATH) << "\n"
                 << std::dec
                 << "ir_vocabulary_version=felidae-ir-v8\n"
                 << "input_vocabulary=" << config.inputVocabularySize << "\n"
                 << "output_vocabulary=" << config.outputVocabularySize << "\n"
                 << "begin_token=" << config.beginToken << "\n"
                 << "artifact_hash=fnv1a64:" << std::hex << fnv1a(artifact) << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
