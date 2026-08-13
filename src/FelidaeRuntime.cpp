#include "FelidaeRuntime.h"

#include "IntegerParser.h"
#include "SentencePieceModel.h"
#include "Symbol.h"

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

Program parseProgramFile(const fs::path& path) {
    std::error_code ec;
    const auto normalized = resolveProgramEntryPath(path);
    return parseProgramText(readSourceFile(normalized));
}

Program parseProgramText(std::string text) {
    IntegerTokenList input(felidaeSentencePieceModel(), std::move(text));
    return IntegerParser(input).parseProgram();
}

LegacyIrModule compileProgramTextToIr(std::string text) {
    return LegacyAstIrAdapter{}.compile(parseProgramText(std::move(text)));
}

LegacyIrModule compileProgramFileToIr(const fs::path& path) {
    const auto entry = resolveProgramEntryPath(path);
    return LegacyAstIrAdapter{}.compile(parseProgramText(readSourceFile(entry)),
                                        entry.parent_path());
}

std::optional<FelidaeIr> tryCompileExpressionTextToIr(const std::string& text) {
    try {
        IntegerTokenList input(felidaeSentencePieceModel(), text);
        return IntegerParser(input).compileExpressionIr();
    } catch (const IntegerParserError&) {
        return std::nullopt;
    }
}

void parseProgramFileStatements(
    const fs::path& path,
    const std::function<void(std::shared_ptr<Statement>)>& consume,
    std::shared_ptr<OperatorRegistry> operators,
    ParserMetrics* metrics) {
    const fs::path normalized = resolveProgramEntryPath(path);
    IntegerTokenList input(felidaeSentencePieceModel(), readSourceFile(normalized));
    IntegerParser parser(input, std::move(operators));
    Program program = parser.parseProgram();
    for (auto& statement : program.statements) consume(std::move(statement));
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
                            std::size_t statementsPerChunk) {
    if (statementsPerChunk == 0) statementsPerChunk = 1;
    Program chunk;
    parseProgramFileStatements(path, [&](std::shared_ptr<Statement> statement) {
        chunk.addStatement(std::move(statement));
        if (chunk.statements.size() >= statementsPerChunk) {
            consume(std::move(chunk));
            chunk = Program{};
        }
    });
    if (!chunk.statements.empty()) consume(std::move(chunk));
}

void loadProgramRoot(const fs::path& file, Interpreter& interpreter) {
    loadProgramRoot(file, interpreter, {});
}

void loadProgramRoot(const fs::path& file,
                     Interpreter& interpreter,
                     const std::function<void(const Program&)>& afterChunk) {
    fs::path normalized = resolveProgramEntryPath(file);
    fs::path baseDir = normalized.parent_path();
    const auto streamStarted = std::chrono::steady_clock::now();
    ParserMetrics parserMetrics;
    interpreter.beginModuleTransaction();
    try {
        if (afterChunk) {
            parseProgramFileChunks(normalized, [&](Program&& program) {
                for (const auto& imp : program.imports) {
                    for (const auto& path : imp->paths) interpreter.addImport(baseDir, path);
                }
                interpreter.addProgram(program);
                afterChunk(program);
            });
        } else {
            parseProgramFileStatements(normalized, [&](std::shared_ptr<Statement> statement) {
                if (statement->kind() == StatementKind::Import) {
                    const auto import = std::static_pointer_cast<ImportStmt>(statement);
                    for (const auto& path : import->paths) interpreter.addImport(baseDir, path);
                    return;
                }
                interpreter.addStreamedStatement(std::move(statement));
            }, interpreter.operatorRegistry(), &parserMetrics);
        }
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

std::vector<std::shared_ptr<Goal>> parseQueryText(const std::string& query) {
    IntegerTokenList input(felidaeSentencePieceModel(), query);
    return IntegerParser(input).parseQuery();
}

static void collectVarsExpr(const std::shared_ptr<Expr>& expr, std::vector<SymbolId>& vars) {
    if (auto v = std::dynamic_pointer_cast<VarExpr>(expr)) {
        if (v->nameId != InternalSymbol::SystemResultId &&
            !isInternalGeneratedSymbolId(v->nameId) &&
            std::find(vars.begin(), vars.end(), v->nameId) == vars.end()) {
            vars.push_back(v->nameId);
        }
    } else if (auto term = std::dynamic_pointer_cast<TermExpr>(expr)) {
        for (const auto& arg : term->args) collectVarsExpr(arg.value, vars);
    } else if (auto lambda = std::dynamic_pointer_cast<LambdaExpr>(expr)) {
        collectVarsExpr(lambda->source, vars);
        collectVarsExpr(lambda->body, vars);
        if (lambda->right) collectVarsExpr(lambda->right, vars);
    } else if (auto array = std::dynamic_pointer_cast<ArrayExpr>(expr)) {
        for (const auto& item : array->items) collectVarsExpr(item, vars);
    } else if (auto map = std::dynamic_pointer_cast<MapExpr>(expr)) {
        for (const auto& entry : map->entries) collectVarsExpr(entry.value, vars);
    } else if (auto access = std::dynamic_pointer_cast<AccessExpr>(expr)) {
        auto targetVar = std::dynamic_pointer_cast<VarExpr>(access->target);
        if (access->keyId == InternalSymbol::ResultId && targetVar && targetVar->nameId == InternalSymbol::SystemId) return;
        collectVarsExpr(access->target, vars);
    } else if (auto op = std::dynamic_pointer_cast<OperatorExpression>(expr)) {
        for (size_t i = 0; i < op->captureCount(); ++i) collectVarsExpr(op->capture(i), vars);
    }
}

static void collectVarsGoal(const std::shared_ptr<Goal>& goal, std::vector<SymbolId>& vars) {
    if (auto cg = std::dynamic_pointer_cast<CallGoal>(goal)) {
        for (const auto& arg : cg->call.args) collectVarsExpr(arg.value, vars);
    } else if (auto ag = std::dynamic_pointer_cast<AssignGoal>(goal)) {
        if (std::find(vars.begin(), vars.end(), ag->nameId) == vars.end()) {
            vars.push_back(ag->nameId);
        }
        if (ag->expr) collectVarsExpr(ag->expr, vars);
    } else if (auto bg = std::dynamic_pointer_cast<BinaryGoal>(goal)) {
        collectVarsExpr(bg->left, vars);
        collectVarsExpr(bg->right, vars);
    } else if (auto wg = std::dynamic_pointer_cast<WhereGoal>(goal)) {
        collectVarsGoal(wg->condition, vars);
    } else if (auto ifGoal = std::dynamic_pointer_cast<IfGoal>(goal)) {
        collectVarsGoal(ifGoal->condition, vars);
        for (const auto& branchGoal : ifGoal->thenBranch) collectVarsGoal(branchGoal, vars);
        for (const auto& branchGoal : ifGoal->elseBranch) collectVarsGoal(branchGoal, vars);
    } else if (auto rg = std::dynamic_pointer_cast<ReturnGoal>(goal)) {
        for (const auto& field : rg->fields) collectVarsExpr(field.value, vars);
    } else if (auto gg = std::dynamic_pointer_cast<GroupGoal>(goal)) {
        for (const auto& groupedGoal : gg->goals) collectVarsGoal(groupedGoal, vars);
    } else if (auto og = std::dynamic_pointer_cast<OrGoal>(goal)) {
        for (const auto& branch : og->branches) {
            for (const auto& branchGoal : branch) collectVarsGoal(branchGoal, vars);
        }
    }
}

static std::vector<SymbolId> collectQueryVars(const std::vector<std::shared_ptr<Goal>>& goals) {
    std::vector<SymbolId> vars;
    for (const auto& g : goals) collectVarsGoal(g, vars);
    return vars;
}

void printSolutions(Interpreter& interpreter,
                    const std::vector<std::shared_ptr<Goal>>& queryGoals,
                    const std::vector<Solution>& solutions,
                    std::ostream& out) {
    auto queryVars = collectQueryVars(queryGoals);
    if (solutions.empty()) {
        out << "false\n";
        return;
    }
    if (queryVars.empty()) {
        out << "true\n";
        return;
    }
    for (size_t i = 0; i < solutions.size(); ++i) {
        out << "Solution " << (i + 1) << ": ";
        for (size_t j = 0; j < queryVars.size(); ++j) {
            if (j) out << ", ";
            const auto name = symbolNameForId(queryVars[j]);
            auto varExpr = std::make_shared<VarExpr>(name, queryVars[j]);
            out << name << " = " << interpreter.exprToString(varExpr, solutions[i].env);
        }
        out << "\n";
    }
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
