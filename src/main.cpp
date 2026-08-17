#include "CompilerFrontend.h"
#include "MixfixStateModel.h"
#include "SentencePieceModel.h"
#include "Version.h"
#include "form/BinaryIr.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>

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
              << "Writes verified binary IR to the compiler build directory.\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--version") {
            std::cout << LANGUAGE_NAME << " compiler v" << LANGUAGE_VERSION << "\n";
            return 0;
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
        // Build artifacts never modify example/source directories.  The
        // compiler executable is placed in build/, so its parent is the
        // canonical output directory on every supported CMake generator.
        const auto executable = fs::absolute(argv[0]).lexically_normal();
        auto output = executable.parent_path() / source.filename();
        output.replace_extension(kBinaryIrExtension);
        writeBinaryIr(output, module);
        std::cout << output.string() << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
