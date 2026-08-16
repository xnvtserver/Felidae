#include "BinaryIr.h"
#include "../Version.h"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

namespace fs = std::filesystem;
using namespace Felidae;

namespace {
std::optional<fs::path> parseInput(int argc, char** argv) {
    if (argc == 2) return fs::path(argv[1]);
    if (argc == 1 || (argc == 2 && std::string(argv[1]) == "--help")) return std::nullopt;
    throw std::runtime_error("felidae_vm accepts exactly one .fir binary IR file");
}

void printHelp() {
    std::cout << LANGUAGE_NAME << " Form VM v" << LANGUAGE_VERSION << "\n\n"
              << "Usage: felidae_vm program.fir\n"
              << "Loads, verifies, and executes binary IR.\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--version") {
            std::cout << LANGUAGE_NAME << " Form VM v" << LANGUAGE_VERSION << "\n";
            return 0;
        }
        const auto input = parseInput(argc, argv);
        if (!input) { printHelp(); return argc == 1 ? 1 : 0; }
        const auto binary = fs::absolute(*input).lexically_normal();
        if (binary.extension() != kBinaryIrExtension) {
            throw std::runtime_error("felidae_vm accepts only .fir binary IR files");
        }
        const auto module = loadBinaryIr(binary);
        DirectVmRuntime runtime(module.procedures);
        RegisterVm vm;
        std::cout << vmValueToDisplayString(vm.executeMain(module, runtime)) << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
