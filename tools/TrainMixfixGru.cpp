#include "MixfixStateModel.h"

#include <charconv>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <torch/torch.h>

#ifndef FELIDAE_SENTENCEPIECE_MODEL_PATH
#error "The C++ mixfix trainer requires the generated SentencePiece model path"
#endif

namespace {

std::vector<std::int64_t> ids(const std::string& text) {
    std::vector<std::int64_t> result;
    std::size_t begin = 0;
    while (begin < text.size()) {
        const auto end = text.find(',', begin);
        const auto part = text.substr(begin, end == std::string::npos ? end : end - begin);
        std::int64_t value = 0;
        const auto conversion = std::from_chars(part.data(), part.data() + part.size(), value);
        if (part.empty() || conversion.ec != std::errc{} || conversion.ptr != part.data() + part.size() || value < 0) {
            throw std::runtime_error("dataset IDs must be non-negative comma-separated integers");
        }
        result.push_back(value);
        if (end == std::string::npos) break;
        begin = end + 1;
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

} // namespace

int main(int argc, char** argv) {
    // TSV records: SentencePiece IDs<TAB>finite IR-vocabulary IDs.  The
    // output sequence includes the IR_END vocabulary token.
    if (argc != 8) {
        std::cerr << "usage: felidae_train_mixfix_gru DATA.tsv OUT_DIR INPUT_VOCAB OUTPUT_VOCAB BEGIN_TOKEN EPOCHS LEARNING_RATE\n";
        return 2;
    }
    try {
        const auto dataPath = std::filesystem::path(argv[1]);
        const auto outputDirectory = std::filesystem::path(argv[2]);
        Felidae::GruMixfixStateModel::Configuration config;
        config.inputVocabularySize = std::stoll(argv[3]);
        config.outputVocabularySize = std::stoll(argv[4]);
        config.beginToken = std::stoll(argv[5]);
        config.allowRandomInitialization = true;
        const auto epochs = std::stoull(argv[6]);
        const auto learningRate = std::stod(argv[7]);
        std::ifstream dataset(dataPath);
        if (!dataset) throw std::runtime_error("cannot open mixfix dataset: " + dataPath.string());

        struct Sample { std::vector<Felidae::SentencePieceId> input; std::vector<std::int64_t> target; };
        std::vector<Sample> samples;
        std::string line;
        while (std::getline(dataset, line)) {
            if (line.empty() || line[0] == '#') continue;
            const auto tab = line.find('\t');
            if (tab == std::string::npos) throw std::runtime_error("dataset record must be TSV");
            const auto input = ids(line.substr(0, tab));
            auto target = ids(line.substr(tab + 1));
            if (input.empty() || target.empty()) throw std::runtime_error("dataset record cannot be empty");
            Sample sample;
            sample.input.assign(input.begin(), input.end());
            sample.target = std::move(target);
            samples.push_back(std::move(sample));
        }
        if (samples.empty()) throw std::runtime_error("mixfix dataset contains no samples");

        const auto artifact = outputDirectory / "mixfix-gru.pt";
        Felidae::GruMixfixStateModel model(config,
            std::filesystem::is_regular_file(artifact) ? artifact : std::filesystem::path{});
        torch::manual_seed(0);
        std::mt19937 generator(0);
        std::vector<std::size_t> order(samples.size());
        std::iota(order.begin(), order.end(), 0);
        for (std::size_t epoch = 0; epoch < epochs; ++epoch) {
            std::shuffle(order.begin(), order.end(), generator);
            double total = 0.0;
            for (const auto index : order) {
                const auto& sample = samples[index];
                total += model.trainTeacherForced(sample.input, sample.target, learningRate);
            }
            std::cout << "epoch=" << (epoch + 1) << " loss=" << (total / samples.size()) << "\n";
        }
        model.saveArtifact(artifact);
        std::filesystem::create_directories(outputDirectory);
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
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
