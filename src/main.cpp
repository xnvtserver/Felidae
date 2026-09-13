#include "Interpreter.h"
#include "DebugSession.h"
#include "FelidaeRuntime.h"
#include "Symbol.h"
#include "ToolingMain.h"
#include "Version.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace Felidae;

static bool isToolingInvocation(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--check" || arg == "--check-json" || arg == "--lsp" ||
            arg == "--list-libraries" || arg == "--list-builtins" ||
            arg == "--symbols-json" || arg == "--operators-json") {
            return true;
        }
    }
    return false;
}

struct CliOptions {
    bool showHelp = false;
    bool showVersion = false;
    bool repl = false;
    bool debug = false;
    bool metricsJson = false;
    bool serve = false;
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
        if (arg == "--serve") {
            options.serve = true;
            continue;
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
        if (!arg.empty() && arg.front() == '-') {
            throw std::runtime_error("Unknown option: " + arg);
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
              << "  felidae program.fx --check-json\n"
              << "  felidae --lsp\n"
              << "  felidae program.fx --metrics-json\n"
              << "  felidae program.fx --serve\n"
              << "  felidae program.fx --benchmark-repeat 100 --metrics-json\n"
              << "  felidae program.fx '? Query(key: x)' --benchmark-repeat 100 --metrics-json\n"
              << "  felidae --help\n"
              << "  felidae --version\n\n"
              << "Commands:\n"
              << "  program.fx                         Run program and execute main(...) if found\n"
              << "  program.fx '? Query(key: x)'        Run external query mode\n"
              << "  --repl program.fx                   Start interactive REPL\n"
              << "  program.fx --repl                   Start interactive REPL\n"
              << "  program.fx --debug                  Run with debug adapter diagnostics enabled\n"
              << "  program.fx --check                  Emit text parser and AST diagnostics\n"
              << "  program.fx --check-json             Emit JSON parser and AST diagnostics\n"
              << "  --lsp                               Run the stdio language server\n"
              << "  --list-libraries                    List importable core libraries as JSON\n"
              << "  --list-builtins                     List builtin functions as JSON\n"
              << "  program.fx --symbols-json           Emit source symbol metadata\n"
              << "  program.fx --operators-json         Emit dynamic operator metadata\n"
              << "  --metrics-json                      Emit load and runtime performance counters to stderr\n"
              << "  --serve                             Run source and reload the AST interpreter when source changes\n"
              << "  --benchmark-repeat N                Repeat the entry method or external query in one runtime\n"
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
                bool exhaustive = true;
                auto solutions = interpreter.solve(queryGoals, 1000, &exhaustive);
                printSolutions(interpreter, queryGoals, solutions, std::cout, exhaustive);
                continue;
            }
            if (isBareIdentifier(line) && interpreter.hasGlobal(line)) {
                std::cout << interpreter.valueToDisplayString(interpreter.evaluateGlobal(line)) << "\n";
                continue;
            }
            auto value = interpreter.evaluateExpressionText(line);
            std::cout << interpreter.valueToDisplayString(value) << "\n";
        } catch (const std::exception& e) {
            std::cout << "error: " << e.what() << "\n";
        }
    }
}

using SourceTimes = std::map<fs::path, fs::file_time_type>;

static SourceTimes sourceTimes(const fs::path& root, const Interpreter& interpreter) {
    SourceTimes result;
    const auto add = [&](const fs::path& source) {
        std::error_code error;
        const auto normalized = fs::absolute(source, error).lexically_normal();
        result.emplace(normalized, error ? fs::file_time_type::min()
                                         : fs::last_write_time(normalized, error));
        if (error) result[normalized] = fs::file_time_type::min();
    };
    add(root);
    for (const auto& source : interpreter.loadedSourceFiles()) add(source);
    return result;
}

static bool sourceChanged(const SourceTimes& watched) {
    for (const auto& [source, previous] : watched) {
        std::error_code error;
        const auto current = fs::last_write_time(source, error);
        if (error || current != previous) return true;
    }
    return false;
}

static void runServeEntry(Interpreter& interpreter, const CliOptions& options) {
    if (options.query) {
        const auto goals = parseQueryText(*options.query);
        bool exhaustive = true;
        const auto solutions = interpreter.solve(goals, 1000, &exhaustive);
        printSolutions(interpreter, goals, solutions, std::cout, exhaustive);
        return;
    }
    if (interpreter.hasMethod("main") || interpreter.hasAutoEntryCall()) {
        const auto result = interpreter.hasMethod("main")
            ? interpreter.callMain(makeSystemInput(options.remainingArgs))
            : interpreter.callAutoEntry();
        std::cout << interpreter.valueToDisplayString(result) << "\n";
    }
}

int main(int argc, char** argv) {
    if (isToolingInvocation(argc, argv)) {
        return Felidae::runToolingMain(argc, argv);
    }
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
        std::optional<DebugSession> debugSession;
        // Attached before loadProgramRoot below, not after: a program with
        // no main() executes its bare top-level calls during loading itself
        // (Interpreter::addProgram's EntryCall handling), so attaching any
        // later would miss stepping through those entirely.
        if (options.debug) {
            std::cerr << "Felidae debug session for " << options.programFile->string()
                      << " - stopped on entry, waiting on stdin.\n";
            debugSession.emplace();
            debugSession->attach(interpreter);
        }
        fs::path entryFile = resolveProgramEntryPath(*options.programFile);
        if (entryFile.extension() != FILE_EXTENSION) {
            throw std::runtime_error("Felidae source files must use .fx extension");
        }
        if (options.serve) {
            if (options.debug) {
                throw std::runtime_error("--serve cannot be combined with --debug");
            }
            if (options.repl) {
                throw std::runtime_error("--serve cannot be combined with --repl");
            }
            std::unique_ptr<Interpreter> interpreter;
            SourceTimes watched;
            const auto reload = [&]() {
                auto next = std::make_unique<Interpreter>();
                loadProgramRoot(entryFile, *next);
                watched = sourceTimes(entryFile, *next);
                interpreter = std::move(next);
                runServeEntry(*interpreter, options);
                std::cerr << "Felidae source interpreter ready: " << entryFile.string() << "\n";
            };
            reload();
            for (;;) {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                if (!sourceChanged(watched)) continue;
                try {
                    reload();
                } catch (const std::exception& error) {
                    // Keep the last successfully constructed interpreter live;
                    // a bad save must not discard the running source program.
                    watched = sourceTimes(entryFile, *interpreter);
                    std::cerr << "source reload failed: " << error.what() << "\n";
                }
            }
        }
        // A source file becomes executable only after its imports and every
        // declaration have registered successfully. Running main while the
        // parser was still producing chunks made later declarations invisible
        // and could leave effects behind when a later error rejected the file.
        // Registration remains streaming; publication is the execution boundary.
        loadProgramRoot(entryFile, interpreter);
        const auto executionStarted = Clock::now();
        double firstQueryMs = 0.0;
        double repeatedQueryAverageMs = 0.0;
        size_t measuredQueryRuns = 0;
        auto reportMetrics = [&]() {
            if (!options.metricsJson) return;
            const auto finished = Clock::now();
            const auto loadMicros = std::chrono::duration_cast<std::chrono::microseconds>(
                executionStarted - loadStarted).count();
            const auto executionMicros = std::chrono::duration_cast<std::chrono::microseconds>(
                finished - executionStarted).count();
            std::cerr << "FELIDAE_METRICS {"
                      << "\"loadMs\":" << (static_cast<double>(loadMicros) / 1000.0) << ","
                      << "\"executionMs\":" << (static_cast<double>(executionMicros) / 1000.0) << ","
                      << "\"queryRuns\":" << measuredQueryRuns << ","
                      << "\"firstQueryMs\":" << firstQueryMs << ","
                      << "\"repeatedQueryAverageMs\":" << repeatedQueryAverageMs << ","
                      << "\"runtime\":" << interpreter.runtimeMetricsJson()
                      << "}\n";
        };
        if (options.repl) {
            runRepl(interpreter);
            reportMetrics();
            return 0;
        }

        if (options.query) {
            auto queryGoals = parseQueryText(*options.query);
            std::vector<Solution> solutions;
            bool exhaustive = true;
            double repeatedQueryTotalMs = 0.0;
            for (size_t run = 0; run < options.benchmarkRepeat; ++run) {
                const auto queryStarted = Clock::now();
                solutions = interpreter.solve(queryGoals, 1000, &exhaustive);
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
            printSolutions(interpreter, queryGoals, solutions, std::cout, exhaustive);
            reportMetrics();
            return 0;
        }

        if (interpreter.hasMethod("main") || interpreter.hasAutoEntryCall()) {
            std::shared_ptr<Expr> result;
            double repeatedEntryTotalMs = 0.0;
            for (size_t run = 0; run < options.benchmarkRepeat; ++run) {
                const auto entryStarted = Clock::now();
                result = interpreter.hasMethod("main")
                    ? interpreter.callMain(makeSystemInput(options.remainingArgs))
                    : interpreter.callAutoEntry();
                const double entryMs = static_cast<double>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        Clock::now() - entryStarted).count()) / 1000000.0;
                if (run == 0) {
                    firstQueryMs = entryMs;
                } else {
                    repeatedEntryTotalMs += entryMs;
                }
            }
            measuredQueryRuns = options.benchmarkRepeat;
            if (options.benchmarkRepeat > 1) {
                repeatedQueryAverageMs =
                    repeatedEntryTotalMs / static_cast<double>(options.benchmarkRepeat - 1);
            }
            std::cout << interpreter.valueToDisplayString(result) << "\n";
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
