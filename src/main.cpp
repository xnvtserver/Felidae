#include "Interpreter.h"
#include "DebugSession.h"
#include "DatabaseService.h"
#include "Environment.h"
#include "FelidaeRuntime.h"
#include "ProjectConfiguration.h"
#include "ReplLineEditor.h"
#include "Symbol.h"
#include "TerminalUi.h"
#include "Version.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
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
        options.repl = true;
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
    if (!options.programFile) options.repl = true;
    if (options.repl && options.programFile) {
        throw std::runtime_error(
            "The REPL does not accept a program file; run felidae without a file");
    }
    if (options.debug && !options.programFile) {
        throw std::runtime_error("--debug requires a program.fx file");
    }
    if (options.debug && options.repl) {
        throw std::runtime_error("--debug and --repl cannot be used together");
    }
    if (options.repl && options.query) {
        throw std::runtime_error("--repl cannot be combined with an external query");
    }
    if ((options.debug || options.repl) && options.benchmarkRepeat != 1) {
        throw std::runtime_error(
            "--benchmark-repeat cannot be combined with --debug or --repl");
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
static void printVersion(std::ostream& output) {
    output << LANGUAGE_NAME << " v" << LANGUAGE_VERSION << "\n";
}

static void printHelp(std::ostream& output) {
    output << bannerText() << "\n"
              << LANGUAGE_NAME << " v" << LANGUAGE_VERSION << "\n"
              << LANGUAGE_DESCRIPTION << "\n\n"
              << "File extension:\n"
              << "  " << FILE_EXTENSION << "\n\n"
              << "Usage:\n"
              << "  felidae\n"
              << "  felidae program.fx\n"
              << "  felidae db stop [program.fx|project-directory]\n"
              << "  felidae program.fx '? Query(key: x)'\n"
              << "  felidae --repl\n"
              << "  felidae program.fx --debug\n"
              << "  felidae program.fx --metrics-json\n"
              << "  felidae program.fx --benchmark-repeat 100 --metrics-json\n"
              << "  felidae program.fx '? Query(key: x)' --benchmark-repeat 100 --metrics-json\n"
              << "  felidae --help\n"
              << "  felidae --version\n\n"
              << "Commands:\n"
              << "  (no file)                          Start the REPL using ./init.fx\n"
              << "  program.fx                         Run program and execute main(...) if found\n"
              << "  db stop [PROJECT]                  Stop the RocksDB owner selected by init.fx\n"
              << "  program.fx '? Query(key: x)'        Run external query mode\n"
              << "  --repl                              Explicitly start the interactive REPL\n"
              << "  program.fx --debug                  Run with live interpreter debugging enabled\n"
              << "  --metrics-json                      Emit load and runtime performance counters to stderr\n"
              << "  --benchmark-repeat N                Repeat the entry method or external query in one runtime\n"
              << "  --help                              Show this help screen\n"
              << "  --version                           Show version information\n\n"
              << "Total commands supported: " << TOTAL_COMMANDS_SUPPORTED << "\n\n"
              << "Examples:\n"
              << "  felidae\n"
              << "  felidae v2_examples/felidae_language_tour.fx\n"
              << "  felidae tests/solver_durable_backtracking.fx '? Candidate(id: x)'\n";
}

static void printReplHelp(TerminalUi& ui) {
    ui.heading("REPL input");
    ui.command("expression", "Evaluate an expression");
    ui.command("? Predicate(field: x)", "Run a logical query");
    ui.command("Fact(field: value).", "Install a persistent top-level fact");
    ui.command("name := expression", "Install an immutable global binding");
    ui.command("import \"library\"", "Load a normal Felidae import");
    ui.command("def name(...) =>", "Start a multiline function");
    ui.command("class Name", "Start a multiline class declaration");
    ui.command("end", "Finish the current declaration");
    ui.command(":cancel", "Discard an unfinished declaration");
    ui.command(":metrics", "Show last-action and RocksDB statistics");
    ui.command(":metrics on|off", "Toggle compact statistics after each action");
    ui.command(":debug on|off", "Toggle real goal-hook tracing");
    ui.command(":debug locals on|off", "Include live local bindings in traces");
    ui.command(":show", "Show the unfinished declaration with line numbers");
    ui.command(":clear", "Clear the terminal while preserving session state");
    ui.command(":version", "Show version information");
    ui.command(":help", "Show this command list");
    ui.command(":quit", "Exit the REPL");
    ui.note("Legacy help, version, exit, and quit commands are also accepted.");
}

struct ReplMeasurement {
    std::string action;
    std::chrono::microseconds elapsed{0};
    Interpreter::MetricValues before;
    Interpreter::MetricValues after;
    bool valid = false;
};

static std::uint64_t metricValue(const Interpreter::MetricValues& values,
                                 const std::string& name) {
    const auto found = values.find(name);
    return found == values.end() ? 0 : found->second;
}

static std::uint64_t metricDelta(const ReplMeasurement& measurement,
                                 const std::string& name) {
    const auto before = metricValue(measurement.before, name);
    const auto after = metricValue(measurement.after, name);
    return after >= before ? after - before : 0;
}

static std::string readableBytes(std::uint64_t bytes) {
    static constexpr const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < std::size(units)) {
        value /= 1024.0;
        ++unit;
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(unit == 0 ? 0 : 2)
        << value << ' ' << units[unit];
    return out.str();
}

static void printReplMetrics(TerminalUi& ui, const Interpreter& interpreter,
                             const ReplMeasurement& measurement,
                             bool compact) {
    if (!measurement.valid) {
        ui.warning("no REPL action has been measured yet");
    } else {
        const auto micros = static_cast<std::uint64_t>(measurement.elapsed.count());
        if (compact) {
            std::ostringstream summary;
            summary << measurement.action << ": "
                    << (static_cast<double>(micros) / 1000.0) << " ms, "
                    << metricDelta(measurement, "rocks_point_reads") << " point reads, "
                    << metricDelta(measurement, "rocks_fact_rows_scanned")
                    << " fact rows scanned";
            ui.note(summary.str());
        } else {
            ui.heading("Last REPL action");
            ui.metric("action", measurement.action);
            ui.metric("elapsed", std::to_string(micros) + " us");
            const std::pair<const char*, const char*> counters[] = {
                {"clause attempts", "clause_attempts"},
                {"unification attempts", "unification_attempts"},
                {"fact candidates", "fact_candidates"},
                {"solutions materialized", "solution_materializations"},
                {"environment copies", "environment_copies"},
                {"dispatch cache hits", "dispatch_cache_hits"},
                {"dispatch cache misses", "dispatch_cache_misses"},
                {"RocksDB point reads", "rocks_point_reads"},
                {"RocksDB type scans", "rocks_type_scans"},
                {"RocksDB full scans", "rocks_full_scans"},
                {"RocksDB index scans", "rocks_index_scans"},
                {"fact rows scanned", "rocks_fact_rows_scanned"},
                {"index rows scanned", "rocks_index_rows_scanned"},
                {"links visited", "rocks_links_visited"},
                {"fact writes", "rocks_fact_writes"},
                {"link writes", "rocks_link_writes"},
            };
            for (const auto& [label, key] : counters) {
                ui.metric(label, std::to_string(metricDelta(measurement, key)));
            }
            if (metricDelta(measurement, "rocks_full_scans") != 0) {
                ui.warning("this action used a full RocksDB scan; consider a key or index lookup");
            }
        }
    }

    if (compact) return;
    ui.heading("RocksDB state");
    const auto database = interpreter.databaseStatistics();
    const std::pair<const char*, const char*> properties[] = {
        {"estimated keys", "estimated_keys"},
        {"estimated live data", "estimated_live_data_bytes"},
        {"memtables", "memtable_bytes"},
        {"block cache", "block_cache_bytes"},
        {"live SST files", "live_sst_bytes"},
        {"total SST files", "total_sst_bytes"},
        {"running compactions", "running_compactions"},
        {"running flushes", "running_flushes"},
        {"write stopped", "write_stopped"},
    };
    for (const auto& [label, key] : properties) {
        const auto found = database.find(key);
        if (found == database.end()) continue;
        const std::string_view keyView(key);
        const bool bytes = keyView.find("bytes") != std::string_view::npos ||
                           keyView.find("sst") != std::string_view::npos ||
                           keyView == "block_cache_bytes";
        ui.metric(label, bytes ? readableBytes(found->second)
                               : std::to_string(found->second));
    }
    ui.note("These are observed counters and storage state, not a predictive SQL cost estimate.");
}

static std::string formatReplDiagnostic(std::string message,
                                        std::string_view source) {
    constexpr std::string_view marker = " at source byte ";
    const auto markerPosition = message.rfind(marker);
    if (markerPosition == std::string::npos) return message;
    const std::string offsetText = message.substr(markerPosition + marker.size());
    std::size_t consumed = 0;
    std::size_t offset = 0;
    try {
        offset = std::stoull(offsetText, &consumed);
    } catch (const std::exception&) {
        return message;
    }
    if (consumed == 0) return message;
    offset = std::min(offset, source.size());
    std::size_t line = 1;
    std::size_t column = 1;
    for (std::size_t index = 0; index < offset; ++index) {
        if (source[index] == '\n') {
            ++line;
            column = 1;
        } else {
            ++column;
        }
    }
    message.erase(markerPosition);
    return "line " + std::to_string(line) + ", column " +
           std::to_string(column) + ": " + message;
}

static void runRepl(Interpreter& interpreter, std::istream& input,
                    std::ostream& output) {
    TerminalUi ui(output);
    ui.banner(LANGUAGE_NAME, LANGUAGE_VERSION, LANGUAGE_DESCRIPTION);
    std::string line;
    std::string source;
    bool readingSource = false;
    bool debugEnabled = false;
    bool debugLocals = false;
    bool automaticMetrics = false;
    ReplLineEditor lineEditor(ui, input);
    ReplMeasurement lastMeasurement;
    const auto finishMeasurement = [&](std::string action,
                                       std::chrono::steady_clock::time_point started,
                                       Interpreter::MetricValues before) {
        lastMeasurement.action = std::move(action);
        lastMeasurement.elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started);
        lastMeasurement.before = std::move(before);
        lastMeasurement.after = interpreter.runtimeCounters();
        lastMeasurement.valid = true;
    };
    const auto printAutomaticMetrics = [&] {
        if (automaticMetrics) printReplMetrics(ui, interpreter, lastMeasurement, true);
    };
    const auto updateDebugHook = [&] {
        if (!debugEnabled) {
            interpreter.setGoalHook({});
            return;
        }
        interpreter.setGoalHook([&](const Goal& goal, const Env& env, std::size_t depth) {
            std::ostringstream event;
            event << "depth " << depth;
            if (goal.sourceSpan.valid()) event << ", line " << goal.sourceSpan.startLine;
            event << " | " << goal.debug();
            ui.debug(event.str());
            if (!debugLocals) return;
            for (const auto& binding : env) {
                if (isInternalGeneratedSymbolId(binding.first)) continue;
                const std::string name = symbolNameForId(binding.first);
                if (!name.empty()) {
                    ui.metric("local " + name,
                              interpreter.valueToDebugString(binding.second));
                }
            }
        });
    };
    const auto executeInteractiveExpression = [&](const std::string& command) {
        if (command[0] == '?') {
            auto before = interpreter.runtimeCounters();
            const auto started = std::chrono::steady_clock::now();
            auto activity = debugEnabled
                ? std::shared_ptr<TerminalUi::Activity>{}
                : ui.activity("solving query");
            auto queryGoals = parseQueryText(
                command, interpreter.tokenizer(), interpreter.operatorRegistry());
            auto solutions = interpreter.solve(queryGoals, 1000);
            activity.reset();
            finishMeasurement("query", started, std::move(before));
            ui.queryHeading();
            printSolutions(interpreter, queryGoals, solutions, output);
            printAutomaticMetrics();
            return;
        }
        if (isBareIdentifier(command) && interpreter.hasGlobal(command)) {
            auto before = interpreter.runtimeCounters();
            const auto started = std::chrono::steady_clock::now();
            auto activity = debugEnabled
                ? std::shared_ptr<TerminalUi::Activity>{}
                : ui.activity("evaluating global");
            const auto value = interpreter.evaluateGlobal(command);
            activity.reset();
            finishMeasurement("global", started, std::move(before));
            ui.result(interpreter.valueToDisplayString(value));
            printAutomaticMetrics();
            return;
        }
        auto before = interpreter.runtimeCounters();
        const auto started = std::chrono::steady_clock::now();
        auto activity = debugEnabled
            ? std::shared_ptr<TerminalUi::Activity>{}
            : ui.activity("evaluating expression");
        auto value = interpreter.evaluateExpressionText(command);
        activity.reset();
        finishMeasurement("expression", started, std::move(before));
        ui.result(interpreter.valueToDisplayString(value));
        printAutomaticMetrics();
    };
    while (true) {
        if (!lineEditor.readLine(readingSource, line)) {
            if (readingSource) {
                output << '\n';
                ui.warning("unfinished input discarded");
            }
            output << "\n";
            break;
        }
        const std::string command = trim(line);
        if (readingSource) {
            if (command == ":cancel") {
                source.clear();
                readingSource = false;
                ui.warning("unfinished input discarded");
                continue;
            }
            if (command == ":show") {
                ui.heading("Unfinished input");
                ui.codeBlock(source);
                continue;
            }
            source += line;
            source.push_back('\n');
            auto before = interpreter.runtimeCounters();
            const auto started = std::chrono::steady_clock::now();
            auto activity = debugEnabled
                ? std::shared_ptr<TerminalUi::Activity>{}
                : ui.activity("parsing input");
            try {
                const auto load = loadInteractiveProgramText(
                    source, fs::current_path(), interpreter);
                if (load == InteractiveProgramLoad::Incomplete) continue;
                if (load == InteractiveProgramLoad::Expression) {
                    activity.reset();
                    executeInteractiveExpression(trim(source));
                } else if (load == InteractiveProgramLoad::Empty) {
                    activity.reset();
                } else {
                    activity.reset();
                    finishMeasurement("declaration", started, std::move(before));
                    ui.success("declaration installed");
                    printAutomaticMetrics();
                }
            } catch (const std::exception& e) {
                activity.reset();
                ui.codeBlock(source);
                ui.error(formatReplDiagnostic(e.what(), source));
            }
            source.clear();
            readingSource = false;
            continue;
        }

        if (command.empty()) continue;
        if (command == ":quit" || command == ":exit" ||
            command == "exit" || command == "quit") break;
        if (command == ":version" || command == "version") {
            ui.result(std::string(LANGUAGE_NAME) + " v" + LANGUAGE_VERSION);
            continue;
        }
        if (command == ":help" || command == "help") {
            printReplHelp(ui);
            continue;
        }
        if (command == ":clear") {
            ui.clearScreen();
            ui.banner(LANGUAGE_NAME, LANGUAGE_VERSION, LANGUAGE_DESCRIPTION);
            continue;
        }
        if (command == ":metrics" || command == ":metrics status") {
            printReplMetrics(ui, interpreter, lastMeasurement, false);
            continue;
        }
        if (command == ":metrics on" || command == ":metrics off") {
            automaticMetrics = command == ":metrics on";
            ui.success(std::string("automatic metrics ") +
                       (automaticMetrics ? "enabled" : "disabled"));
            continue;
        }
        if (command == ":debug" || command == ":debug status") {
            ui.heading("REPL debugger");
            ui.metric("goal tracing", debugEnabled ? "on" : "off");
            ui.metric("live locals", debugLocals ? "on" : "off");
            continue;
        }
        if (command == ":debug on" || command == ":debug off") {
            debugEnabled = command == ":debug on";
            updateDebugHook();
            ui.success(std::string("goal tracing ") +
                       (debugEnabled ? "enabled" : "disabled"));
            if (debugEnabled) {
                ui.warning("debug tracing adds terminal I/O and changes elapsed timings");
            }
            continue;
        }
        if (command == ":debug locals on" || command == ":debug locals off") {
            debugLocals = command == ":debug locals on";
            if (debugLocals && !debugEnabled) debugEnabled = true;
            updateDebugHook();
            ui.success(std::string("live-local tracing ") +
                       (debugLocals ? "enabled" : "disabled"));
            continue;
        }
        if (command.front() == ':') {
            ui.error("Unknown REPL command '" + command + "'. Type :help for commands.");
            continue;
        }

        try {
            source = line;
            source.push_back('\n');
            auto before = interpreter.runtimeCounters();
            const auto started = std::chrono::steady_clock::now();
            const auto load = loadInteractiveProgramText(
                source, fs::current_path(), interpreter);
            if (load == InteractiveProgramLoad::Empty) {
                source.clear();
                continue;
            }
            if (load == InteractiveProgramLoad::Incomplete) {
                readingSource = true;
                continue;
            }
            if (load == InteractiveProgramLoad::Loaded) {
                finishMeasurement("declaration", started, std::move(before));
                ui.success("declaration installed");
                printAutomaticMetrics();
                source.clear();
                continue;
            }
            source.clear();
        } catch (const std::exception& e) {
            ui.codeBlock(source);
            const std::string diagnosticSource = source;
            source.clear();
            ui.error(formatReplDiagnostic(e.what(), diagnosticSource));
            continue;
        }

        try {
            executeInteractiveExpression(command);
        } catch (const std::exception& e) {
            ui.codeBlock(command);
            ui.error(formatReplDiagnostic(e.what(), command));
        }
    }
    interpreter.setGoalHook({});
}

static ProjectConfiguration projectConfigurationFor(CliOptions& options) {
    fs::path projectDirectory;
    if (options.programFile) {
        const fs::path entryFile = resolveProgramEntryPath(*options.programFile);
        if (entryFile.extension() != FILE_EXTENSION) {
            throw std::runtime_error("Felidae source files must use .fx extension");
        }
        options.programFile = entryFile;
        projectDirectory = entryFile.parent_path();
    } else {
        projectDirectory = fs::current_path();
    }
    return loadProjectConfiguration(projectDirectory);
}

static std::vector<std::string> databaseServiceArguments(
    const CliOptions& options) {
    if (!options.programFile || options.debug || options.repl) {
        throw std::logic_error(
            "Only non-interactive program execution may use the database service");
    }

    // A database service can outlive the client that started it and therefore
    // retains that client's working directory. Always send the resolved entry
    // path: a relative path must continue to select its own sibling init.fx
    // when a later client reaches the same database from another directory.
    std::vector<std::string> arguments;
    arguments.push_back(options.programFile->string());
    if (options.query) arguments.push_back(*options.query);
    arguments.insert(arguments.end(), options.remainingArgs.begin(),
                     options.remainingArgs.end());
    if (options.metricsJson) arguments.emplace_back("--metrics-json");
    if (options.benchmarkRepeat != 1) {
        arguments.emplace_back("--benchmark-repeat");
        arguments.push_back(std::to_string(options.benchmarkRepeat));
    }
    return arguments;
}

static int executeOptions(CliOptions options,
                          const std::atomic_bool* cancellation,
                          std::istream& input,
                          std::ostream& output,
                          std::ostream& errorOutput,
                          std::optional<ProjectConfiguration> resolvedProject = std::nullopt,
                          std::optional<fs::path> ownedDatabase = std::nullopt) {
    try {
        if (options.showHelp) {
            printHelp(output);
            return 0;
        }
        if (options.showVersion) {
            printVersion(output);
            return 0;
        }
        if (!options.programFile && !options.repl) {
            printHelp(output);
            return 1;
        }

        using Clock = std::chrono::steady_clock;
        const auto loadStarted = Clock::now();
        ProjectConfiguration project = resolvedProject
            ? std::move(*resolvedProject)
            : projectConfigurationFor(options);
        if (ownedDatabase) {
            const fs::path owner = fs::absolute(*ownedDatabase).lexically_normal();
            std::error_code equivalentError;
            const bool equivalent = fs::equivalent(
                project.databaseDirectory, owner, equivalentError);
            if ((!equivalentError && !equivalent) ||
                (equivalentError && project.databaseDirectory != owner)) {
                throw std::runtime_error(
                    "init.fx selects RocksDB directory '" +
                    project.databaseDirectory.string() +
                    "', but this service owns '" + owner.string() + "'");
            }
        }
        Interpreter interpreter;
        interpreter.setIoStreams(input, output);
        if (cancellation) {
            interpreter.setCancellationCheck(
                [cancellation] { return cancellation->load(); });
        }
        interpreter.openDatabase(project.databaseDirectory);
        interpreter.configureDatabase(project.databaseOptions);
        std::optional<DebugSession> debugSession;
        // Attached before loadProgramRoot below, not after: a program with
        // no main() executes its bare top-level calls during loading itself
        // (Interpreter::addProgram's EntryCall handling), so attaching any
        // later would miss stepping through those entirely.
        if (options.debug) {
            errorOutput << "Felidae debug session for " << options.programFile->string()
                        << " - stopped on entry, waiting on stdin.\n";
            debugSession.emplace();
            debugSession->attach(interpreter);
        }
        if (options.programFile) {
            // A source file becomes executable only after its imports and every
            // declaration have registered successfully. Running main while the
            // parser was still producing chunks made later declarations invisible
            // and could leave effects behind when a later error rejected the file.
            // Registration remains streaming; publication is the execution boundary.
            loadProgramRoot(*options.programFile, interpreter);
        }
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
            errorOutput << "FELIDAE_METRICS {"
                      << "\"loadMs\":" << (static_cast<double>(loadMicros) / 1000.0) << ","
                      << "\"executionMs\":" << (static_cast<double>(executionMicros) / 1000.0) << ","
                      << "\"queryRuns\":" << measuredQueryRuns << ","
                      << "\"firstQueryMs\":" << firstQueryMs << ","
                      << "\"repeatedQueryAverageMs\":" << repeatedQueryAverageMs << ","
                      << "\"runtime\":" << interpreter.runtimeMetricsJson()
                      << "}\n";
        };
        if (options.repl) {
            runRepl(interpreter, input, output);
            reportMetrics();
            return 0;
        }

        if (options.query) {
            auto queryGoals = parseQueryText(
                *options.query, interpreter.tokenizer(), interpreter.operatorRegistry());
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
            printSolutions(interpreter, queryGoals, solutions, output);
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
            output << interpreter.valueToDisplayString(result) << "\n";
        } else {
            output << "Program loaded successfully. No main() method found.\n"
                   << "Use a query argument, add a zero-argument entry call, or run with --repl.\n";
        }

        reportMetrics();
        return 0;
    } catch (const std::exception& e) {
        errorOutput << "error: " << e.what() << "\n";
        return 1;
    }
}

static std::vector<char*> mutableArguments(std::vector<std::string>& arguments) {
    std::vector<char*> pointers;
    pointers.reserve(arguments.size());
    for (auto& argument : arguments) pointers.push_back(argument.data());
    return pointers;
}

int main(int argc, char** argv) {
    try {
        if (argc >= 2 && std::string_view(argv[1]) == "--db-service") {
            std::optional<fs::path> database;
            for (int index = 2; index < argc; ++index) {
                if (std::string_view(argv[index]) == "--database-directory" &&
                    index + 1 < argc) {
                    if (database) {
                        throw std::runtime_error(
                            "--db-service accepts one --database-directory");
                    }
                    database = fs::path(argv[++index]);
                } else {
                    throw std::runtime_error(
                        "Unknown database service option: " +
                        std::string(argv[index]));
                }
            }
            if (!database) {
                throw std::runtime_error(
                    "--db-service requires --database-directory PATH");
            }
            std::chrono::seconds idleTimeout(60);
            if (const auto configured =
                    environmentVariable("FELIDAE_DB_IDLE_SECONDS")) {
                std::size_t consumed = 0;
                const auto seconds = std::stoull(*configured, &consumed);
                if (consumed != configured->size() || seconds > 86400) {
                    throw std::runtime_error(
                        "FELIDAE_DB_IDLE_SECONDS must be an integer from 0 to 86400");
                }
                idleTimeout = std::chrono::seconds(seconds);
            }
            const fs::path ownedDatabase =
                fs::absolute(*database).lexically_normal();
            return runDatabaseService(ownedDatabase, idleTimeout,
                [ownedDatabase](const std::vector<std::string>& forwarded,
                   const std::atomic_bool& cancellation,
                   std::ostream& standardOutput,
                   std::ostream& standardError) {
                    std::vector<std::string> arguments{"felidae"};
                    arguments.insert(arguments.end(), forwarded.begin(), forwarded.end());
                    auto pointers = mutableArguments(arguments);
                    std::istringstream noInput;
                    int exitCode = 1;
                    try {
                        exitCode = executeOptions(
                            parseCli(static_cast<int>(pointers.size()), pointers.data()),
                            &cancellation, noInput,
                            standardOutput, standardError,
                            std::nullopt, ownedDatabase);
                    } catch (const std::exception& error) {
                        standardError << "error: " << error.what() << '\n';
                    }
                    return exitCode;
                });
        }

        if (argc >= 3 && std::string_view(argv[1]) == "db" &&
            std::string_view(argv[2]) == "stop") {
            if (argc >= 4 && std::string_view(argv[3]) == "--db") {
                throw std::runtime_error("Unknown option: --db");
            }
            if (argc > 4) {
                throw std::runtime_error(
                    "Usage: felidae db stop [program.fx|project-directory]");
            }
            fs::path projectDirectory = fs::current_path();
            if (argc == 4) {
                const fs::path requested = fs::absolute(fs::path(argv[3])).lexically_normal();
                std::error_code error;
                projectDirectory = fs::is_directory(requested, error)
                    ? requested
                    : resolveProgramEntryPath(requested).parent_path();
            }
            const auto project = loadProjectConfiguration(projectDirectory);
            return stopDatabaseService(project.databaseDirectory) ? 0 : 1;
        }

        CliOptions options = parseCli(argc, argv);
        if (options.showHelp || options.showVersion) {
            return executeOptions(std::move(options), nullptr,
                                  std::cin, std::cout, std::cerr);
        }
        const ProjectConfiguration project = projectConfigurationFor(options);
        if (options.debug || options.repl) {
            return runInteractiveDatabaseOwner(project.databaseDirectory,
                [&, options = std::move(options), project]() mutable {
                return executeOptions(std::move(options), nullptr,
                                      std::cin, std::cout, std::cerr,
                                      std::move(project));
            });
        }
        const std::vector<std::string> forwarded = databaseServiceArguments(options);
        const auto response = requestDatabaseExecution(
            fs::absolute(fs::path(argv[0])).lexically_normal(),
            project.databaseDirectory, forwarded, std::cout, std::cerr);
        return response.exitCode;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
