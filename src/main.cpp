#include "CompilerFrontend.h"
#include "Version.h"
#include "form/BinaryIr.h"

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
    throw std::runtime_error("felidae_compiler accepts exactly one .fx source file");
}

void printHelp() {
    std::cout << LANGUAGE_NAME << " compiler v" << LANGUAGE_VERSION << "\n\n"
              << "Usage: felidae_compiler program.fx\n"
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
        const auto source = resolveProgramEntryPath(*input);
        if (source.extension() != FILE_EXTENSION) {
            throw std::runtime_error("felidae_compiler accepts only .fx source files");
        }
        const auto module = compileProgramFileToIr(source);
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
