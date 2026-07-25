#include "Interpreter.h"
#include "FelidaeRuntime.h"
#include "Version.h"

#include <chrono>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace Felidae;

struct CliOptions {
    bool showHelp = false;
    bool showVersion = false;
    bool repl = false;
    bool debug = false;
    bool metricsJson = false;
    size_t benchmarkRepeat = 1;
    std::optional<fs::path> programFile;
    std::optional<std::string> query;
    std::vector<std::string> remainingArgs;
};

static CliOptions parseCli(int argc, char** argv) {
    CliOptions options;
    if (argc <= 1) {
        options.showHelp = true;
        return options;
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            options.showHelp = true;
            return options;
        }
        if (arg == "--version" || arg == "-v") {
            options.showVersion = true;
            return options;
        }
        if (arg == "--repl") {
            options.repl = true;
            continue;
        }
        if (arg == "--debug") {
            options.debug = true;
            continue;
        }
        if (arg == "--metrics-json") {
            options.metricsJson = true;
            continue;
        }
        if (arg == "--visualize-data-json" || arg == "--visualize-data-html" ||
            arg == "--inspect-graph" || arg == "--load-imports") {
            throw std::runtime_error(
                "Visualization is owned by Celidae. Run celidae program.fx --json or --html --load-imports");
        }
        if (arg == "--benchmark-repeat") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--benchmark-repeat expects a positive integer");
            }
            const std::string count = argv[++i];
            size_t consumed = 0;
            const unsigned long long parsed = std::stoull(count, &consumed);
            if (consumed != count.size() || parsed == 0) {
                throw std::runtime_error("--benchmark-repeat expects a positive integer");
            }
            options.benchmarkRepeat = static_cast<size_t>(parsed);
            continue;
        }
        if (!options.programFile) {
            options.programFile = fs::path(arg);
            continue;
        }
        if (!options.query && !arg.empty() && arg[0] == '?') {
            options.query = arg;
            continue;
        }
        options.remainingArgs.push_back(arg);
    }
    return options;
}

static const char* bannerText() {
    return R"(        ______    _ _     _            
       |  ____|  | (_)   | |           
       | |__ ___ | |_  __| | __ _  ___ 
       |  __/ _ \| | |/ _` |/ _` |/ _ \
       | | |  __/| | | (_| | (_| |  __/
       |_|  \___||_|_|\__,_|\__, |\___|

       :=Bow(:)|Grr(..)|Roar(<)|Meow(>).                             

)";
}
//       :=Bow(🐶)|Grr(🐯)|Roar(🦁)|Meow(🐱).

static void printVersion() {
    std::cout << LANGUAGE_NAME << " v" << LANGUAGE_VERSION << "\n";
}

static void printHelp() {
    std::cout << bannerText() << "\n"
              << LANGUAGE_NAME << " v" << LANGUAGE_VERSION << "\n"
              << LANGUAGE_DESCRIPTION << "\n\n"
              << "File extension:\n"
              << "  " << FILE_EXTENSION << "\n\n"
              << "Usage:\n"
              << "  felidae program.fx\n"
              << "  felidae program.fx '? Query(key: x)'\n"
              << "  felidae --repl program.fx\n"
              << "  felidae program.fx --repl\n"
              << "  felidae program.fx --debug\n"
              << "  felidae program.fx --metrics-json\n"
              << "  felidae program.fx '? Query(key: x)' --benchmark-repeat 100 --metrics-json\n"
              << "  felidae --help\n"
              << "  felidae --version\n\n"
              << "Commands:\n"
              << "  program.fx                         Run program and execute main(...) if found\n"
              << "  program.fx '? Query(key: x)'        Run external query mode\n"
              << "  --repl program.fx                   Start interactive REPL\n"
              << "  program.fx --repl                   Start interactive REPL\n"
              << "  program.fx --debug                  Run with debug adapter diagnostics enabled\n"
              << "  --metrics-json                      Emit load and runtime performance counters to stderr\n"
              << "  --benchmark-repeat N                Repeat an external query in one runtime for benchmarking\n"
              << "  --help                              Show this help screen\n"
              << "  --version                           Show version information\n\n"
              << "Total commands supported: " << TOTAL_COMMANDS_SUPPORTED << "\n\n"
              << "Examples:\n"
              << "  felidae examples/main.fx\n"
              << "  felidae examples/main.fx '? Person(name: x)'\n"
              << "  felidae --repl examples/main.fx\n";
}

static void runRepl(Interpreter& interpreter) {
    std::cout << LANGUAGE_NAME << " v" << LANGUAGE_VERSION << "\n"
              << LANGUAGE_DESCRIPTION << "\n"
              << "Type 'help' for commands, 'exit' or 'quit' to leave.\n\n";
    std::string line;
    while (true) {
        std::cout << "felidae> ";
        if (!std::getline(std::cin, line)) {
            std::cout << "\n";
            break;
        }
        line = trim(line);
        if (line.empty()) continue;
        if (line == "exit" || line == "quit") break;
        if (line == "version") {
            printVersion();
            continue;
        }
        if (line == "help") {
            std::cout << "REPL commands:\n"
                      << "  ? Predicate(field: x)        Run old-style query\n"
                      << "  MethodName(args...)          Invoke method\n"
                      << "  GlobalName                   Print global value\n"
                      << "  function(args...)            Call built-in function\n"
                      << "  help                         Show REPL help\n"
                      << "  version                      Show version\n"
                      << "  exit                         Exit REPL\n"
                      << "  quit                         Exit REPL\n";
            continue;
        }

        try {
            if (!line.empty() && line[0] == '?') {
                auto queryGoals = parseQueryText(line);
                auto solutions = interpreter.solve(queryGoals, 1000);
                printSolutions(interpreter, queryGoals, solutions, std::cout);
                continue;
            }
            if (isBareIdentifier(line) && interpreter.hasGlobal(line)) {
                std::cout << interpreter.valueToString(interpreter.evaluateGlobal(line)) << "\n";
                continue;
            }
            auto value = interpreter.evaluateExpressionText(line);
            std::cout << interpreter.valueToString(value) << "\n";
        } catch (const std::exception& e) {
            std::cout << "error: " << e.what() << "\n";
        }
    }
}

int main(int argc, char** argv) {
    try {
        CliOptions options = parseCli(argc, argv);
        if (options.showHelp) {
            printHelp();
            return 0;
        }
        if (options.showVersion) {
            printVersion();
            return 0;
        }
        if (!options.programFile) {
            printHelp();
            return 1;
        }

        using Clock = std::chrono::steady_clock;
        const auto loadStarted = Clock::now();
        Interpreter interpreter;
        fs::path entryFile = resolveProgramEntryPath(*options.programFile);
        if (entryFile.extension() != FILE_EXTENSION) {
            throw std::runtime_error("Felidae source files must use .fx extension");
        }
        loadProgramRoot(entryFile, interpreter);
        const auto executionStarted = Clock::now();
        double firstQueryMs = 0.0;
        double repeatedQueryAverageMs = 0.0;
        size_t measuredQueryRuns = 0;
        auto reportMetrics = [&]() {
            if (!options.metricsJson) return;
            const auto finished = Clock::now();
            const auto loadMicros =
                std::chrono::duration_cast<std::chrono::microseconds>(executionStarted - loadStarted).count();
            const auto executionMicros =
                std::chrono::duration_cast<std::chrono::microseconds>(finished - executionStarted).count();
            std::cerr << "FELIDAE_METRICS {"
                      << "\"loadMs\":" << (static_cast<double>(loadMicros) / 1000.0) << ","
                      << "\"executionMs\":" << (static_cast<double>(executionMicros) / 1000.0) << ","
                      << "\"queryRuns\":" << measuredQueryRuns << ","
                      << "\"firstQueryMs\":" << firstQueryMs << ","
                      << "\"repeatedQueryAverageMs\":" << repeatedQueryAverageMs << ","
                      << "\"runtime\":" << interpreter.runtimeMetricsJson()
                      << "}\n";
        };
        if (options.debug) {
            std::cerr << "Felidae debug mode enabled for " << entryFile.string() << "\n";
        }

        if (options.repl) {
            runRepl(interpreter);
            reportMetrics();
            return 0;
        }

        if (options.query) {
            auto queryGoals = parseQueryText(*options.query);
            std::vector<Solution> solutions;
            double repeatedQueryTotalMs = 0.0;
            for (size_t run = 0; run < options.benchmarkRepeat; ++run) {
                const auto queryStarted = Clock::now();
                solutions = interpreter.solve(queryGoals, 1000);
                const auto queryFinished = Clock::now();
                const double queryMs = static_cast<double>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        queryFinished - queryStarted).count()) / 1000000.0;
                if (run == 0) {
                    firstQueryMs = queryMs;
                } else {
                    repeatedQueryTotalMs += queryMs;
                }
            }
            measuredQueryRuns = options.benchmarkRepeat;
            if (options.benchmarkRepeat > 1) {
                repeatedQueryAverageMs =
                    repeatedQueryTotalMs / static_cast<double>(options.benchmarkRepeat - 1);
            }
            printSolutions(interpreter, queryGoals, solutions, std::cout);
            reportMetrics();
            return 0;
        }

        if (interpreter.hasMethod("main")) {
            auto result = interpreter.callMain(makeSystemInput(options.remainingArgs));
            std::cout << interpreter.valueToString(result) << "\n";
        } else if (interpreter.hasAutoEntryCall()) {
            auto result = interpreter.callAutoEntry();
            std::cout << interpreter.valueToString(result) << "\n";
        } else {
            std::cout << "Program loaded successfully. No main() method found.\n"
                      << "Use a query argument, add a zero-argument entry call, or run with --repl.\n";
        }

        reportMetrics();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
