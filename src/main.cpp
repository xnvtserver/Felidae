#include "CompilerFrontend.h"
#include "IntegerTokenList.h"
#include "MixfixTraining.h"
#include "MixfixStateModel.h"
#include "ModelStore.h"
#include "SentencePieceModel.h"
#include "Version.h"
#include "form/BinaryIsa.h"
#include "form/IsaLowerer.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <sentencepiece_processor.h>

namespace fs = std::filesystem;
using namespace Felidae;

namespace {
struct Options { fs::path input; std::optional<fs::path> mixfixModelDirectory; };

std::optional<Options> parseInput(int argc, char** argv) {
    if (argc == 1 || (argc == 2 && std::string(argv[1]) == "--help")) return std::nullopt;
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);
        if (argument == "--mixfix-model") {
            if (++index == argc) throw std::runtime_error("--mixfix-model requires a model directory");
            options.mixfixModelDirectory = fs::path(argv[index]);
            continue;
        }
        if (!options.input.empty()) throw std::runtime_error("felidae_compiler accepts exactly one .fx source file");
        options.input = argument;
    }
    if (options.input.empty()) throw std::runtime_error("felidae_compiler requires a .fx source file");
    return options;
}

std::string manifestValue(const fs::path& manifest, const char* key) {
    std::ifstream input(manifest);
    if (!input) throw std::runtime_error("cannot open mixfix model manifest: " + manifest.string());
    std::string line;
    while (std::getline(input, line)) {
        const auto equals = line.find('=');
        if (equals != std::string::npos && line.substr(0, equals) == key) return line.substr(equals + 1);
    }
    throw std::runtime_error(std::string("mixfix model manifest omits ") + key);
}

void printHelp() {
    std::cout << LANGUAGE_NAME << " compiler v" << LANGUAGE_VERSION << "\n\n"
              << "Usage: felidae_compiler [--mixfix-model models/mixfix] program.fx\n"
              << "       felidae_compiler --tokenize input.fx\n"
              << "       felidae_compiler --train 'datasets/compiler/*.jsonl' --store-model build|dist [--epochs N] [--learning-rate R]\n"
              << "Writes verified Felidae ISA to the compiler directory.\n";
}

std::string readSourceFile(const fs::path& source) {
    std::ifstream input(source, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open source file: " + source.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

int tokenizeSource(const fs::path& input) {
    const auto source = resolveProgramEntryPath(input);
    if (source.extension() != FILE_EXTENSION) {
        throw std::runtime_error("--tokenize accepts only .fx source files");
    }
    const IntegerTokenList tokens(felidaeSentencePieceModel(), readSourceFile(source));
    std::cout << '[';
    for (std::size_t index = 0; index < tokens.entries().size(); ++index) {
        if (index != 0) std::cout << ',';
        std::cout << tokens.entries()[index].id;
    }
    std::cout << "]\n";
    return 0;
}

int trainMixfixModel(const ModelTrainingOptions& training) {
#ifdef FELIDAE_HAS_TORCH
    const auto datasets = expandJsonlDatasetPaths(training.dataset);
    const auto output = modelStoreDirectory(training.store, "mixfix-gru");
    const auto inputVocabulary = std::to_string(felidaeSentencePieceModel().GetPieceSize());
    const auto outputVocabulary = std::to_string(kMixfixStructuralVocabularySize);
    std::vector<std::string> arguments{
        "felidae_compiler", encodeDatasetPaths(datasets), output.string(), inputVocabulary,
        outputVocabulary, "0", std::to_string(training.epochs), std::to_string(training.learningRate)};
    std::vector<char*> rawArguments;
    rawArguments.reserve(arguments.size());
    for (auto& argument : arguments) rawArguments.push_back(argument.data());
    return runMixfixGruTraining(static_cast<int>(rawArguments.size()), rawArguments.data());
#else
    (void)training;
    throw std::runtime_error("this compiler build has no LibTorch training support");
#endif
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--version") {
            std::cout << LANGUAGE_NAME << " compiler v" << LANGUAGE_VERSION << "\n";
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--tokenize") {
            return tokenizeSource(argv[2]);
        }
        if (const auto training = parseModelTrainingOptions(argc, argv)) {
            return trainMixfixModel(*training);
        }
        const auto input = parseInput(argc, argv);
        if (!input) { printHelp(); return argc == 1 ? 1 : 0; }
        const auto source = resolveProgramEntryPath(input->input);
        if (source.extension() != FILE_EXTENSION) {
            throw std::runtime_error("felidae_compiler accepts only .fx source files");
        }
        CompilerOptions compilerOptions;
#ifdef FELIDAE_HAS_TORCH
        std::optional<GruMixfixStateModel> mixfixModel;
        if (input->mixfixModelDirectory) {
            const auto manifest = *input->mixfixModelDirectory / "manifest.txt";
            GruMixfixStateModel::Configuration configuration;
            configuration.inputVocabularySize = std::stoll(manifestValue(manifest, "input_vocabulary"));
            configuration.outputVocabularySize = std::stoll(manifestValue(manifest, "output_vocabulary"));
            configuration.beginToken = std::stoll(manifestValue(manifest, "begin_token"));
            mixfixModel.emplace(GruMixfixStateModel::loadVersioned(
                configuration, *input->mixfixModelDirectory, felidaeSentencePieceModelHash()));
            compilerOptions.mixfixModel = &*mixfixModel;
        }
#else
        if (input->mixfixModelDirectory) throw std::runtime_error("this compiler build has no LibTorch mixfix support");
#endif
        const auto module = compileProgramFileToIr(source, compilerOptions);
        verifyIrModule(module);
        const auto isaModule = IsaLowerer::lowerModule(module);
        verifyIsaModule(isaModule);
        // Build artifacts never modify example/source directories or a staged
        // distribution. The caller's existing build/ directory is the one
        // canonical artifact location for both a developer build and the
        // portable compiler.
        const auto outputDirectory = fs::absolute(fs::path(argv[0])).lexically_normal().parent_path();
        fs::create_directories(outputDirectory);
        auto output = outputDirectory / source.filename();
        output.replace_extension(kFelidaeBinaryExtension);
        writeBinaryIsa(output, isaModule);
        std::cout << output.string() << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
