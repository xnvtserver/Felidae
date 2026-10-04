#include "FelidaeRuntime.h"

#include "IntegerParser.h"
#include "Tokenizer.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <functional>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace Felidae {

namespace {

constexpr std::uintmax_t kStreamingReadThresholdBytes = 10ull * 1024ull * 1024ull;
constexpr std::size_t kReadChunkBytes = 1024ull * 1024ull;
} // namespace

std::string readSourceFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open file: " + path.string());

    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (!ec && size <= kStreamingReadThresholdBytes) {
        std::string text;
        text.resize(static_cast<std::size_t>(size));
        if (!text.empty()) {
            in.read(&text[0], static_cast<std::streamsize>(text.size()));
            if (!in && !in.eof()) throw std::runtime_error("Cannot read file: " + path.string());
        }
        return text;
    }

    std::string text;
    if (!ec) text.reserve(static_cast<std::size_t>(std::min<std::uintmax_t>(size, kStreamingReadThresholdBytes)));
    std::vector<char> buffer(kReadChunkBytes);
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto read = in.gcount();
        if (read > 0) text.append(buffer.data(), static_cast<std::size_t>(read));
    }
    if (!in.eof()) throw std::runtime_error("Cannot read file: " + path.string());
    return text;
}

void readSourceLines(const fs::path& path, const std::function<void(const std::string&)>& onLine) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open file: " + path.string());
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        onLine(line);
    }
    if (!in.eof()) throw std::runtime_error("Cannot read file: " + path.string());
}

std::filesystem::path resolveProgramEntryPath(const fs::path& path) {
    fs::path normalized = fs::absolute(path).lexically_normal();
    std::error_code ec;
    if (fs::is_directory(normalized, ec)) {
        fs::path mainFile = normalized / "main.fx";
        if (fs::exists(mainFile, ec) && fs::is_regular_file(mainFile, ec)) {
            return mainFile.lexically_normal();
        }
        throw std::runtime_error("Project directory does not contain main.fx: " + normalized.string());
    }
    return normalized;
}

Program parseProgramFile(const fs::path& path, bool manifest) {
    const auto normalized = resolveProgramEntryPath(path);
    return parseProgramText(readSourceFile(normalized), {}, {}, manifest);
}

Program parseProgramText(std::string text,
                         std::shared_ptr<ByteTokenizer> tokenizer,
                         std::shared_ptr<OperatorRegistry> operators,
                         bool manifest) {
    if (!tokenizer) tokenizer = std::make_shared<ByteTokenizer>();
    IntegerTokenList input(std::move(tokenizer), std::move(text));
    IntegerParser parser(input, std::move(operators));
    parser.setManifestMode(manifest);
    return parser.parseProgram();
}

InteractiveProgramLoad loadInteractiveProgramText(
    std::string text,
    const fs::path& importBase,
    Interpreter& interpreter) {
    IntegerTokenList input(interpreter.tokenizer(), std::move(text));
    IntegerParser parser(input, interpreter.operatorRegistry());
    if (parser.emptyInput()) return InteractiveProgramLoad::Empty;
    if (!parser.startsDeclaration()) {
        // REPL expressions use the production parser as their completeness
        // oracle and obey the same mandatory period as source files.
        try {
            (void)parser.parseTerminatedExpressionText();
            return InteractiveProgramLoad::Expression;
        } catch (const IntegerParserIncomplete&) {
            return InteractiveProgramLoad::Incomplete;
        }
    }

    interpreter.beginModuleTransaction();
    try {
        // IntegerParser is the sole authority for both normal source and REPL
        // declarations. Its structured incomplete-input signal requests
        // another line without introducing a second block grammar.
        Program program = parser.parseProgram();
        for (const auto& imp : program.imports) {
            for (const auto& path : imp->paths) interpreter.addImport(importBase, path);
        }
        interpreter.addProgram(program);
        interpreter.commitModuleTransaction();
        return InteractiveProgramLoad::Loaded;
    } catch (const IntegerParserIncomplete&) {
        interpreter.rollbackModuleTransaction();
        return InteractiveProgramLoad::Incomplete;
    } catch (...) {
        interpreter.rollbackModuleTransaction();
        throw;
    }
}

void parseProgramFileStatements(
    const fs::path& path,
    const std::function<void(std::shared_ptr<Statement>)>& consume,
    std::shared_ptr<OperatorRegistry> operators,
    ParserMetrics* metrics,
    std::shared_ptr<ByteTokenizer> tokenizer) {
    const fs::path normalized = resolveProgramEntryPath(path);
    if (!tokenizer) tokenizer = std::make_shared<ByteTokenizer>();
    IntegerTokenList input(std::move(tokenizer), readSourceFile(normalized));
    IntegerParser parser(input, std::move(operators));
    // Publish each completed statement before parsing the next one. Import
    // consumers can thereby register public mixfix syntax in the shared
    // operator registry before the following statement is assembled.
    while (!parser.programComplete())
        consume(parser.parseNextProgramStatement());
    if (metrics) {
        metrics->tokensLexed += input.entries().size();
        metrics->iterations += parser.metrics().iterations;
        metrics->peakRecursionDepth = std::max(metrics->peakRecursionDepth,
                                                parser.metrics().peakRecursionDepth);
        metrics->backtrackingAttempts += parser.metrics().backtrackingAttempts;
    }
}

void parseProgramFileChunks(const fs::path& path,
                            const std::function<void(Program&&)>& consume,
                            std::size_t statementsPerChunk,
                            std::shared_ptr<OperatorRegistry> operators,
                            std::shared_ptr<ByteTokenizer> tokenizer) {
    if (statementsPerChunk == 0) statementsPerChunk = 1;
    Program chunk;
    parseProgramFileStatements(path, [&](std::shared_ptr<Statement> statement) {
        chunk.addStatement(std::move(statement));
        if (chunk.statements.size() >= statementsPerChunk) {
            consume(std::move(chunk));
            chunk = Program{};
        }
    }, std::move(operators), nullptr, std::move(tokenizer));
    if (!chunk.statements.empty()) consume(std::move(chunk));
}

void loadProgramRoot(const fs::path& file, Interpreter& interpreter) {
    loadProgramRoot(file, interpreter, {});
}

void loadProgramRoot(const fs::path& file,
                     Interpreter& interpreter,
                     const std::function<void(const Program&)>& afterChunk) {
    fs::path normalized = resolveProgramEntryPath(file);
    ParserMetrics parserMetrics;
    if (!afterChunk) {
        interpreter.loadProgramFile(normalized, &parserMetrics);
        interpreter.recordParserMetrics(parserMetrics);
        return;
    }
    fs::path baseDir = normalized.parent_path();
    const auto streamStarted = std::chrono::steady_clock::now();
    interpreter.beginModuleTransaction();
    try {
        parseProgramFileChunks(normalized, [&](Program&& program) {
            for (const auto& imp : program.imports) {
                for (const auto& path : imp->paths) interpreter.addImport(baseDir, path);
            }
            interpreter.addProgram(program);
            afterChunk(program);
        }, 1, interpreter.operatorRegistry(), interpreter.tokenizer());
        interpreter.commitModuleTransaction();
        interpreter.recordStreamedModuleMicros(static_cast<std::size_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - streamStarted).count()));
        interpreter.recordParserMetrics(parserMetrics);
    } catch (...) {
        interpreter.rollbackModuleTransaction();
        throw;
    }
}

void loadProgramRoot(const fs::path& file,
                     const Program& program,
                     Interpreter& interpreter) {
    fs::path normalized = fs::absolute(file).lexically_normal();
    fs::path baseDir = normalized.parent_path();

    interpreter.beginModuleTransaction();
    try {
        for (const auto& imp : program.imports) {
            for (const auto& path : imp->paths) {
                interpreter.addImport(baseDir, path);
            }
        }
        interpreter.addProgram(program);
        interpreter.commitModuleTransaction();
    } catch (...) {
        interpreter.rollbackModuleTransaction();
        throw;
    }
}

std::vector<std::string> listCoreLibraries(const fs::path& startDir) {
    std::vector<std::string> names;
    std::error_code ec;
    fs::path current = fs::absolute(startDir, ec).lexically_normal();
    if (ec) return names;

    fs::path coreDir;
    while (true) {
        fs::path candidate = current / "core";
        if (fs::exists(candidate, ec) && fs::is_directory(candidate, ec)) {
            coreDir = candidate;
            break;
        }
        fs::path parent = current.parent_path();
        if (parent == current) break;
        current = parent;
    }
    if (coreDir.empty()) return names;

    for (const auto& entry : fs::directory_iterator(coreDir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".fx") continue;
        names.push_back(entry.path().stem().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::shared_ptr<Expr> makeSystemInput(const std::vector<std::string>& args) {
    std::vector<std::shared_ptr<Expr>> argValues;
    argValues.reserve(args.size());
    for (const auto& arg : args) argValues.push_back(std::make_shared<StringExpr>(arg));
    return std::make_shared<MapExpr>(std::vector<MapEntry>{
        MapEntry{"args", std::make_shared<ArrayExpr>(std::move(argValues))},
        MapEntry{"text", std::make_shared<StringExpr>("")}
    });
}

std::string trim(const std::string& text) {
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) start++;
    size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) end--;
    return text.substr(start, end - start);
}

bool isBareIdentifier(const std::string& text) {
    if (text.empty() || !(std::isalpha(static_cast<unsigned char>(text[0])) || text[0] == '_')) return false;
    for (char ch : text) {
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_')) return false;
    }
    return true;
}

} // namespace Felidae
