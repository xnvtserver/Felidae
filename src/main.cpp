#include "Interpreter.h"
#include "LegacyIrAdapter.h"
#include "Symbol.h"
#include "FelidaeRuntime.h"
#include "Version.h"

#include <algorithm>
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
                "Visualization options are no longer supported");
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
              << "  --metrics-json                      Emit load and runtime performance counters to stderr\n"
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
                auto solutions = interpreter.solve(queryGoals, 1000);
                printSolutions(interpreter, queryGoals, solutions, std::cout);
                continue;
            }
            if (isBareIdentifier(line) && interpreter.hasGlobal(line)) {
                std::cout << interpreter.valueToDisplayString(interpreter.evaluateGlobal(line)) << "\n";
                continue;
            }
            if (const auto directIr = tryCompileExpressionTextToIr(line)) {
                class ReplNativeRuntime final : public VmRuntime {
                public:
                    explicit ReplNativeRuntime(Interpreter& services) : services_(services) {}
                    VmValue executeProgram(IrWord, const VmValue&) override {
                        throw IrError("direct REPL expression unexpectedly requested a runtime call");
                    }
                    VmValue loadSymbol(IrSymbolRef symbol) override {
                        const auto name = symbolNameForId(static_cast<SymbolId>(symbol));
                        if (!services_.hasGlobal(name)) throw IrError("IR references an undefined symbol: " + name);
                        const auto value = services_.evaluateGlobal(name);
                        if (const auto number = std::dynamic_pointer_cast<NumberExpr>(value)) return number->value;
                        if (const auto boolean = std::dynamic_pointer_cast<BoolExpr>(value)) return boolean->value;
                        if (std::dynamic_pointer_cast<NilExpr>(value)) return VmNil{};
                        if (const auto text = std::dynamic_pointer_cast<StringExpr>(value)) return VmText{{}, text->value};
                        return legacyVmValue(value);
                    }
                    VmValue callSymbol(IrSymbolRef symbol, std::span<const VmValue> arguments) override {
                        const auto name = symbolNameForId(static_cast<SymbolId>(symbol));
                        std::vector<Arg> values;
                        values.reserve(arguments.size());
                        for (const auto& argument : arguments) {
                            values.emplace_back("", legacyExprFromVmValue(argument));
                        }
                        return legacyVmValue(services_.callValue(TermExpr(
                            name, static_cast<SymbolId>(symbol), std::move(values))));
                    }
                    VmValue callSymbolNamed(IrSymbolRef symbol,
                                            std::span<const VmCallArgument> arguments) override {
                        const auto name = symbolNameForId(static_cast<SymbolId>(symbol));
                        std::vector<Arg> values;
                        values.reserve(arguments.size());
                        for (const auto& argument : arguments) {
                            const auto argumentName = argument.name
                                ? symbolNameForId(static_cast<SymbolId>(*argument.name)) : std::string{};
                            values.emplace_back(argumentName,
                                argument.name ? static_cast<SymbolId>(*argument.name) : 0,
                                legacyExprFromVmValue(argument.value));
                        }
                        return legacyVmValue(services_.callValue(TermExpr(
                            name, static_cast<SymbolId>(symbol), std::move(values))));
                    }
                private:
                    Interpreter& services_;
                } nativeRuntime(interpreter);
                RegisterVm vm;
                const auto value = vm.execute(*directIr, nativeRuntime, VmNil{});
                if (const auto* number = std::get_if<double>(&value)) {
                    std::cout << *number << "\n";
                    continue;
                }
                if (const auto* boolean = std::get_if<bool>(&value)) {
                    std::cout << (*boolean ? "true" : "false") << "\n";
                    continue;
                }
                if (const auto* text = std::get_if<VmText>(&value)) {
                    std::cout << text->utf8 << "\n";
                    continue;
                }
                if (const auto* opaque = std::get_if<VmOpaqueValue>(&value)) {
                    std::cout << interpreter.valueToDisplayString(legacyExprFromVmValue(*opaque)) << "\n";
                    continue;
                }
            }
            auto value = interpreter.evaluateExpressionText(line);
            std::cout << interpreter.valueToDisplayString(value) << "\n";
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
        // File execution goes through canonical IR and the register VM.  The
        // temporary runtime adapter owns legacy semantics while the direct IR
        // compiler is being migrated; neither the CLI nor model output can
        // bypass verification.
        if (!options.repl && !options.query) {
            auto module = compileProgramFileToIr(entryFile);
            IrVerifier::verify(module.ir);
            LegacyVmRuntime runtime(module);
            RegisterVm vm;
            const auto executionStarted = Clock::now();
            VmValue result = VmNil{};
            double firstEntryMs = 0.0;
            double repeatedEntryTotalMs = 0.0;
            for (size_t run = 0; run < options.benchmarkRepeat; ++run) {
                const auto entryStarted = Clock::now();
                result = vm.execute(module.ir, runtime,
                                    legacyVmValue(makeSystemInput(options.remainingArgs)));
                const double entryMs = static_cast<double>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        Clock::now() - entryStarted).count()) / 1000000.0;
                if (run == 0) firstEntryMs = entryMs;
                else repeatedEntryTotalMs += entryMs;
            }
            if (options.debug) {
                std::cerr << "Felidae debug mode enabled for " << entryFile.string() << "\n";
            }
            if (runtime.executedEntry()) {
                std::cout << runtime.services().valueToDisplayString(
                    legacyExprFromVmValue(result)) << "\n";
            } else {
                std::cout << "Program loaded successfully. No main() method found.\n"
                          << "Use a query argument, add a zero-argument entry call, or run with --repl.\n";
            }
            if (options.metricsJson) {
                const auto finished = Clock::now();
                const auto loadMicros = std::chrono::duration_cast<std::chrono::microseconds>(
                    executionStarted - loadStarted).count();
                const auto executionMicros = std::chrono::duration_cast<std::chrono::microseconds>(
                    finished - executionStarted).count();
                const double repeatedAverage = options.benchmarkRepeat > 1
                    ? repeatedEntryTotalMs / static_cast<double>(options.benchmarkRepeat - 1) : 0.0;
                std::cerr << "FELIDAE_METRICS {"
                          << "\"loadMs\":" << (static_cast<double>(loadMicros) / 1000.0) << ","
                          << "\"executionMs\":" << (static_cast<double>(executionMicros) / 1000.0) << ","
                          << "\"queryRuns\":" << options.benchmarkRepeat << ","
                          << "\"firstQueryMs\":" << firstEntryMs << ","
                          << "\"repeatedQueryAverageMs\":" << repeatedAverage << ","
                          << "\"runtime\":" << runtime.services().runtimeMetricsJson()
                          << "}\n";
            }
            return 0;
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
