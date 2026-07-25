#include "celidae/Visualization.h"
#include "tooling/SourceParser.h"
#include "Version.h"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

namespace fs = std::filesystem;

namespace {

struct Options {
    bool html = false;
    bool envelope = true;
    bool loadImports = false;
    bool help = false;
    std::optional<fs::path> file;
};

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") options.help = true;
        else if (arg == "--html" || arg == "--visualize-data-html") options.html = true;
        else if (arg == "--json") options.envelope = false;
        else if (arg == "--inspect-graph" || arg == "--visualize-data-json") options.envelope = true;
        else if (arg == "--load-imports") options.loadImports = true;
        else if (arg == "--check" || arg == "--check-json" || arg == "--lsp") {
            throw std::runtime_error(
                "Diagnostics are owned by felidae_debug. Run felidae_debug program.fx --check-json");
        }
        else if (arg == "--query" || (!arg.empty() && arg.front() == '?')) {
            throw std::runtime_error(
                "Celidae does not execute queries. Run felidae program.fx '? Query(...)'");
        }
        else if (!options.file) options.file = fs::path(arg);
        else throw std::runtime_error("Unexpected Celidae argument: " + arg);
    }
    return options;
}

void printHelp() {
    std::cout
        << "Celidae fact relationship visualizer " << Felidae::LANGUAGE_VERSION << "\n"
        << "Pipeline: Lexer -> Parser -> Celidae Visualization\n\n"
        << "Usage:\n"
        << "  celidae program.fx --inspect-graph [--load-imports]\n"
        << "  celidae program.fx --json [--load-imports]\n"
        << "  celidae program.fx --html [--load-imports]\n\n"
        << "Celidae parses facts, inheritance, properties, and rule references. "
        << "It never executes main(), methods, queries, or native libraries.\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parseOptions(argc, argv);
        if (options.help || !options.file) {
            printHelp();
            return options.help ? 0 : 1;
        }
        if (options.file->extension() != ".fx") {
            throw std::runtime_error("Celidae input files must use .fx extension");
        }

        auto loaded = Felidae::Tooling::loadProgram(*options.file, options.loadImports);
        const std::string json =
            Felidae::Celidae::graphJson(loaded.program, loaded.unresolvedImports);
        if (options.html) {
            std::cout << Felidae::Celidae::standaloneHtml(json);
        } else if (options.envelope) {
            std::cout << Felidae::Celidae::graphJsonEnvelope(json);
        } else {
            std::cout << json << "\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
