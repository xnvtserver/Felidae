#include "celidae/Visualization.h"
#include "tooling/SourceParser.h"
#include "Version.h"

#include <algorithm>
#include <filesystem>
#include <chrono>
#include <iostream>
#include <map>
#include <optional>
#include <string>

namespace fs = std::filesystem;

namespace {

struct Options {
    bool loadImports = false;
    bool metricsJson = false;
    bool recommend = false;
    // Which template --html emits. Unset means "all views in one file", which
    // is also what running celidae with no other flag now does - HTML is the
    // only visualization Celidae produces, so there is no longer a second
    // flag to opt into it.
    std::optional<Felidae::Celidae::DiagramType> templateType;
    bool help = false;
    bool version = false;
    std::optional<fs::path> file;
};

std::string diagramTypeList() {
    std::string names;
    for (Felidae::Celidae::DiagramType candidate : Felidae::Celidae::kAllDiagramTypes) {
        if (!names.empty()) names += ", ";
        names += Felidae::Celidae::diagramTypeName(candidate);
    }
    return names;
}

Options parseOptions(int argc, char** argv) {
    Options options;
    auto parseTemplate = [&](const std::string& value) {
        Felidae::Celidae::DiagramType parsed;
        if (!Felidae::Celidae::parseDiagramType(value, parsed)) {
            throw std::runtime_error(
                "Unknown Celidae template '" + value + "'. Use one of: " + diagramTypeList());
        }
        options.templateType = parsed;
    };
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") options.help = true;
        else if (arg == "--version" || arg == "-v") options.version = true;
        // Accepted and ignored: HTML is the only output Celidae produces now,
        // so asking for it explicitly is a no-op rather than an error. A
        // script written against the old CLI keeps working.
        else if (arg == "--html" || arg == "--visualize-data-html") { /* no-op */ }
        else if (arg == "--json" || arg == "--inspect-graph" || arg == "--visualize-data-json") {
            throw std::runtime_error(
                "Celidae no longer produces a JSON output mode - it produces one visualization, "
                "the interactive HTML page. Run: celidae program.fx [--template=<name>]");
        }
        else if (arg == "--svg") {
            throw std::runtime_error(
                "Celidae no longer produces a static SVG export - HTML is its only output. "
                "Run: celidae program.fx [--template=<name>]");
        }
        else if (arg == "--load-imports") options.loadImports = true;
        else if (arg == "--metrics-json") options.metricsJson = true;
        else if (arg == "--recommend") options.recommend = true;
        // --type selected which single view a JSON/SVG export covered; with
        // both gone the only per-view selector left is --template.
        else if (arg == "--type" || arg.rfind("--type=", 0) == 0) {
            throw std::runtime_error("--type was for --json/--svg, which Celidae no longer "
                                     "produces. Use --template=<name> to select one view.");
        }
        else if (arg == "--template") {
            if (++i >= argc) throw std::runtime_error("--template requires one of: " + diagramTypeList());
            parseTemplate(argv[i]);
        }
        else if (arg.rfind("--template=", 0) == 0) parseTemplate(arg.substr(11));
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
        << "Celidae produces one thing: a self-contained interactive HTML page. There is\n"
        << "no separate JSON or SVG export mode - every view's data is embedded in the page\n"
        << "for the browser to read, and that page is the only artifact worth keeping.\n\n"
        << "Usage:\n"
        << "  celidae program.fx [--load-imports] > report.html\n"
        << "  celidae program.fx --template=<name> [--load-imports] > view.html\n"
        << "  celidae program.fx --recommend [--load-imports]\n"
        << "  celidae --version\n\n"
        << "Options:\n"
        << "  --template=<name>         Emit just that one view instead of all of them.\n"
        << "  --load-imports            Resolve and include imported fact declarations.\n"
        << "  --recommend               Print, as JSON, which views this program's data\n"
        << "                            actually supports and why - useful for choosing a\n"
        << "                            --template without first rendering every view.\n\n"
        << "Diagram types (--template):\n";
    for (Felidae::Celidae::DiagramType type : Felidae::Celidae::kAllDiagramTypes) {
        const std::string name = Felidae::Celidae::diagramTypeName(type);
        std::cout << "  " << name << std::string(14 - std::min<std::size_t>(13, name.size()), ' ')
                  << Felidae::Celidae::diagramTypeSummary(type) << "\n";
    }
    std::cout
        << "\nThe first four are structural (what the program declares); the rest analyse the\n"
        << "literal values facts carry - distributions, outliers, correlations and segments.\n"
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
        if (options.recommend) {
            // Which views this program's data can actually support. Printed on
            // its own so a script (or an editor extension) can pick a template
            // without first rendering all nine and inspecting them.
            const auto recommendations =
                Felidae::Celidae::recommendViews(graph.shape());
            std::cout << "{\"recommendations\":[";
            for (std::size_t i = 0; i < recommendations.size(); ++i) {
                if (i) std::cout << ",";
                // `applicable` is the field a caller should branch on. A score
                // of 0 means the same thing, but a caller reading only the
                // score cannot tell "this view is a weak fit" from "this view
                // cannot answer anything about this program", and those need
                // different handling: the first is worth offering, the second
                // is worth explaining.
                std::cout << "{\"view\":\""
                          << Felidae::Celidae::diagramTypeName(recommendations[i].view)
                          << "\",\"score\":" << recommendations[i].score
                          << ",\"applicable\":"
                          << (recommendations[i].applicable ? "true" : "false")
                          << ",\"rationale\":\"" << recommendations[i].rationale << "\"}";
            }
            std::cout << "]}\n";
        } else {
            // The only visualization Celidae produces. Bundling every diagram
            // type into one self-contained file lets a reader compare
            // schema/er/segments/etc. without re-running Celidae;
            // --template=<name> narrows that to one view for a focused export.
            std::map<Felidae::Celidae::DiagramType, std::string> payloads;
            if (options.templateType) {
                payloads.emplace(
                    *options.templateType,
                    graph.json(*options.templateType, loaded.unresolvedImports));
            } else {
                for (Felidae::Celidae::DiagramType type : Felidae::Celidae::kAllDiagramTypes) {
                    payloads.emplace(type, graph.json(type, loaded.unresolvedImports));
                }
            }
            std::cout << Felidae::Celidae::standaloneHtml(payloads);
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
