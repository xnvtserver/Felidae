#include "FelidaeRuntime.h"

#include "Lexer.h"
#include "Parser.h"
#include "Symbol.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace Felidae {

namespace {

constexpr std::uintmax_t kStreamingReadThresholdBytes = 10ull * 1024ull * 1024ull;
constexpr std::size_t kReadChunkBytes = 1024ull * 1024ull;
constexpr std::size_t kMaxProgramCacheEntries = 256;

struct CachedProgram {
    std::uintmax_t size = 0;
    fs::file_time_type modified;
    std::uint64_t lastUsed = 0;
    Program program;
};

std::mutex& programCacheMutex() {
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<std::string, CachedProgram>& programCache() {
    static auto* cache = new std::unordered_map<std::string, CachedProgram>();
    return *cache;
}

bool& programCacheEnabled() {
    static bool enabled = false;
    return enabled;
}

std::uint64_t& programCacheClock() {
    static std::uint64_t clock = 0;
    return clock;
}

std::string normalizedCacheKey(const fs::path& path) {
    return fs::absolute(path).lexically_normal().string();
}

void evictProgramCacheIfNeeded() {
    auto& cache = programCache();
    if (cache.size() <= kMaxProgramCacheEntries) return;
    auto oldest = cache.end();
    for (auto it = cache.begin(); it != cache.end(); ++it) {
        if (oldest == cache.end() || it->second.lastUsed < oldest->second.lastUsed) {
            oldest = it;
        }
    }
    if (oldest != cache.end()) cache.erase(oldest);
}

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

void setProgramAstCacheEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(programCacheMutex());
    programCacheEnabled() = enabled;
    if (!enabled) {
        programCache().clear();
        programCacheClock() = 0;
    }
}

void clearProgramAstCache() {
    std::lock_guard<std::mutex> lock(programCacheMutex());
    programCache().clear();
    programCacheClock() = 0;
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
    const auto key = normalizedCacheKey(normalized);
    const auto size = fs::file_size(normalized, ec);
    if (ec) throw std::runtime_error("Cannot inspect file: " + normalized.string() + ": " + ec.message());
    const auto modified = fs::last_write_time(normalized, ec);
    if (ec) throw std::runtime_error("Cannot inspect file: " + normalized.string() + ": " + ec.message());

    {
        std::lock_guard<std::mutex> lock(programCacheMutex());
        auto& cache = programCache();
        if (programCacheEnabled()) {
            auto found = cache.find(key);
            if (found != cache.end() &&
                found->second.size == size &&
                found->second.modified == modified) {
                found->second.lastUsed = ++programCacheClock();
                return found->second.program;
            }
        }
    }

    Program program = parseProgramText(readSourceFile(normalized));
    {
        std::lock_guard<std::mutex> lock(programCacheMutex());
        if (programCacheEnabled()) {
            programCache()[key] = CachedProgram{size, modified, ++programCacheClock(), program};
            evictProgramCacheIfNeeded();
        }
    }
    return program;
}

Program parseProgramText(const std::string& text) {
    Lexer lexer(text);
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    return parser.parseProgram();
}

void loadProgramRoot(const fs::path& file, Interpreter& interpreter) {
    fs::path normalized = resolveProgramEntryPath(file);
    Program program = parseProgramFile(normalized);
    loadProgramRoot(normalized, program, interpreter);
}

void loadProgramRoot(const fs::path& file,
                     const Program& program,
                     Interpreter& interpreter) {
    fs::path normalized = fs::absolute(file).lexically_normal();
    fs::path baseDir = normalized.parent_path();

    for (const auto& imp : program.imports) {
        for (const auto& path : imp->paths) {
            interpreter.addLazyImport(baseDir, path);
        }
    }
    interpreter.addProgram(program);
}

std::vector<std::shared_ptr<Goal>> parseQueryText(const std::string& query) {
    Lexer lexer(query);
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    return parser.parseQuery();
}

static void collectVarsExpr(const std::shared_ptr<Expr>& expr, std::vector<std::string>& vars) {
    if (auto v = std::dynamic_pointer_cast<VarExpr>(expr)) {
        if (v->nameId != InternalSymbol::SystemResultId &&
            !isInternalGeneratedSymbolName(v->name)) {
            for (const auto& existing : vars) {
                if (existing == v->name) return;
            }
            vars.push_back(v->name);
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
    } else if (auto binary = std::dynamic_pointer_cast<BinaryExpr>(expr)) {
        collectVarsExpr(binary->left, vars);
        collectVarsExpr(binary->right, vars);
    } else if (auto pipeline = std::dynamic_pointer_cast<PipelineExpr>(expr)) {
        collectVarsExpr(pipeline->left, vars);
        collectVarsExpr(pipeline->right, vars);
    }
}

static void collectVarsGoal(const std::shared_ptr<Goal>& goal, std::vector<std::string>& vars) {
    if (auto cg = std::dynamic_pointer_cast<CallGoal>(goal)) {
        for (const auto& arg : cg->call.args) collectVarsExpr(arg.value, vars);
    } else if (auto ag = std::dynamic_pointer_cast<AssignGoal>(goal)) {
        bool seen = false;
        for (const auto& existing : vars) {
            if (existing == ag->name) {
                seen = true;
                break;
            }
        }
        if (!seen) vars.push_back(ag->name);
        if (ag->goal) collectVarsGoal(ag->goal, vars);
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

static std::vector<std::string> collectQueryVars(const std::vector<std::shared_ptr<Goal>>& goals) {
    std::vector<std::string> vars;
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
            auto varExpr = std::make_shared<VarExpr>(queryVars[j]);
            out << queryVars[j] << " = " << interpreter.exprToString(varExpr, solutions[i].env);
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
