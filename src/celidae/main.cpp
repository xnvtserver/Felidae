#include "celidae/Visualization.h"
#include "tooling/SourceParser.h"
#include "Version.h"

#include <filesystem>
#include <chrono>
#include <iostream>
#include <optional>
#include <string>

namespace fs = std::filesystem;

namespace {

struct Options {
    bool html = false;
    bool svg = false;
    bool envelope = true;
    bool loadImports = false;
    bool metricsJson = false;
    Felidae::Celidae::DiagramType type = Felidae::Celidae::DiagramType::Schema;
    bool help = false;
    bool version = false;
    std::optional<fs::path> file;
};

Options parseOptions(int argc, char** argv) {
    Options options;
    auto parseType = [&](const std::string& value) {
        if (value == "schema") options.type = Felidae::Celidae::DiagramType::Schema;
        else if (value == "graph") options.type = Felidae::Celidae::DiagramType::Graph;
        else if (value == "er") options.type = Felidae::Celidae::DiagramType::Er;
        else throw std::runtime_error("Unknown Celidae diagram type '" + value + "'. Use schema, graph, or er");
    };
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") options.help = true;
        else if (arg == "--version" || arg == "-v") options.version = true;
        else if (arg == "--html" || arg == "--visualize-data-html") options.html = true;
        else if (arg == "--svg") options.svg = true;
        else if (arg == "--json") options.envelope = false;
        else if (arg == "--inspect-graph" || arg == "--visualize-data-json") options.envelope = true;
        else if (arg == "--load-imports") options.loadImports = true;
        else if (arg == "--metrics-json") options.metricsJson = true;
        else if (arg == "--type") {
            if (++i >= argc) throw std::runtime_error("--type requires schema, graph, or er");
            parseType(argv[i]);
        }
        else if (arg.rfind("--type=", 0) == 0) parseType(arg.substr(7));
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

void printVersion() {
    std::cout << "{\"name\":\"celidae\",\"version\":\"" << Felidae::LANGUAGE_VERSION << "\"}\n";
}

void printHelp() {
    std::cout
        << "Celidae fact relationship visualizer " << Felidae::LANGUAGE_VERSION << "\n"
        << "Pipeline: Lexer -> Parser -> Celidae Visualization\n\n"
        << "Usage:\n"
        << "  celidae program.fx --inspect-graph [--load-imports]\n"
        << "  celidae program.fx --json [--load-imports]\n"
        << "  celidae program.fx --html [--load-imports]\n"
        << "  celidae program.fx --svg --type=er [--load-imports]\n"
        << "  celidae --version\n\n"
        << "Output formats:\n"
        << "  --inspect-graph / --json  JSON graph for one --type (schema/graph/er).\n"
        << "  --html                    Self-contained interactive visualizer bundling all\n"
        << "                            three diagram types with force/tree/circle layouts.\n"
        << "  --svg                     Static vector export for one --type, suitable for\n"
        << "                            embedding directly in documents or slides.\n\n"
        << "Diagram types: schema (default), graph (dependencies), er (facts, fields, inheritance).\n"
        << "Celidae parses facts, inheritance, properties, and rule references. "
        << "It never executes main(), methods, queries, or native libraries.\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parseOptions(argc, argv);
        if (options.version) {
            printVersion();
            return 0;
        }
        if (options.help || !options.file) {
            printHelp();
            return options.help ? 0 : 1;
        }
        if (options.file->extension() != ".fx") {
            throw std::runtime_error("Celidae input files must use .fx extension");
        }

        Felidae::Celidae::SchemaGraphAccumulator graph;
        const auto started = std::chrono::steady_clock::now();
        auto loaded = Felidae::Tooling::loadProgramStatements(
            *options.file,
            options.loadImports,
            [&](const std::shared_ptr<Felidae::Statement>& statement) {
                graph.consume(statement);
            });
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count();
        if (options.html) {
            // Bundle all three diagram types into one self-contained file so
            // a user can compare schema/graph/er without re-running Celidae.
            const std::string schemaJson = graph.json(Felidae::Celidae::DiagramType::Schema, loaded.unresolvedImports);
            const std::string graphTypeJson = graph.json(Felidae::Celidae::DiagramType::Graph, loaded.unresolvedImports);
            const std::string erJson = graph.json(Felidae::Celidae::DiagramType::Er, loaded.unresolvedImports);
            std::cout << Felidae::Celidae::standaloneHtml(schemaJson, graphTypeJson, erJson);
        } else if (options.svg) {
            const auto rendered = graph.buildGraph(options.type, loaded.unresolvedImports);
            std::cout << Felidae::Celidae::standaloneSvg(rendered, options.type);
        } else if (options.envelope) {
            std::cout << Felidae::Celidae::graphJsonEnvelope(graph.json(options.type, loaded.unresolvedImports));
        } else {
            std::cout << graph.json(options.type, loaded.unresolvedImports) << "\n";
        }
        if (options.metricsJson) {
            std::cerr << "FELIDAE_CELIDAE_METRICS {\"analysisMs\":"
                      << static_cast<double>(elapsed) / 1000.0 << "}\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
