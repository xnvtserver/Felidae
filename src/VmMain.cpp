#include "form/BinaryIsa.h"
#include "form/RuntimeStateModel.h"
#include "form/RuntimeTrainingCommand.h"
#include "ModelStore.h"
#include "Version.h"

#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using namespace Felidae;

namespace {
struct Options { fs::path input; bool serve = false; std::optional<fs::path> modelDirectory; };

std::optional<Options> parseInput(int argc, char** argv) {
    if (argc == 1 || (argc == 2 && std::string(argv[1]) == "--help")) return std::nullopt;
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);
        if (argument == "--serve") { options.serve = true; continue; }
        if (argument == "--model") {
            if (++index == argc) throw std::runtime_error("--model requires a model directory");
            options.modelDirectory = fs::path(argv[index]);
            continue;
        }
        if (!options.input.empty()) throw std::runtime_error("felidae_vm accepts exactly one verified .bin ISA file");
        options.input = argument;
    }
    if (options.input.empty()) throw std::runtime_error("felidae_vm requires a .bin Felidae ISA file");
    return options;
}

void printHelp() {
    std::cout << LANGUAGE_NAME << " Form VM v" << LANGUAGE_VERSION << "\n\n"
              << "Usage: felidae_vm program.bin\n"
              << "       felidae_vm [--serve] [--model models/runtime] program.bin\n"
              << "       felidae_vm --train datasets/vm/runtime-context-v1.jsonl --store-model build|dist [--epochs N] [--learning-rate R]\n"
              << "Loads, verifies, and executes Felidae ISA. --serve keeps one VM and "
                 "its fact memory resident; use run, facts [type-id], field <id>, "
                 "history, proof <child-id> <ancestor-id>, load <module.bin>, "
                 "modules, or quit on stdin.\n";
}

int trainRuntimeModel(const ModelTrainingOptions& training) {
#ifdef FELIDAE_HAS_TRAINING
    if (training.dataset.extension() != ".jsonl") {
        throw std::runtime_error("--train requires one VM .jsonl dataset");
    }
    const auto output = modelStoreDirectory(training.store, "runtime-gru");
    std::vector<std::string> arguments{
        "felidae_vm", training.dataset.string(), output.string(),
        std::to_string(training.epochs), std::to_string(training.learningRate)};
    std::vector<char*> rawArguments;
    rawArguments.reserve(arguments.size());
    for (auto& argument : arguments) rawArguments.push_back(argument.data());
    return runRuntimeGruTraining(static_cast<int>(rawArguments.size()), rawArguments.data());
#else
    (void)training;
    throw std::runtime_error("this VM build has training disabled; configure FELIDAE_ENABLE_TRAINING=ON");
#endif
}

IrSymbolRef readSymbol(std::istringstream& input, const char* command) {
    std::uint64_t value = 0;
    if (!(input >> value) || value == 0) throw std::runtime_error(std::string(command) + " requires a nonzero numeric symbol ID");
    return static_cast<IrSymbolRef>(value);
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--version") {
            std::cout << LANGUAGE_NAME << " Form VM v" << LANGUAGE_VERSION << "\n";
            return 0;
        }
        if (const auto training = parseModelTrainingOptions(argc, argv)) {
            return trainRuntimeModel(*training);
        }
        const auto options = parseInput(argc, argv);
        if (!options) { printHelp(); return argc == 1 ? 1 : 0; }
        const auto binary = fs::absolute(options->input).lexically_normal();
        if (binary.extension() != kFelidaeBinaryExtension) {
            throw std::runtime_error("felidae_vm accepts only .bin Felidae ISA files");
        }
        auto module = loadBinaryIsa(binary);
        auto display = makeIsaDisplayContext(module);
#ifdef FELIDAE_HAS_TORCH
        std::unique_ptr<GruRuntimeStateModel> model;
        if (options->modelDirectory) {
            auto vocabulary = defaultRuntimeOutputVocabulary();
            GruRuntimeStateModel::Configuration configuration;
            configuration.inputVocabularySize = 4096;
            configuration.outputVocabularySize = static_cast<std::int64_t>(vocabulary.size());
            model = std::make_unique<GruRuntimeStateModel>(
                GruRuntimeStateModel::loadVersioned(configuration, std::move(vocabulary), *options->modelDirectory));
        }
        FelidaeKnowledgeRuntime runtime(model.get());
#else
        if (options->modelDirectory) throw std::runtime_error("this VM build has no LibTorch runtime SSM support");
        FelidaeKnowledgeRuntime runtime;
#endif
        runtime.installIsaModule(module);
        RegisterVm vm;
        const auto execute = [&] {
            std::cout << vmValueToDisplayString(vm.executeIsaMain(module, runtime), display) << "\n";
        };
        if (!options->serve) {
            execute();
            return 0;
        }
        std::string command;
        while (std::getline(std::cin, command)) {
            std::istringstream words(command);
            std::string verb;
            words >> verb;
            if (verb == "run") execute();
            else if (verb == "load") {
                std::string path;
                if (!(words >> path)) throw std::runtime_error("load requires a .bin Felidae ISA file");
                const auto next = fs::absolute(fs::path(path)).lexically_normal();
                if (next.extension() != kFelidaeBinaryExtension) throw std::runtime_error("load accepts only .bin Felidae ISA files");
                module = loadBinaryIsa(next);
                display = makeIsaDisplayContext(module);
                // Registration is explicit and verified before this module can
                // become the daemon's current entry point. Persistent facts
                // and already registered procedures are preserved.
                runtime.installIsaModule(module);
                for (const auto& type : module.factTypes) runtime.registerFactType(type.symbol, type.parents);
                std::cout << next.string() << "\n";
            } else if (verb == "modules") {
                std::cout << runtime.installedModuleCount() << "\n";
            }
            else if (verb == "facts") {
                std::uint64_t type = 0;
                if (words >> type) std::cout << runtime.factStore()->snapshotAssignableTo(type).size() << "\n";
                else std::cout << runtime.factStore()->size() << "\n";
            } else if (verb == "field") {
                std::cout << runtime.factStore()->snapshotByField(readSymbol(words, "field")).size() << "\n";
            } else if (verb == "history") {
                std::cout << runtime.factStore()->mutations().size() << "\n";
            } else if (verb == "proof") {
                const auto child = readSymbol(words, "proof");
                const auto ancestor = readSymbol(words, "proof");
                const auto path = runtime.factStore()->hierarchyProof(child, ancestor);
                for (std::size_t index = 0; index < path.size(); ++index) {
                    if (index != 0) std::cout << ' ';
                    std::cout << path[index];
                }
                std::cout << "\n";
            } else if (verb == "quit") return 0;
            else throw std::runtime_error("VM daemon command is invalid");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
