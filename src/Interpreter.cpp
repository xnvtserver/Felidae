Warning: truncated output (original token count: 109819)
Total output lines: 9128

#include "Interpreter.h"
#include "BuiltinRegistry.h"
#include "IntegerParser.h"
#include "SentencePieceModel.h"
#include "FelidaeRuntime.h"
#include "OperatorAnnotation.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <cstring>
#include <random>
#include <set>
#include <sstream>

#ifdef FELIDAE_HAS_EIGEN
#include <Eigen/Dense>
#endif

namespace Felidae {
namespace fs = std::filesystem;

namespace {

constexpr size_t kMaxCachedEnvFrames = 4096;
constexpr size_t kHotMethodPrepareThreshold = 2;
// A non-tail call expands through solver, value evaluation, and unification
// frames. Keep this deliberately below the platform stack danger zone until
// Tail calls unwind through TailCallSignal. Non-tail calls still use the
// native stack until the method-aware frame engine lands, so this deliberately
// conservative bound turns excessive recursion into a Felidae error first.
// Relational recursion is scheduled on solveIterative's work stack.  Methods
// still have a small native continuation boundary, so keep a conservative
// hard ceiling rather than allowing user recursion to consume the process
// stack. This is intentionally independent from the iterative goal limit.
constexpr size_t kMaxNativeMethodCallDepth = 8;
constexpr size_t kMaxNativeGoalFrameDepth = 256;

class CounterScope {
public:
    explicit CounterScope(size_t& counter) : counter_(counter) { ++counter_; }
    ~CounterScope() { --counter_; }
private:
    size_t& counter_;
};

class PipelineResultClearScope {
public:
    explicit PipelineResultClearScope(std::vector<std::shared_ptr<Expr>>& results)
        : results_(results), saved_(std::move(results)) {
        results_.clear();
    }

    ~PipelineResultClearScope() {
        results_ = std::move(saved_);
    }

private:
    std::vector<std::shared_ptr<Expr>>& results_;
    std::vector<std::shared_ptr<Expr>> saved_;
};

class TailCallSignal {
public:
    TailCallSignal(TermExpr next, Env environment)
        : term(std::move(next)), env(std::move(environment)) {}
    TermExpr term;
    Env env;
};

class PipelineResultValueScope {
public:
    PipelineResultValueScope(std::vector<std::shared_ptr<Expr>>& results,
                             const std::shared_ptr<Expr>& value)
        : results_(results) {
        results_.push_back(value->clone());
    }

    ~PipelineResultValueScope() {
        results_.pop_back();
    }

private:
    std::vector<std::shared_ptr<Expr>>& results_;
};

}

static std::string jsonEscape(const std::string& value) {
    std::ostringstream out;
    for (char c : value) {
        switch (c) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out << "\\u";
                    out << std::hex << std::uppercase << std::setw(4) << std::setfill('0')
                        << static_cast<int>(static_cast<unsigned char>(c));
                    out << std::dec << std::nouppercase;
                } else {
                    out << c;
                }
        }
    }
    return out.str();
}

static bool argAsNumber(const std::shared_ptr<Expr>& expr, double& out) {
    if (auto n = std::dynamic_pointer_cast<NumberExpr>(expr)) {
        out = n->value;
        return true;
    }
    return false;
}

static bool argAsString(const std::shared_ptr<Expr>& expr, std::string& out) {
    if (auto s = std::dynamic_pointer_cast<StringExpr>(expr)) {
        out = s->value;
        return true;
    }
    return false;
}

static std::vector<std::shared_ptr<Expr>> termArgs(const std::shared_ptr<Expr>& expr,
                                                   BuiltinId id) {
    if (auto t = std::dynamic_pointer_cast<TermExpr>(expr)) {
        if (t->builtinId == id) {
            std::vector<std::shared_ptr<Expr>> out;
            for (const auto& arg : t->args) out.push_back(arg.value);
            return out;
        }
    }
    if (id == BuiltinId::FnArray) {
        if (auto a = std::dynamic_pointer_cast<ArrayExpr>(expr)) return a->items;
    }
    return {};
}

static std::shared_ptr<Expr> findMapValue(const std::shared_ptr<Expr>& expr,
                                          const std::string& key) {
    if (const auto ast = std::dynamic_pointer_cast<AstValueExpr>(expr)) {
        if (key == "text") {
            return std::make_shared<StringExpr>(ast->sourceText());
        }
        if (key == "nodeKind") {
            return std::make_shared<StringExpr>(ast->nodeKind);
        }
        return {};
    }
    if (auto selection = std::dynamic_pointer_cast<FactSelectionExpr>(expr)) {
        if (key == internalSymbolName(InternalSymbolKind::Type)) {
            return std::make_shared<StringExpr>("FactSelection");
        }
        if (key == "fact_type") {
            return std::make_shared<StringExpr>(selection->factType);
        }
        if (key == "source") return std::make_shared<StringExpr>("memory");
        if (key == "snapshot_generation") {
            return std::make_shared<NumberExpr>(
                static_cast<double>(selection->snapshotGeneration));
        }
        if (key == "field" && !selection->field.empty()) {
            return std::make_shared<StringExpr>(selection->field);
        }
        if (key == "equals") return selection->equals;
        if (key == "designation" && selection->designations.size() == 1) {
            return std::make_shared<StringExpr>(selection->designations.front());
        }
        return {};
    }
    const SymbolId keyId = symbolIdForName(key);
    if (auto m = std::dynamic_pointer_cast<MapExpr>(expr)) {
        for (const auto& entry : m->entries) {
            if (entry.keyId == keyId) return entry.value;
        }
    }
    if (auto t = std::dynamic_pointer_cast<TermExpr>(expr)) {
        if (t->builtinId == BuiltinId::JsonObject) {
            for (const auto& field : t->args) {
                auto pair = termArgs(field.value, BuiltinId::FnPair);
                std::string fieldName;
                if (pair.size() == 2 && argAsString(pair[0], fieldName) &&
                    symbolIdForName(fieldName) == keyId) {
                    return pair[1];
                }
            }
        }
    }
    return {};
}

static std::string publicValueString(const std::shared_ptr<Expr>& value) {
    if (!value) return "nil";
    if (const auto array = std::dynamic_pointer_cast<ArrayExpr>(value)) {
        std::ostringstream out;
        out << "[";
        for (size_t index = 0; index < array->items.size(); ++index) {
            if (index) out << ", ";
            out << publicValueString(array->items[index]);
        }
        out << "]";
        return out.str();
    }
    if (const auto map = std::dynamic_pointer_cast<MapExpr>(value)) {
        const bool fact = !map->factType.empty();
        std::ostringstream out;
        out << (fact ? map->factType + "(" : "{");
        bool first = true;
        for (const auto& entry : map->entries) {
            if (fact && (entry.keyId == InternalSymbol::TypeId ||
                         entry.keyId == InternalSymbol::ParentId)) {
                continue;
            }
            if (!first) out << ", ";
            first = false;
            out << entry.key << ": " << publicValueString(entry.value);
        }
        out << (fact ? ")" : "}");
        return out.str();
    }
    if (const auto term = std::dynamic_pointer_cast<TermExpr>(value)) {
        std::ostringstream out;
        out << term->name << "(";
        for (size_t index = 0; index < term->args.size(); ++index) {
            if (index) out << ", ";
            if (!term->args[index].name.empty()) out << term->args[index].name << ": ";
            out << publicValueString(term->args[index].value);
        }
        out << ")";
        return out.str();
    }
    return value->debug();
}

static bool isStructuredPublicValue(const std::shared_ptr<Expr>& value) {
    return std::dynamic_pointer_cast<ArrayExpr>(value) ||
           std::dynamic_pointer_cast<MapExpr>(value);
}

static std::string publicDisplayString(const std::shared_ptr<Expr>& value, size_t indent = 0) {
    if (!value) return "nil";
    const std::string compact = publicValueString(value);
    const bool structured = isStructuredPublicValue(value);
    if (!structured || compact.size() + indent <= 96) return compact;

    const std::string padding(indent, ' ');
    const std::string childPadding(indent + 2, ' ');
    if (const auto array = std::dynamic_pointer_cast<ArrayExpr>(value)) {
        std::ostringstream out;
        out << "[\n";
        for (size_t index = 0; index < array->items.size(); ++index) {
            out << childPadding << publicDisplayString(array->items[index], indent + 2);
            if (index + 1 < array->items.size()) out << ",";
            out << "\n";
        }
        out << padding << "]";
        return out.str();
    }

    const auto map = std::dynamic_pointer_cast<MapExpr>(value);
    const bool fact = !map->factType.empty();
    std::vector<const MapEntry*> entries;
    entries.reserve(map->entries.size());
    for (const auto& entry : map->entries) {
        if (fact && (entry.keyId == InternalSymbol::TypeId ||
                     entry.keyId == InternalSymbol::ParentId)) {
            continue;
        }
        entries.push_back(&entry);
    }
    std::ostringstream out;
    out << (fact ? map->factType + "(\n" : "{\n");
    for (size_t index = 0; index < entries.size(); ++index) {
        out << childPadding << entries[index]->key << ": "
            << publicDisplayString(entries[index]->value, indent + 2);
        if (index + 1 < entries.size()) out << ",";
        out << "\n";
    }
    out << padding << (fact ? ")" : "}");
    return out.str();
}
static std::vector<MapEntry> cloneEntries(const std::vector<MapEntry>& entries) {
    std::vector<MapEntry> copied;
    copied.reserve(entries.size());
    for (const auto& entry : entries) copied.push_back(MapEntry{entry.key, entry.value->clone()});
    return copied;
}

static std::shared_ptr<Expr> cloneExprOrNil(const std::shared_ptr<Expr>& value) {
    if (value) return value->clone();
    return std::make_shared<NilExpr>();
}

static bool exprAsMapEntries(const std::shared_ptr<Expr>& expr, std::vector<MapEntry>& out) {
    if (auto map = std::dynamic_pointer_cast<MapExpr>(expr)) {
        out = cloneEntries(map->entries);
        return true;
    }
    if (auto term = std::dynamic_pointer_cast<TermExpr>(expr)) {
        if (term->builtinId == BuiltinId::JsonObject) {
            out.clear();
            for (const auto& field : term->args) {
                auto pair = termArgs(field.value, BuiltinId::FnPair);
                std::string key;
                if (pair.size() != 2 || !argAsString(pair[0], key)) return false;
                out.push_back(MapEntry{key, pair[1]->clone()});
            }
            return true;
        }
    }
    return false;
}

static bool exprAsArrayItems(const std::shared_ptr<Expr>& expr, std::vector<std::shared_ptr<Expr>>& out) {
    if (auto array = std::dynamic_pointer_cast<ArrayExpr>(expr)) {
        out.clear();
        out.reserve(array->items.size());
        for (const auto& item : array->items) out.push_back(cloneExprOrNil(item));
        return true;
    }
    if (auto term = std::dynamic_pointer_cast<TermExpr>(expr)) {
        if (term->builtinId == BuiltinId::FnArray) {
            out.clear();
            for (const auto& arg : term->args) {
                if (arg.nameId == symbolIdForName("data")) {
                    auto data = std::dynamic_pointer_cast<ArrayExpr>(arg.value);
                    if (!data) return false;
                    for (const auto& item : data->items) out.push_back(cloneExprOrNil(item));
                    return true;
                }
                out.push_back(cloneExprOrNil(arg.value));
            }
            return true;
        }
    }
    return false;
}

static double requireNumber(const std::shared_ptr<Expr>& expr, const std::string& fn, const std::string& arg);

static std::vector<double> requireNumberArray(const std::shared_ptr<Expr>& expr, const std::string& fn, const std::string& arg) {
    std::vector<std::shared_ptr<Expr>> items;
    if (!exprAsArrayItems(expr, items)) throw InterpreterError(fn + " expects numeric array argument '" + arg + "'");
    std::vector<double> numbers;
    numbers.reserve(items.size());
    for (const auto& item : items) numbers.push_back(requireNumber(item, fn, arg));
    return numbers;
}

static std::shared_ptr<ArrayExpr> numbersToArray(const std::vector<double>& values) {
    std::vector<std::shared_ptr<Expr>> items;
    items.reserve(values.size());
    for (double value : values) items.push_back(std::make_shared<NumberExpr>(value));
    return std::make_shared<ArrayExpr>(std::move(items));
}

static double factorialTerm(double n, const std::string& fn, const std::string& arg) {
    if (n < 0 || std::floor(n) != n) throw InterpreterError(fn + " expects non-negative integer argument '" + arg + "'");
    return n;
}

static void upsertEntry(std::vector<MapEntry>& entries, const std::string& key, std::shared_ptr<Expr> value) {
    const SymbolId keyId = symbolIdForName(key);
    for (auto& entry : entries) {
        if (entry.keyId == keyId) {
            entry.value = std::move(value);
            return;
        }
    }
    entries.push_back(MapEntry{key, std::move(value)});
}

static bool removeEntry(std::vector<MapEntry>& entries, const std::string& key) {
    auto oldSize = entries.size();
    const SymbolId keyId = symbolIdForName(key);
    entries.erase(std::remove_if(entries.begin(), entries.end(),
        [&](const MapEntry& entry) { return entry.keyId == keyId; }), entries.end());
    return entries.size() != oldSize;
}

static std::string exprTextValue(const std::shared_ptr<Expr>& expr) {
    if (auto str = std::dynamic_pointer_cast<StringExpr>(expr)) return str->value;
    return expr ? expr->debug() : "nil";
}

static bool exprEqualsLiteral(const std::shared_ptr<Expr>& a, const std::shared_ptr<Expr>& b) {
    if (auto sa = std::dynamic_pointer_cast<StringExpr>(a)) {
        auto sb = std::dynamic_pointer_cast<StringExpr>(b);
        return sb && sa->value == sb->value;
    }
    if (auto ba = std::dynamic_pointer_cast<BoolExpr>(a)) {
        auto bb = std::dynamic_pointer_cast<BoolExpr>(b);
        return bb && ba->value == bb->value;
    }
    if (auto na = std::dynamic_pointer_cast<NumberExpr>(a)) {
        auto nb = std::dynamic_pointer_cast<NumberExpr>(b);
        return nb && std::fabs(na->value - nb->value) < 1e-12;
    }
    if (std::dynamic_pointer_cast<NilExpr>(a) || std::dynamic_pointer_cast<NilExpr>(b)) {
        return static_cast<bool>(std::dynamic_pointer_cast<NilExpr>(a)) &&
               static_cast<bool>(std::dynamic_pointer_cast<NilExpr>(b));
    }
    return a->debug() == b->debug();
}

static bool exprContainsLiteral(const std::shared_ptr<Expr>& haystack, const std::shared_ptr<Expr>& needle) {
    if (exprEqualsLiteral(haystack, needle)) return true;
    std::vector<std::shared_ptr<Expr>> items;
    if (!exprAsArrayItems(haystack, items)) return false;
    for (const auto& item : items) {
        if (exprEqualsLiteral(item, needle)) return true;
    }
    return false;
}

static bool isMethodTruthTupleWithFalse(const std::shared_ptr<Expr>& expr) {
    auto tuple = std::dynamic_pointer_cast<TermExpr>(expr);
    if (!tuple || tuple->builtinId != BuiltinId::FnTuple || tuple->args.empty()) return false;
    for (const auto& arg : tuple->args) {
        const auto value = std::dynamic_pointer_cast<BoolExpr>(arg.value);
        if (!value) return false;
        if (!value->value) return true;
    }
    return false;
}

static std::string lowerText(std::string text) {
    for (char& ch : text) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return text;
}

static std::string upperText(std::string text) {
    for (char& ch : text) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return text;
}

static const Arg* findTermArgByNameOrIndex(const TermExpr& term, const std::string& name, size_t index) {
    const SymbolId nameId = name.empty() ? 0 : symbolIdForName(name);
    for (const auto& arg : term.args) {
        if (arg.nameId == nameId) return &arg;
    }
    if (index < term.args.size()) return &term.args[index];
    return nullptr;
}

static bool exprAsArray(const std::shared_ptr<Expr>& expr, std::vector<std::shared_ptr<Expr>>& out) {
    if (auto array = std::dynamic_pointer_cast<ArrayExpr>(expr)) {
        out = array->items;
        return true;
    }
    if (auto term = std::dynamic_pointer_cast<TermExpr>(expr)) {
        if (term->builtinId == BuiltinId::FnArray) {
            out.clear();
            for (const auto& arg : term->args) {
                if (arg.nameId == symbolIdForName("data")) {
                    auto data = std::dynamic_pointer_cast<ArrayExpr>(arg.value);
                    if (!data) return false;
                    out = data->items;
                    return true;
                }
            }
        }
    }
    return false;
}

static std::string astNodeKind(const std::shared_ptr<AstNode>& node) {
    if (std::dynamic_pointer_cast<TermExpr>(node)) return "func_call";
    if (auto op = std::dynamic_pointer_cast<OperatorExpression>(node)) {
        if (isComparisonOperator(op->coreOperator) ||
            op->coreOperator == CoreOperator::LogicalAnd ||
            op->coreOperator == CoreOperator::LogicalOr ||
            op->coreOperator == CoreOperator::LogicalNot) return "logical";
        if (op->coreOperator == CoreOperator::Add ||
            op->coreOperator == CoreOperator::Subtract ||
            op->coreOperator == CoreOperator::Multiply ||
            op->coreOperator == CoreOperator::Divide ||
            op->coreOperator == CoreOperator::Modulo ||
            op->coreOperator == CoreOperator::UnaryPlus ||
            op->coreOperator == CoreOperator::UnaryMinus) return "arithmetic";
        return "expr";
    }
    if (std::dynamic_pointer_cast<LambdaExpr>(node)) return "lambda";
    if (std::dynamic_pointer_cast<AccessExpr>(node)) return "member_access";
    if (std::dynamic_pointer_cast<MapExpr>(node)) return "map";
    if (std::dynamic_pointer_cast<ArrayExpr>(node)) return "array";
    if (std::dynamic_pointer_cast<StringExpr>(node)) return "string_literal";
    if (std::dynamic_pointer_cast<NumberExpr>(node)) return "number_literal";
    if (std::dynamic_pointer_cast<BoolExpr>(node)) return "bool_literal";
    if (std::dynamic_pointer_cast<NilExpr>(node)) return "nil_literal";
    if (std::dynamic_pointer_cast<VarExpr>(node)) return "variable";
    if (auto clause = std::dynamic_pointer_cast<ClauseStmt>(node)) {
        if (clause->clauseKind == ClauseKind::Fact) return "fact";
        if (clause->clauseKind == ClauseKind::NativeDeclaration) return "native";
        if (clause->clauseKind == ClauseKind::EntryCall) return "entry";
        return "func";
    }
    if (std::dynamic_pointer_cast<ImportStmt>(node)) return "stmt";
    if (std::dynamic_pointer_cast<GlobalBindingStmt>(node)) return "stmt";
    if (std::dynamic_pointer_cast<IfGoal>(node)) return "stmt";
    if (std::dynamic_pointer_cast<AssignGoal>(node) ||
        std::dynamic_pointer_cast<MultiAssignGoal>(node)) return "stmt";
    if (std::dynamic_pointer_cast<ReturnGoal>(node)) return "stmt";
    if (std::dynamic_pointer_cast<WhereGoal>(node)) return "stmt";
    if (std::dynamic_pointer_cast<CallGoal>(node)) return "stmt";
    if (std::dynamic_pointer_cast<NotGoal>(node)) return "stmt";
    if (std::dynamic_pointer_cast<OrGoal>(node)) return "stmts";
    if (std::dynamic_pointer_cast<GroupGoal>(node)) return "stmts";
    return "expr";
}

static bool valueMatchesBuiltinType(const std::shared_ptr<Expr>& value,
                                    LanguageTypeId type) {
    switch (type) {
        case LanguageTypeId::Any:
            return true;
        case LanguageTypeId::Fact: {
            const auto fact = std::dynamic_pointer_cast<MapExpr>(value);
            return fact && !fact->factType.empty();
        }
        case LanguageTypeId::Number:
        case LanguageTypeId::Decimal:
        case LanguageTypeId::Double:
        case LanguageTypeId::Float:
            return static_cast<bool>(std::dynamic_pointer_cast<NumberExpr>(value));
        case LanguageTypeId::Int: {
            const auto number = std::dynamic_pointer_cast<NumberExpr>(value);
            return number &&
                std::fabs(number->value - std::round(number->value)) < 1e-12;
        }
        case LanguageTypeId::String:
            return static_cast<bool>(std::dynamic_pointer_cast<StringExpr>(value));
        case LanguageTypeId::Array: {
            std::vector<std::shared_ptr<Expr>> items;
            return exprAsArray(value, items);
        }
        case LanguageTypeId::Expr:
        case LanguageTypeId::Stmt:
        case LanguageTypeId::Statements: {
            const auto ast = std::dynamic_pointer_cast<AstValueExpr>(value);
            if (!ast) return false;
            if (type == LanguageTypeId::Expr) {
                return ast->valueKind == AstValueKind::Expression;
            }
            if (type == LanguageTypeId::Stmt) {
                return ast->valueKind == AstValueKind::Statement;
            }
            return ast->valueKind == AstValueKind::Statements;
        }
        // mixfix is matched from retained OperatorExpression metadata during
        // overload selection; it is not a runtime value category.
        case LanguageTypeId::Mixfix:
            return false;
        case LanguageTypeId::Bool:
        case LanguageTypeId::Boolean:
            return static_cast<bool>(std::dynamic_pointer_cast<BoolExpr>(value));
        case LanguageTypeId::Unknown:
            return false;
    }
    return false;
}

static bool valueMatchesBuiltinType(const std::shared_ptr<Expr>& value,
                                    const std::string& type) {
    return valueMatchesBuiltinType(value, languageTypeIdForName(type));
}

static double requireNumber(const std::shared_ptr<Expr>& expr, const std::string& fn, const std::string& arg) {
    double number = 0.0;
    if (!argAsNumber(expr, number)) throw InterpreterError(fn + " expects numeric argument '" + arg + "'");
    return number;
}

static std::string requireString(const std::shared_ptr<Expr>& expr, const std::string& fn, const std::string& arg) {
    std::string text;
    if (!argAsString(expr, text)) throw InterpreterError(fn + " expects string argument '" + arg + "'");
    return text;
}

static fs::path sourceRootFromBase(const fs::path& baseDir) {
    fs::path current = fs::absolute(baseDir).lexically_normal();
    while (!current.empty()) {
        if (fs::exists(current / "core") && fs::is_directory(current / "core")) return current;
        fs::path parent = current.parent_path();
        if (parent == current) break;
        current = parent;
    }
    return fs::current_path();
}

static bool isBareModuleImport(const std::string& pattern) {
    if (pattern.empty()) return false;
    fs::path raw(pattern);
    if (raw.is_absolute() || raw.has_parent_path() || raw.has_extension()) return false;
    return pattern.find('*') == std::string::npos;
}

static bool validateNativePackageRegistry(const fs::path& libraryFile,
                                          const NativeModuleManifest& manifest,
                                          std::string& error) {
    const fs::path registryFile = sourceRootFromBase(libraryFile.parent_path()) / "core" / "package.fx";
    if (!fs::is_regular_file(registryFile)) {
        error = "missing authoritative package registry '" + registryFile.string() + "'";
        return false;
    }
    try {
        const Program registry = parseProgramText(readSourceFile(registryFile));
        for (const auto& statement : registry.statements) {
            if (statement->kind() != StatementKind::Clause) continue;
            const auto package = std::static_pointer_cast<ClauseStmt>(statement);
            if (!package->isFact() || package->head.name != "NativePackage") continue;
            const auto argument = [&](const char* name) -> std::shared_ptr<Expr> {
                for (const auto& arg : package->head.args) {
                    if (arg.name == name) return arg.value;
                }
                return {};
            };
            const auto name = std::dynamic_pointer_cast<StringExpr>(argument("name"));
            const auto wrapper = std::dynamic_pointer_cast<StringExpr>(argument("wrapper"));
            const auto declaration = std::dynamic_pointer_cast<StringExpr>(argument("declaration"));
            const auto abi = std::dynamic_pointer_cast<NumberExpr>(argument("abi"));
            const auto requiredManifest = std::dynamic_pointer_cast<BoolExpr>(argument("manifest"));
            if (!name || !abi || !requiredManifest || name->value != manifest.moduleName) continue;
            if (!requiredManifest->value || abi->value != static_cast<double>(manifest.abiVersion)) {
                error = "registry contract does not permit manifest ABI " + std::to_string(manifest.abiVersion) +
                    " for package '" + manifest.moduleName + "'";
                return false;
            }
            const fs::path root = sourceRootFromBase(registryFile.parent_path());
            if (!wrapper || !declaration || wrapper->value.empty() || declaration->value.empty() ||
                !fs::is_regular_file(root / wrapper->value) ||
                !fs::is_regular_file(root / declaration->value)) {
                error = "registry wrapper/declaration files are invalid for package '" + manifest.moduleName + "'";
                return false;
            }
            std::unordered_set<std::string> allowed;
            if (const auto capabilities = std::dynamic_pointer_cast<ArrayExpr>(argument("capabilities"))) {
                for (const auto& item : capabilities->items) {
                    const auto capability = std::dynamic_pointer_cast<StringExpr>(item);
                    if (!capability) {
                        error = "registry capabilities must be strings for package '" + manifest.moduleName + "'";
                        return false;
                    }
                    allowed.insert(capability->value);
                }
            }
            const auto requireAllowed = [&](bool enabled, const char* capability) -> bool {
                if (!enabled || allowed.count(capability)) return true;
                error = "manifest capability '" + std::string(capability) +
                    " is not approved for package '" + manifest.moduleName + "'";
                return false;
            };
            const auto validateContract = [&](const NativeContract& contract) -> bool {
                const auto& caps = contract.capabilities;
                return requireAllowed(caps.pure, "pure") &&
                    requireAllowed(caps.threadSafe, "thread_safe") &&
                    requireAllowed(caps.supportsBatch, "batch") &&
                    requireAllowed(caps.acceptsFactSelections, "fact_selections") &&
                    requireAllowed(caps.needsFactProjection, "fact_projection") &&
                    requireAllowed(caps.needsFactHierarchy, "fact_hierarchy");
            };
            if (!validateContract(manifest.defaultContract)) return false;
            for (const auto& function : manifest.functions) {
                if (!validateContract(function.second)) return false;
            }
            return true;
        }
        error = "package '" + manifest.moduleName + "' is not allowlisted";
        return false;
    } catch (const std::exception& ex) {
        error = "cannot read package registry: " + std::string(ex.what());
        return false;
    }
}

static fs::path resolveCoreImport(const fs::path& baseDir, const std::string& pattern) {
    fs::path root = sourceRootFromBase(baseDir);
    return fs::absolute(root / "core" / (pattern + ".fx")).lexically_normal();
}

static std::string exprToJson(const std::shared_ptr<Expr>& expr) {
    if (auto s = std::dynamic_pointer_cast<StringExpr>(expr)) return "\"" + jsonEscape(s->value) + "\"";
    if (auto b = std::dynamic_pointer_cast<BoolExpr>(expr)) return b->value ? "true" : "false";
    if (auto n = std::dynamic_pointer_cast<NumberExpr>(expr)) {
        std::ostringstream out;
        out << std::setprecision(15) << n->value;
        return out.str();
    }
    if (std::dynamic_pointer_cast<NilExpr>(expr)) return "null";
    if (auto selection =
            std::dynamic_pointer_cast<FactSelectionExpr>(expr)) {
        std::ostringstream out;
        out << "{\"__type\":\"FactSelection\",\"fact_type\":\""
            << jsonEscape(selection->factType)
            << "\",\"source\":\"memory\",\"snapshot_generation\":"
            << selection->snapshotGeneration;
        if (!selection->field.empty()) {
            out << ",\"field\":\"" << jsonEscape(selection->field) << "\"";
            if (selection->equals) {
                out << ",\"equals\":" << exprToJson(selection->equals);
            }
        }
        if (!selection->designations.empty()) {
            out << ",\"designations\":[";
            for (size_t i = 0; i < selection->designations.size(); ++i) {
                if (i) out << ",";
                out << "\"" << jsonEscape(selection->designations[i]) << "\"";
            }
            out << "]";
        }
        out << "}";
        return out.str();
    }
    if (auto a = std::dynamic_pointer_cast<ArrayExpr>(expr)) {
        std::ostringstream out;
        out << "[";
        for (size_t i = 0; i < a->items.size(); ++i) {
            if (i) out << ",";
            out << exprToJson(a->items[i]);
        }
        out << "]";
        return out.str();
    }
    if (auto m = std::dynamic_pointer_cast<MapExpr>(expr)) {
        std::ostringstream out;
        out << "{";
        for (size_t i = 0; i < m->entries.size(); ++i) {
            if (i) out << ",";
            out << "\"" << jsonEscape(m->entries[i].key) << "\":" << exprToJson(m->entries[i].value);
        }
        out << "}";
        return out.str();
    }
    if (auto t = std::dynamic_pointer_cast<TermExpr>(expr)) {
        std::vector<MapEntry> entries;
        entries.push_back(MapEntry{"__term", std::make_shared<StringExpr>(t->name)});
        for (size_t i = 0; i < t->args.size(); ++i) {
            const auto& arg = t->args[i];
            entries.push_back(MapEntry{arg.name.empty() ? ("arg" + std::to_string(i)) : arg.name, arg.value});
        }
        return exprToJson(std::make_shared<MapExpr>(std::move(entries)));
    }
    return "\"" + jsonEscape(expr ? expr->debug() : "") + "\"";
}

static void skipJsonWs(const std::string& text, size_t& pos) {
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) pos++;
}

static bool parseJsonString(const std::string& text, size_t& pos, std::string& out) {
    skipJsonWs(text, pos);
    if (pos >= text.size() || text[pos] != '"') return false;
    pos++;
    out.clear();
    while (pos < text.size() && text[pos] != '"') {
        char c = text[pos++];
        if (c == '\\' && pos < text.size()) {
            char esc = text[pos++];
            switch (esc) {
                case 'n': out.push_back('\n'); break;
                case 't': out.push_back('\t'); break;
                case 'r': out.push_back('\r'); break;
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                default: out.push_back(esc); break;
            }
        } else {
            out.push_back(c);
        }
    }
    if (pos >= text.size() || text[pos] != '"') return false;
    pos++;
    return true;
}

static bool parseJsonValue(const std::string& text, size_t& pos, std::shared_ptr<Expr>& out);

static bool parseJsonObjectValue(const std::string& text, size_t& pos, std::shared_ptr<Expr>& out) {
    skipJsonWs(text, pos);
    if (pos >= text.size() || text[pos] != '{') return false;
    pos++;
    std::vector<MapEntry> entries;
    skipJsonWs(text, pos);
    if (pos < text.size() && text[pos] == '}') {
        pos++;
        out = std::make_shared<MapExpr>(std::move(entries));
        return true;
    }
    while (pos < text.size()) {
        std::string key;
        std::shared_ptr<Expr> value;
        if (!parseJsonString(text, pos, key)) return false;
        skipJsonWs(text, pos);
        if (pos >= text.size() || text[pos] != ':') return false;
        pos++;
        if (!parseJsonValue(text, pos, value)) return false;
        entries.push_back(MapEntry{key, value});
        skipJsonWs(text, pos);
        if (pos < text.size() && text[pos] == ',') {
            pos++;
            continue;
        }
        if (pos < text.size() && text[pos] == '}') {
            pos++;
            out = std::make_shared<MapExpr>(std::move(entries));
            return true;
        }
        return false;
    }
    return false;
}

static bool parseJsonArrayValue(const std::string& text, size_t& pos, std::shared_ptr<Expr>& out) {
    skipJsonWs(text, pos);
    if (pos >= text.size() || text[pos] != '[') return false;
    pos++;
    std::vector<std::shared_ptr<Expr>> items;
    skipJsonWs(text, pos);
    if (pos < text.size() && text[pos] == ']') {
        pos++;
        out = std::make_shared<ArrayExpr>(std::move(items));
        return true;
    }
    while (pos < text.size()) {
        std::shared_ptr<Expr> value;
        if (!parseJsonValue(text, pos, value)) return false;
        items.push_back(value);
        skipJsonWs(text, pos);
        if (pos < text.size() && text[pos] == ',') {
            pos++;
            continue;
        }
        if (pos < text.size() && text[pos] == ']') {
            pos++;
            out = std::make_shared<ArrayExpr>(std::move(items));
            return true;
        }
        return false;
    }
    return false;
}

static bool parseJsonValue(const std::string& text, size_t& pos, std::shared_ptr<Expr>& out) {
    skipJsonWs(text, pos);
    if (pos >= text.size()) return false;
    if (text.compare(pos, 4, "null") == 0) {
        pos += 4;
        out = std::make_shared<NilExpr>();
        return true;
    }
    if (text.compare(pos, 4, "true") == 0) {
        pos += 4;
        out = std::make_shared<BoolExpr>(true);
        return true;
    }
    if (text.compare(pos, 5, "false") == 0) {
        pos += 5;
        out = std::make_shared<BoolExpr>(false);
        return true;
    }
    if (text[pos] == '{') return parseJsonObjectValue(text, pos, out);
    if (text[pos] == '[') return parseJsonArrayValue(text, pos, out);
    if (text[pos] == '"') {
        std::string value;
        if (!parseJsonString(text, pos, value)) return false;
        out = std::make_shared<StringExpr>(value);
        return true;
    }
    size_t start = pos;
    if (text[pos] == '-') pos++;
    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) pos++;
    if (pos < text.size() && text[pos] == '.') {
        pos++;
        while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) pos++;
    }
    if (start == pos || (start + 1 == pos && text[start] == '-')) return false;
    out = std::make_shared<NumberExpr>(std::stod(text.substr(start, pos - start)));
    return true;
}

static bool parseFlatJsonObject(const std::string& text, std::shared_ptr<Expr>& out) {
    size_t pos = 0;
    if (!parseJsonValue(text, pos, out)) return false;
    skipJsonWs(text, pos);
    return pos == text.size();
}

Interpreter::~Interpreter() {
    joinThreads();
    closeNativeLibraries();
}

void Interpreter::closeNativeLibraries() {
    for (auto& library : nativeLibraries_) closeSharedLibrary(library.handle);
    nativeLibraries_.clear();
    nativeLibraryPaths_.clear();
}

void Interpreter::beginModuleTransaction() {
    if (moduleTransaction_) {
        throw InterpreterError("Nested module transactions are not supported");
    }
    auto transaction = std::make_unique<ModuleTransactionState>();
    // Clause objects are immutable and shared; the bucket topology is not.
    // Keep an independent bucket table for rollback so normal streamed
    // registration does not invalidate live ClauseList pointers through COW.
    transaction->clauses = std::make_shared<ClauseTable>(*clauses_);
    transaction->operators = std::make_shared<OperatorRegistry>(*operators_);
    transaction->operatorClauses = operatorClauses_;
    transaction->autoEntryCalls = autoEntryCalls_;
    transaction->autoEntryResults = autoEntryResults_;
    transaction->memory = memory_;
    transaction->globals = globals_;
    transaction->referencesBySource = referencesBySource_;
    transaction->nextReferenceAttachmentId = nextReferenceAttachmentId_;
    transaction->nextReferenceCreationOrder = nextReferenceCreationOrder_;
    transaction->referenceEvaluationGeneration = referenceEvaluationGeneration_;
    transaction->loadedFiles = loadedFiles_;
    transaction->packageDiscoveryAttempts = packageDiscoveryAttempts_;
    transaction->clauseOrigins = clauseOrigins_;
    transaction->programGeneration = programGeneration_;
    transaction->symbolGenerations = symbolGenerations_;
    transaction->moduleLoads = moduleLoads_;
    transaction->cacheInvalidationDepth = cacheInvalidationDepth_;
    transaction->pendingCacheInvalidation = pendingCacheInvalidation_;
    transaction->contraries = contraries_;
    moduleTransaction_ = std::move(transaction);
}

void Interpreter::commitModuleTransaction() {
    if (!moduleTransaction_) {
        throw InterpreterError("No module transaction is active");
    }
    moduleTransaction_.reset();
    // The staged clause catalog can have moved buckets while it was being
    // populated. These caches retain raw clause pointers, so publish only
    // after dropping all staging-era lookup/preparation entries.
    clauseLookupCache_.clear();
    methodRuntimeCache_.clear();
}

void Interpreter::rollbackModuleTransaction() {
    if (!moduleTransaction_) return;
    const ModuleTransactionState& transaction = *moduleTransaction_;
    clauses_ = transaction.clauses;
    operators_ = transaction.operators;
    operatorClauses_ = transaction.operatorClauses;
    autoEntryCalls_ = transaction.autoEntryCalls;
    autoEntryResults_ = transaction.autoEntryResults;
    memory_ = transaction.memory;
    globals_ = transaction.globals;
    referencesBySource_ = transaction.referencesBySource;
    nextReferenceAttachmentId_ = transaction.nextReferenceAttachmentId;
    nextReferenceCreationOrder_ = transaction.nextReferenceCreationOrder;
    referenceEvaluationGeneration_ = transaction.referenceEvaluationGeneration;
    loadedFiles_ = transaction.loadedFiles;
    packageDiscoveryAttempts_ = transaction.packageDiscoveryAttempts;
    clauseOrigins_ = transaction.clauseOrigins;
    programGeneration_ = transaction.programGeneration;
    symbolGenerations_ = transaction.symbolGenerations;
    moduleLoads_ = transaction.moduleLoads;
    cacheInvalidationDepth_ = transaction.cacheInvalidationDepth;
    pendingCacheInvalidation_ = transaction.pendingCacheInvalidation;
    contraries_ = transaction.contraries;
    moduleTransaction_.reset();
    // Plans can hold pointers into the staged clause table, so restore them
    // only through the normal cache boundary after the roots are replaced.
    clearCachesNow();
}

void Interpreter::joinThreads() {
    std::vector<std::shared_ptr<ThreadTask>> tasks;
    {
        std::lock_guard<std::mutex> lock(threadMutex_);
        tasks.reserve(threadTasks_.size());
        for (const auto& entry : threadTasks_) tasks.push_back(entry.second);
    }
    for (const auto& task : tasks) {
        if (task && task->worker.joinable()) task->worker.join();
    }
}

std::shared_ptr<Expr> Interpreter::makeThreadHandle(const std::string& id) const {
    return std::make_shared<MapExpr>(std::vector<MapEntry>{
        {internalSymbolString(InternalSymbolKind::Type), std::make_shared<StringExpr>("Thread")},
        {"id", std::make_shared<StringExpr>(id)}
    });
}

std::shared_ptr<Interpreter::ThreadTask> Interpreter::threadTaskFromHandle(const std::shared_ptr<Expr>& handle) {
    auto typeValue = findMapValue(handle, internalSymbolString(InternalSymbolKind::Type));
    auto idValue = findMapValue(handle, "id");
    auto type = std::dynamic_pointer_cast<StringExpr>(typeValue);
    auto id = std::dynamic_pointer_cast<StringExpr>(idValue);
    if (!type || type->value != "Thread" || !id || id->value.empty()) {
        throw InterpreterError("thread API expects a valid thread handle");
    }
    std::lock_guard<std::mutex> lock(threadMutex_);
    auto it = threadTasks_.find(id->value);
    if (it == threadTasks_.end()) throw InterpreterError("Unknown thread handle: " + id->value);
    return it->second;
}

std::string Interpreter::createThreadTask(const std::string& functionName) {
    if (functionName.empty()) throw InterpreterError("thread.createThread expects non-empty function name");
    if (!hasMethod(functionName)) {
        throw InterpreterError("Thread function '" + functionName + "' not found");
    }
    std::lock_guard<std::mutex> lock(threadMutex_);
    std::string id = "thread-" + std::to_string(++threadCounter_);
    threadTasks_[id] = std::make_shared<ThreadTask>(functionName);
    return id;
}

std::string Interpreter::startThreadTask(const std::shared_ptr<Expr>& handle) {
    auto task = threadTaskFromHandle(handle);
    {
        std::lock_guard<std::mutex> lock(threadMutex_);
        if (task->started) return task->status;
        task->started = true;
        task->status = "running";
    }

    auto clausesSnapshot = clauses_;
    auto operatorsSnapshot = operators_;
    auto operatorClausesSnapshot = operatorClauses_;
    auto memorySnapshot = memory_;
    auto globalsSnapshot = cloneEnv(globals_.values());
    auto loadedFilesSnapshot = loadedFiles_;
    auto packageDiscoveryAttemptsSnapshot = packageDiscoveryAttempts_;
    auto currentLoadingFileSnapshot = currentLoadingFile_;
    auto nativeLibraryPathsSnapshot = nativeLibraryPaths_;
    auto contrariesSnapshot = contraries_;
    auto functionName = task->functionName;

    task->worker = std::thread([this,
                                task,
                                functionName,
                                clausesSnapshot = std::move(clausesSnapshot),
                                operatorsSnapshot = std::move(operatorsSnapshot),
                                operatorClausesSnapshot = std::move(operatorClausesSnapshot),
                                memorySnapshot = std::move(memorySnapshot),
                                globalsSnapshot = std::move(globalsSnapshot),
                                loadedFilesSnapshot = std::move(loadedFilesSnapshot),
                                packageDiscoveryAttemptsSnapshot =
                                    std::move(packageDiscoveryAttemptsSnapshot),
                                currentLoadingFileSnapshot = std::move(currentLoadingFileSnapshot),
                                nativeLibraryPathsSnapshot = std::move(nativeLibraryPathsSnapshot),
                                contrariesSnapshot = std::move(contrariesSnapshot)]() mutable {
        try {
            Interpreter child;
            child.clauses_ = std::move(clausesSnapshot);
            child.operators_ = std::move(operatorsSnapshot);
            child.operatorClauses_ = std::move(operatorClausesSnapshot);
            child.memory_ = std::move(memorySnapshot);
            child.globals_.replaceValues(std::move(globalsSnapshot));
            child.loadedFiles_ = std::move(loadedFilesSnapshot);
            child.packageDiscoveryAttempts_ = std::move(packageDiscoveryAttemptsSnapshot);
            child.currentLoadingFile_ = std::move(currentLoadingFileSnapshot);
            child.contraries_ = std::move(contrariesSnapshot);
            for (const auto& nativePath : nativeLibraryPathsSnapshot) {
                child.loadNativeLibrary(nativePath);
            }

            if (!child.hasMethod(functionName)) {
                throw InterpreterError("Thread function '" + functionName + "' not found");
            }

            Call call(functionName, {});
            std::vector<Solution> solutions;
            auto* clauses = child.findClauses(functionName, symbolIdForName(functionName));
            if (clauses) {
                for (const auto& clause : *clauses) {
                    if (!child.isMethodClause(*clause)) continue;
                    child.solveMethodCall(call, clause, Env{}, solutions, 1, 0);
                    if (!solutions.empty()) break;
                }
            }

            std::string result = "false";
            if (!solutions.empty()) {
                auto returned = findReturnValue(solutions.front().env);
                result = !returned
                    ? "true"
                    : child.valueToString(returned);
            }

            std::lock_guard<std::mutex> lock(threadMutex_);
            task->result = result;
            task->status = "finished";
        } catch (const std::exception& ex) {
            std::lock_guard<std::mutex> lock(threadMutex_);
            task->error = ex.what();
            task->status = "error";
        }
    });

    return "started";
}

std::string Interpreter::threadTaskStatus(const std::shared_ptr<Expr>& handle) {
    auto task = threadTaskFromHandle(handle);
    std::lock_guard<std::mutex> lock(threadMutex_);
    return task->status;
}

std::shared_ptr<Expr> Interpreter::threadTaskResult(const std::shared_ptr<Expr>& handle) {
    auto task = threadTaskFromHandle(handle);
    if (task->worker.joinable()) task->worker.join();
    std::lock_guard<std::mutex> lock(threadMutex_);
    if (!task->error.empty()) throw InterpreterError("Thread failed: " + task->error);
    if (!task->started) throw InterpreterError("Thread has not been started");
    return std::make_shared<StringExpr>(task->result);
}

void Interpreter::collectExecutionGarbage() {
    envFramePool_.collectGarbage(kMaxCachedEnvFrames);
}

Env Interpreter::copyExecutionEnvironment(const Env& source) {
    ++environmentCopies_;
    return cloneEnv(source);
}

void Interpreter::loadNativeLibrary(const std::filesystem::path& file) {
    fs::path normalized = fs::absolute(file).lexically_normal();
    if (nativeLibraryPaths_.count(normalized)) return;
    void* handle = openSharedLibrary(normalized);
    if (!handle) {
        throw InterpreterError("Cannot load native module library '" + normalized.string() + "': " + sharedLibraryError());
    }
    auto call = reinterpret_cast<NativeCallFn>(findSharedLibrarySymbol(handle, "felidae_native_call"));
    auto free = reinterpret_cast<NativeFreeFn>(findSharedLibrarySymbol(handle, "felidae_native_free"));
    auto manifestFn = reinterpret_cast<NativeManifestFn>(findSharedLibrarySymbol(handle, "felidae_native_manifest_v1"));
    if (!call || !free || !manifestFn) {
        closeSharedLibrary(handle);
        throw InterpreterError("Native module library '" + normalized.string() +
                               "' must export felidae_native_call, felidae_native_free, and felidae_native_manifest_v1");
    }
    const char* rawManifest = manifestFn();
    if (!rawManifest) {
        closeSharedLibrary(handle);
        throw InterpreterError("Native module library '" + normalized.string() + "' returned a null manifest");
    }
    NativeModuleManifest manifest;
    std::string manifestError;
    if (!parseNativeModuleManifest(rawManifest, manifest, manifestError)) {
        closeSharedLibrary(handle);
        throw InterpreterError("Native module library '" + normalized.string() +
                               "' has an invalid manifest: " + manifestError);
    }
    if (!validateNativePackageRegistry(normalized, manifest, manifestError)) {
        closeSharedLibrary(handle);
        throw InterpreterError("Native module library '" + normalized.string() +
                               "' is not approved by core/package.fx: " + manifestError);
    }
    const std::string expectedModule = nativeModuleNameFromPath(normalized);
    if (manifest.moduleName != expectedModule) {
        closeSharedLibrary(handle);
        throw InterpreterError("Native module manifest names '" + manifest.moduleName +
                               "' but library path resolves to '" + expectedModule + "'");
    }
    nativeLibraries_.push_back(NativeLibrary{normalized, manifest.moduleName, handle, call, free, std::move(manifest)});
    nativeLibraryPaths_.insert(normalized);
}

void Interpreter::addProgram(const Program& program) {
    validateNegationStratification(program);
    beginCacheInvalidationBatch();
    try {
        for (const auto& statement : program.statements) {
            switch (statement->kind()) {
                case StatementKind::Import:
                    break;
                case StatementKind::Clause: {
                    auto clause = std::static_pointer_cast<ClauseStmt>(statement);
                    if (clause->clauseKind == ClauseKind::EntryCall) {
                        autoEntryCalls_.push_back(clause->head);
                        autoEntryResults_.push_back(executeEntryCall(clause->head));
                        break;
                    }
                    addClause(clause);
                    break;
                }
                case StatementKind::GlobalBinding: {
                    auto binding = std::static_pointer_cast<GlobalBindingStmt>(statement);
                    if (globals_.count(binding->name) || findClauses(binding->name, symbolIdForName(binding->name))) {
                        throw InterpreterError("Global '" + binding->name + "' is already defined and immutable");
                    }
                    Env env;
                    std::shared_ptr<Expr> value;
                    if (!evalExprValue(binding->expr, env, value)) {
                        throw InterpreterError("Cannot evaluate global binding '" + binding->name + "'");
                    }
                    globals_.bind(binding->name, value, currentLoadingFile_);
                    Call head(binding->name, std::vector<Arg>{{"value", value->clone()}});
                    addClause(std::make_shared<ClauseStmt>(std::move(head), std::vector<std::shared_ptr<Goal>>{}));
                    break;
                }
            }
        }
    } catch (...) {
        endCacheInvalidationBatch();
        throw;
    }
    endCacheInvalidationBatch();
}

void Interpreter::addStreamedStatement(std::shared_ptr<Statement> statement) {
    if (!statement || statement->kind() == StatementKind::Import) return;
    if (statement->kind() == StatementKind::Clause) {
        // Keep statement alive for the validated rule path below. Moving it
        // into the cast made non-fact clauses become null before they were
        // wrapped in the singleton Program.
        auto clause = std::static_pointer_cast<ClauseStmt>(statement);
        if (clause->clauseKind == ClauseKind::EntryCall) {
            autoEntryCalls_.push_back(clause->head);
            autoEntryResults_.push_back(executeEntryCall(clause->head));
            return;
        }
        // Facts cannot introduce rule dependency edges, globals, or method
        // behavior. Register them directly while parsing so a large fact
        // module does not allocate one temporary Program per fact.
        if (clause->isFact()) {
            addClause(std::move(clause));
            return;
        }
    }

    // Rule/global registration retains the established validation path. In a
    // module transaction an error restores all roots, including facts already
    // streamed before this statement.
    Program singleton;
    singleton.addStatement(std::move(statement));
    addProgram(singleton);
}

void Interpreter::validateNegationStratification(const Program& program) const {
    // Fact-only streaming chunks cannot add rule-dependency edges.  Skipping
    // them is essential for large fact imports: otherwise every chunk would
    // rescan all prior facts merely to rebuild an empty rule graph.
    bool containsRelationalRule = false;
    for (const auto& statement : program.statements) {
        if (statement->kind() != StatementKind::Clause) continue;
        const auto clause = std::static_pointer_cast<ClauseStmt>(statement);
        if (!clause->isFact() && !isMethodClause(*clause)) {
            containsRelationalRule = true;
            break;
        }
    }
    if (!containsRelationalRule) return;

    struct Edge {
        std::string target;
        bool negative = false;
    };
    std::unordered_map<std::string, std::vector<Edge>> graph;

    const auto collectGoals = [&](const auto& self,
                                  const std::string& source,
                                  const std::vector<std::shared_ptr<Goal>>& goals) -> void {
        for (const auto& goal : goals) {
            if (auto call = std::dynamic_pointer_cast<CallGoal>(goal)) {
                graph[source].push_back(Edge{call->call.name, false});
            } else if (auto negated = std::dynamic_pointer_cast<NotGoal>(goal)) {
                graph[source].push_back(Edge{negated->call.name, true});
            } else if (auto group = std::dynamic_pointer_cast<GroupGoal>(goal)) {
                self(self, source, group->goals);
            } else if (auto disjunction = std::dynamic_pointer_cast<OrGoal>(goal)) {
                for (const auto& branch : disjunction->branches) self(self, source, branch);
            } else if (auto conditional = std::dynamic_pointer_cast<IfGoal>(goal)) {
                self(self, source, std::vector<std::shared_ptr<Goal>>{conditional->condition});
                self(self, source, conditional->thenBranch);
                self(self, source, conditional->elseBranch);
            }
        }
    };
    const auto addClause = [&](const std::shared_ptr<ClauseStmt>& clause) {
        if (!clause || clause->isFact() || isMethodClause(*clause)) return;
        auto& edges = graph[clause->head.name];
        (void)edges;
        collectGoals(collectGoals, clause->head.name, clause->body);
        for (const auto& branch : clause->fallbackBranches) collectGoals(collectGoals, clause->head.name, branch);
    };

    for (const auto& bucket : *clauses_) {
        for (const auto& nameBucket : bucket.second) {
            for (const auto& clause : nameBucket.clauses) addClause(clause);
        }
    }
    for (const auto& statement : program.statements) {
        if (statement->kind() != StatementKind::Clause) continue;
        addClause(std::static_pointer_cast<ClauseStmt>(statement));
    }

    const auto reaches = [&](const auto& self,
                             const std::string& current,
                             const std::string& target,
                             std::unordered_set<std::string>& visited) -> bool {
        if (current == target) return true;
        if (!visited.insert(current).second) return false;
        const auto edges = graph.find(current);
        if (edges == graph.end()) return false;
        for (const auto& edge : edges->second) {
            if (self(self, edge.target, target, visited)) return true;
        }
        return false;
    };
    for (const auto& node : graph) {
        for (const auto& edge : node.second) {
            if (!edge.negative) continue;
            std::unordered_set<std::string> visited;
            if (reaches(reaches, edge.target, node.first, visited)) {
                throw InterpreterError("Unstratified negative dependency cycle involving '" + node.first + "'");
            }
        }
    }
}

void Interpreter::addClause(std::shared_ptr<ClauseStmt> clause) {
    const std::string clauseName = clause->head.name;
    for (const auto& annotation : clause->annotations) {
        if (annotation.builtinId != BuiltinId::OverloadAnnotation &&
            annotation.builtinId != BuiltinId::MatcherAnnotation &&
            annotation.builtinId != BuiltinId::MixfixAnnotation) {
            if (!hasMethod(annotation.name) && !nativeDeclarationFor(annotation.name)) {
                throw InterpreterError("Annotation method '" + annotation.name +
                                       "' is not declared before '" + clauseName + "'");
            }
            TermExpr invocation(annotation.name, {}, annotation.builtinId);
            invocation.nameId = annotation.nameId;
            invocation.args.reserve(annotation.args.size());
            for (const auto& argument : annotation.args) {
                invocation.args.push_back(Arg{argument.name, argument.nameId, argument.value->clone()});
            }
            if (const auto* annotationClauses = findClauses(annotation.name, annotation.nameId)) {
                for (const auto& annotationClause : *annotationClauses) {
                    if (!isMethodClause(*annotationClause)) continue;
                    const auto plans = buildMethodParamPlan(*annotationClause);
                    for (std::size_t i = 0; i < plans.size(); ++i) {
                        const auto& plan = plans[i];
                        if (plan.typeId != LanguageTypeId::Stmt &&
                            plan.typeId != LanguageTypeId::Statements) continue;
                        const bool supplied = std::any_of(
                            invocation.args.begin(), invocation.args.end(), [&](const Arg& argument) {
                                return argument.name == annotationClause->head.args[i].name;
                            });
                        if (supplied) continue;
                        std::vector<std::shared_ptr<AstNode>> nodes;
                        AstValueKind valueKind = AstValueKind::Statement;
                        std::string nodeKind;
                        if (plan.typeId == LanguageTypeId::Stmt) {
                            nodes.push_back(clause);
                            nodeKind = astNodeKind(clause);
                        } else {
                            valueKind = AstValueKind::Statements;
                            for (const auto& goal : clause->body) nodes.push_back(goal);
                            for (const auto& branch : clause->fallbackBranches) {
                                for (const auto& goal : branch) nodes.push_back(goal);
                            }
                            nodeKind = "stmts";
                        }
                        invocation.args.push_back(Arg{
                            annotationClause->head.args[i].name,
                            std::make_shared<AstValueExpr>(
                                valueKind, std::move(nodes), std::move(nodeKind))});
                    }
                    break;
                }
            }
            std::shared_ptr<Expr> ignored;
            if (!evalCallAsValue(invocation, Env{}, ignored)) {
                throw InterpreterError("Annotation method '" + annotation.name +
                                       "' produced no result for declaration '" + clauseName + "'");
            }
            continue;
        }
        ParsedOperatorAnnotation parsed;
        try {
            parsed = decodeOperatorAnnotation(annotation);
        } catch (const std::runtime_error& error) {
            throw InterpreterError(error.what());
        }
        const auto* pa…79819 tokens truncated…dynamic_pointer_cast<NumberExpr>(ra)) {
        auto nb = std::dynamic_pointer_cast<NumberExpr>(rb);
        return nb && std::fabs(na->value - nb->value) < 1e-12;
    }
    if (auto ba = std::dynamic_pointer_cast<BoolExpr>(ra)) {
        auto bb = std::dynamic_pointer_cast<BoolExpr>(rb);
        if (bb) return ba->value == bb->value;
        auto sb = std::dynamic_pointer_cast<StringExpr>(rb);
        return sb && sb->value == (ba->value ? "true" : "false");
    }
    if (auto bb = std::dynamic_pointer_cast<BoolExpr>(rb)) {
        auto sa = std::dynamic_pointer_cast<StringExpr>(ra);
        return sa && sa->value == (bb->value ? "true" : "false");
    }
    if (std::dynamic_pointer_cast<NilExpr>(ra) || std::dynamic_pointer_cast<NilExpr>(rb)) {
        return static_cast<bool>(std::dynamic_pointer_cast<NilExpr>(ra)) &&
               static_cast<bool>(std::dynamic_pointer_cast<NilExpr>(rb));
    }
    if (auto ta = std::dynamic_pointer_cast<TermExpr>(ra)) {
        auto tb = std::dynamic_pointer_cast<TermExpr>(rb);
        if (!tb || ta->nameId != tb->nameId || ta->args.size() != tb->args.size()) return false;
        for (size_t i = 0; i < ta->args.size(); ++i) {
            if (!unifyExpr(ta->args[i].value, tb->args[i].value, env)) return false;
        }
        return true;
    }
    if (auto aa = std::dynamic_pointer_cast<ArrayExpr>(ra)) {
        auto ab = std::dynamic_pointer_cast<ArrayExpr>(rb);
        if (!ab || aa->items.size() != ab->items.size()) return false;
        for (size_t i = 0; i < aa->items.size(); ++i) {
            if (!unifyExpr(aa->items[i], ab->items[i], env)) return false;
        }
        return true;
    }
    if (auto ma = std::dynamic_pointer_cast<MapExpr>(ra)) {
        auto mb = std::dynamic_pointer_cast<MapExpr>(rb);
        if (!mb || ma->entries.size() != mb->entries.size()) return false;
        for (const auto& entry : ma->entries) {
            auto other = findMapValue(rb, entry.key);
            if (!other || !unifyExpr(entry.value, other, env)) return false;
        }
        return true;
    }

    return false;
}

std::shared_ptr<Expr> Interpreter::resolveExpr(const std::shared_ptr<Expr>& expr, const Env& env) const {
    auto var = std::dynamic_pointer_cast<VarExpr>(expr);
    if (var && var->nameId == InternalSymbol::SystemResultId) {
        if (pipelineResults_.empty()) {
            throw InterpreterError("system.result is only available inside a then pipeline");
        }
        return pipelineResults_.back()->clone();
    }
    if (!var) {
        if (auto access = std::dynamic_pointer_cast<AccessExpr>(expr)) {
            if (access->keyId == InternalSymbol::ResultId) {
                auto targetVar = std::dynamic_pointer_cast<VarExpr>(access->target);
                if (targetVar && targetVar->nameId == InternalSymbol::SystemId) {
                    if (pipelineResults_.empty()) {
                        throw InterpreterError("system.result is only available inside a then pipeline");
                    }
                    return pipelineResults_.back()->clone();
                }
            }
            std::shared_ptr<Expr> target;
            if (!const_cast<Interpreter*>(this)->evalExprValue(access->target, env, target)) return expr;
            auto value = findMapValue(target, access->key);
            if (!value) return expr;
            return resolveExpr(value, env);
        }
        return expr;
    }

    auto it = env.find(var->nameId);
    if (it == env.end()) {
        auto globalIt = globals_.find(var->nameId);
        if (globalIt != globals_.end()) return resolveExpr(globalIt->second, env);
        return expr;
    }

    // Follow standardized variable aliases to their resolved value.
    return resolveExpr(it->second, env);
}

std::string Interpreter::exprToString(const std::shared_ptr<Expr>& expr, const Env& env) const {
    auto resolved = resolveExpr(expr, env);
    std::shared_ptr<Expr> value;
    if (const_cast<Interpreter*>(this)->evalExprValue(resolved, env, value)) {
        return value->debug();
    }
    return resolved->debug();
}

std::shared_ptr<ClauseStmt> Interpreter::standardizeApart(const std::shared_ptr<ClauseStmt>& originalClause) {
    const ClauseStmt& clause = *originalClause;
    const auto cachedRequirement = clauseRenameRequirements_.find(originalClause.get());
    if (cachedRequirement != clauseRenameRequirements_.end() && !cachedRequirement->second) {
        return originalClause;
    }
    const auto goalNeedsRename = [&](const auto& self, const std::shared_ptr<Goal>& goal) -> bool {
        switch (goal->kind()) {
            case GoalKind::Call:
                for (const auto& arg : std::static_pointer_cast<CallGoal>(goal)->call.args) {
                    if (exprNeedsRename(arg.value)) return true;
                }
                return false;
            case GoalKind::Not:
                for (const auto& arg : std::static_pointer_cast<NotGoal>(goal)->call.args) {
                    if (exprNeedsRename(arg.value)) return true;
                }
                return false;
            case GoalKind::Assign:
            case GoalKind::MultiAssign:
                return true;
            case GoalKind::Binary: {
                const auto binary = std::static_pointer_cast<BinaryGoal>(goal);
                return exprNeedsRename(binary->left) || exprNeedsRename(binary->right);
            }
            case GoalKind::Where:
                return self(self, std::static_pointer_cast<WhereGoal>(goal)->condition);
            case GoalKind::Return:
                for (const auto& field : std::static_pointer_cast<ReturnGoal>(goal)->fields) {
                    if (exprNeedsRename(field.value)) return true;
                }
                return false;
            case GoalKind::Group:
                for (const auto& nested : std::static_pointer_cast<GroupGoal>(goal)->goals) {
                    if (self(self, nested)) return true;
                }
                return false;
            case GoalKind::Or:
                for (const auto& branch : std::static_pointer_cast<OrGoal>(goal)->branches) {
                    for (const auto& nested : branch) if (self(self, nested)) return true;
                }
                return false;
            case GoalKind::If: {
                const auto conditional = std::static_pointer_cast<IfGoal>(goal);
                if (self(self, conditional->condition)) return true;
                for (const auto& nested : conditional->thenBranch) if (self(self, nested)) return true;
                for (const auto& nested : conditional->elseBranch) if (self(self, nested)) return true;
                return false;
            }
        }
        return true;
    };
    bool needsRename = false;
    for (const auto& arg : clause.head.args) {
        if (exprNeedsRename(arg.value)) { needsRename = true; break; }
    }
    if (!needsRename) {
        for (const auto& goal : clause.body) {
            if (goalNeedsRename(goalNeedsRename, goal)) { needsRename = true; break; }
        }
    }
    if (!needsRename) {
        for (const auto& branch : clause.fallbackBranches) {
            for (const auto& goal : branch) {
                if (goalNeedsRename(goalNeedsRename, goal)) { needsRename = true; break; }
            }
            if (needsRename) break;
        }
    }
    clauseRenameRequirements_[originalClause.get()] = needsRename;
    if (!needsRename) return originalClause;
    ++standardizedClauses_;
    RenameMap names;
    Call head;
    head.name = clause.head.name;
    head.nameId = clause.head.nameId;
    head.builtinId = clause.head.builtinId;
    bool methodHead = isMethodClause(clause);
    for (const auto& arg : clause.head.args) {
        auto typeExpr = std::dynamic_pointer_cast<VarExpr>(arg.value);
        if (methodHead && typeExpr && isFelidaeTypeAnnotationName(typeExpr->name)) {
            head.args.push_back(Arg{arg.name, arg.nameId, arg.value->clone()});
        } else {
            head.args.push_back(Arg{arg.name, arg.nameId, renameExpr(arg.value, names)});
        }
    }
    std::vector<std::shared_ptr<Goal>> body;
    body.reserve(clause.body.size());
    for (const auto& g : clause.body) body.push_back(renameGoal(g, names));
    std::vector<std::vector<std::shared_ptr<Goal>>> fallbackBranches;
    fallbackBranches.reserve(clause.fallbackBranches.size());
    for (const auto& branch : clause.fallbackBranches) {
        std::vector<std::shared_ptr<Goal>> renamedBranch;
        renamedBranch.reserve(branch.size());
        for (const auto& goal : branch) renamedBranch.push_back(renameGoal(goal, names));
        fallbackBranches.push_back(std::move(renamedBranch));
    }
    return std::make_shared<ClauseStmt>(
        std::move(head),
        clause.parentNames,
        std::move(body),
        std::move(fallbackBranches),
        clause.emptyDeclaration,
        clause.clauseKind);
}

SymbolId Interpreter::renamedId(SymbolId original, RenameMap& names) {
    const auto found = names.find(original);
    if (found != names.end()) return found->second;
    const SymbolId generated = symbolInterner().makeGenerated();
    names.emplace(original, generated);
    return generated;
}

Call Interpreter::renameCall(const Call& call, RenameMap& names) {
    Call out;
    out.name = call.name;
    out.nameId = call.nameId;
    out.builtinId = call.builtinId;
    for (const auto& a : call.args) {
        if ((call.builtinId == BuiltinId::Throw && a.name == "target") ||
            (call.builtinId == BuiltinId::Instanceof &&
             (a.name == "type" || a.name == "parent" || a.name == "of"))) {
            out.args.push_back(Arg{a.name, a.nameId, a.value->clone()});
            continue;
        }
        out.args.push_back(Arg{a.name, a.nameId, renameExpr(a.value, names)});
    }
    return out;
}

std::shared_ptr<Goal> Interpreter::renameGoal(const std::shared_ptr<Goal>& goal, RenameMap& names) {
    switch (goal->kind()) {
        case GoalKind::Call: {
            auto cg = std::static_pointer_cast<CallGoal>(goal);
            return std::make_shared<CallGoal>(renameCall(cg->call, names));
        }
        case GoalKind::Not: {
            auto ng = std::static_pointer_cast<NotGoal>(goal);
            return std::make_shared<NotGoal>(renameCall(ng->call, names));
        }
        case GoalKind::Assign: {
            auto ag = std::static_pointer_cast<AssignGoal>(goal);
            const SymbolId id = renamedId(ag->nameId, names);
            return std::make_shared<AssignGoal>(symbolNameForId(id), id, renameExpr(ag->expr, names));
        }
        case GoalKind::MultiAssign: {
            auto mag = std::static_pointer_cast<MultiAssignGoal>(goal);
            std::vector<AssignmentTarget> targets;
            targets.reserve(mag->targets.size());
            for (const auto& target : mag->targets) {
                const SymbolId id = renamedId(target.nameId, names);
                targets.emplace_back(symbolNameForId(id), id, target.type);
            }
            return std::make_shared<MultiAssignGoal>(std::move(targets), renameExpr(mag->expr, names));
        }
        case GoalKind::Binary: {
            auto bg = std::static_pointer_cast<BinaryGoal>(goal);
            return std::make_shared<BinaryGoal>(renameExpr(bg->left, names), bg->op, renameExpr(bg->right, names));
        }
        case GoalKind::Where: {
            auto wg = std::static_pointer_cast<WhereGoal>(goal);
            return std::make_shared<WhereGoal>(renameGoal(wg->condition, names));
        }
        case GoalKind::If: {
            auto ifGoal = std::static_pointer_cast<IfGoal>(goal);
            std::vector<std::shared_ptr<Goal>> thenBranch;
            thenBranch.reserve(ifGoal->thenBranch.size());
            for (const auto& branchGoal : ifGoal->thenBranch) thenBranch.push_back(renameGoal(branchGoal, names));
            std::vector<std::shared_ptr<Goal>> elseBranch;
            elseBranch.reserve(ifGoal->elseBranch.size());
            for (const auto& branchGoal : ifGoal->elseBranch) elseBranch.push_back(renameGoal(branchGoal, names));
            return std::make_shared<IfGoal>(
                renameGoal(ifGoal->condition, names),
                std::move(thenBranch),
                std::move(elseBranch));
        }
        case GoalKind::Return: {
            auto rg = std::static_pointer_cast<ReturnGoal>(goal);
            std::vector<Arg> fields;
            fields.reserve(rg->fields.size());
            for (const auto& field : rg->fields) fields.push_back(Arg{field.name, field.nameId, renameExpr(field.value, names)});
            return std::make_shared<ReturnGoal>(std::move(fields));
        }
        case GoalKind::Group: {
            auto gg = std::static_pointer_cast<GroupGoal>(goal);
            std::vector<std::shared_ptr<Goal>> goals;
            goals.reserve(gg->goals.size());
            for (const auto& groupedGoal : gg->goals) goals.push_back(renameGoal(groupedGoal, names));
            return std::make_shared<GroupGoal>(std::move(goals));
        }
        case GoalKind::Or: {
            auto og = std::static_pointer_cast<OrGoal>(goal);
            std::vector<std::vector<std::shared_ptr<Goal>>> branches;
            branches.reserve(og->branches.size());
            for (const auto& branch : og->branches) {
                std::vector<std::shared_ptr<Goal>> renamedBranch;
                renamedBranch.reserve(branch.size());
                for (const auto& branchGoal : branch) renamedBranch.push_back(renameGoal(branchGoal, names));
                branches.push_back(std::move(renamedBranch));
            }
            return std::make_shared<OrGoal>(std::move(branches));
        }
    }
    throw InterpreterError("Unknown goal while renaming");
}

std::shared_ptr<Expr> Interpreter::renameExpr(const std::shared_ptr<Expr>& expr, RenameMap& names) {
    if (!expr) return nullptr;
    if (!exprNeedsRename(expr)) return expr;
    switch (expr->kind()) {
        case ExprKind::String:
        case ExprKind::Number:
        case ExprKind::Bool:
        case ExprKind::Nil:
            return expr;
        default:
            break;
    }
    if (auto v = std::dynamic_pointer_cast<VarExpr>(expr)) {
        if (v->nameId == InternalSymbol::SystemResultId) return v->clone();
        if (globals_.count(v->nameId) > 0) return v->clone();
        const SymbolId id = renamedId(v->nameId, names);
        return std::make_shared<VarExpr>(symbolNameForId(id), id);
    }
    if (auto term = std::dynamic_pointer_cast<TermExpr>(expr)) {
        std::vector<Arg> args;
        args.reserve(term->args.size());
        for (const auto& arg : term->args) args.push_back(Arg{arg.name, arg.nameId, renameExpr(arg.value, names)});
        return std::make_shared<TermExpr>(term->name, std::move(args), term->builtinId);
    }
    if (auto array = std::dynamic_pointer_cast<ArrayExpr>(expr)) {
        std::vector<std::shared_ptr<Expr>> items;
        items.reserve(array->items.size());
        for (const auto& item : array->items) items.push_back(renameExpr(item, names));
        return std::make_shared<ArrayExpr>(std::move(items));
    }
    if (auto map = std::dynamic_pointer_cast<MapExpr>(expr)) {
        std::vector<MapEntry> entries;
        entries.reserve(map->entries.size());
        for (const auto& entry : map->entries) {
            entries.push_back(MapEntry{entry.key, renameExpr(entry.value, names)});
        }
        return std::make_shared<MapExpr>(std::move(entries));
    }
    if (auto access = std::dynamic_pointer_cast<AccessExpr>(expr)) {
        auto targetVar = std::dynamic_pointer_cast<VarExpr>(access->target);
        if (access->keyId == InternalSymbol::ResultId && targetVar && targetVar->nameId == InternalSymbol::SystemId) {
            return access->clone();
        }
        return std::make_shared<AccessExpr>(renameExpr(access->target, names), access->key);
    }
    if (auto lambda = std::dynamic_pointer_cast<LambdaExpr>(expr)) {
        const SymbolId variableId = renamedId(lambda->variableId, names);
        return std::make_shared<LambdaExpr>(
            renameExpr(lambda->source, names),
            symbolNameForId(variableId), variableId,
            renameExpr(lambda->body, names),
            lambda->op,
            lambda->right ? renameExpr(lambda->right, names) : nullptr);
    }
    if (auto op = std::dynamic_pointer_cast<OperatorExpression>(expr)) {
        std::shared_ptr<OperatorExpression> renamed;
        if (op->coreOperator != CoreOperator::Unknown) {
            renamed = op->captureCount() == 1
                ? std::make_shared<OperatorExpression>(op->coreOperator, renameExpr(op->capture(0), names))
                : std::make_shared<OperatorExpression>(
                      op->coreOperator,
                      renameExpr(op->capture(0), names),
                      renameExpr(op->capture(1), names));
        } else {
            std::vector<OperatorCapture> captures;
            captures.reserve(op->captureCount());
            for (size_t i = 0; i < op->captureCount(); ++i) {
                captures.emplace_back(std::string(op->captureName(i)), renameExpr(op->capture(i), names));
            }
            renamed = std::make_shared<OperatorExpression>(
                op->operatorId, op->patternId, std::move(captures), op->explicitlyGrouped);
        }
        renamed->operatorId = op->operatorId;
        renamed->patternId = op->patternId;
        renamed->module = op->module;
        return renamed;
    }
    return expr;
}

bool Interpreter::exprNeedsRename(const std::shared_ptr<Expr>& expr) const {
    if (!expr) return false;
    if (auto variable = std::dynamic_pointer_cast<VarExpr>(expr)) {
        return variable->nameId != InternalSymbol::SystemResultId && globals_.count(variable->nameId) == 0;
    }
    if (auto term = std::dynamic_pointer_cast<TermExpr>(expr)) {
        for (const auto& arg : term->args) {
            if (exprNeedsRename(arg.value)) return true;
        }
        return false;
    }
    if (auto array = std::dynamic_pointer_cast<ArrayExpr>(expr)) {
        for (const auto& item : array->items) {
            if (exprNeedsRename(item)) return true;
        }
        return false;
    }
    if (auto map = std::dynamic_pointer_cast<MapExpr>(expr)) {
        for (const auto& entry : map->entries) {
            if (exprNeedsRename(entry.value)) return true;
        }
        return false;
    }
    if (auto access = std::dynamic_pointer_cast<AccessExpr>(expr)) {
        return exprNeedsRename(access->target);
    }
    if (auto op = std::dynamic_pointer_cast<OperatorExpression>(expr)) {
        for (size_t i = 0; i < op->captureCount(); ++i) {
            if (exprNeedsRename(op->capture(i))) return true;
        }
        return false;
    }
    // Lambda parameter names are invocation-local even when their current
    // source/body happens not to mention another ordinary variable.
    if (std::dynamic_pointer_cast<LambdaExpr>(expr)) return true;
    return false;
}

bool Interpreter::isSameVariable(const std::shared_ptr<Expr>& a, const std::shared_ptr<Expr>& b) const {
    auto va = std::dynamic_pointer_cast<VarExpr>(a);
    auto vb = std::dynamic_pointer_cast<VarExpr>(b);
    return va && vb && va->nameId == vb->nameId;
}

bool Interpreter::isGroundLiteral(const std::shared_ptr<Expr>& expr) const {
    return static_cast<bool>(std::dynamic_pointer_cast<StringExpr>(expr)) ||
           static_cast<bool>(std::dynamic_pointer_cast<BoolExpr>(expr)) ||
           static_cast<bool>(std::dynamic_pointer_cast<NumberExpr>(expr)) ||
           static_cast<bool>(std::dynamic_pointer_cast<NilExpr>(expr));
}

bool Interpreter::isCacheableQuery(const std::vector<std::shared_ptr<Goal>>& goals) const {
    for (const auto& goal : goals) {
        if (goalMayHaveSideEffects(goal)) return false;
    }
    return true;
}

bool Interpreter::goalMayHaveSideEffects(const std::shared_ptr<Goal>& goal) const {
    if (!goal) return false;
    if (auto call = std::dynamic_pointer_cast<CallGoal>(goal)) {
        if (nativeDeclarationFor(call->call.name)) return true;
        if (call->call.builtinId != BuiltinId::Unknown && !isBuiltinPure(call->call.builtinId)) return true;
        for (const auto& arg : call->call.args) {
            if (exprMayHaveSideEffects(arg.value)) return true;
        }
        return false;
    }
    if (auto notGoal = std::dynamic_pointer_cast<NotGoal>(goal)) {
        for (const auto& arg : notGoal->call.args) {
            if (exprMayHaveSideEffects(arg.value)) return true;
        }
        return false;
    }
    if (auto assign = std::dynamic_pointer_cast<AssignGoal>(goal)) {
        return exprMayHaveSideEffects(assign->expr);
    }
    if (auto multi = std::dynamic_pointer_cast<MultiAssignGoal>(goal)) {
        return exprMayHaveSideEffects(multi->expr);
    }
    if (auto binary = std::dynamic_pointer_cast<BinaryGoal>(goal)) {
        return exprMayHaveSideEffects(binary->left) || exprMayHaveSideEffects(binary->right);
    }
    if (auto where = std::dynamic_pointer_cast<WhereGoal>(goal)) {
        return goalMayHaveSideEffects(where->condition);
    }
    if (auto ifGoal = std::dynamic_pointer_cast<IfGoal>(goal)) {
        if (goalMayHaveSideEffects(ifGoal->condition)) return true;
        for (const auto& nested : ifGoal->thenBranch) {
            if (goalMayHaveSideEffects(nested)) return true;
        }
        for (const auto& nested : ifGoal->elseBranch) {
            if (goalMayHaveSideEffects(nested)) return true;
        }
        return false;
    }
    if (auto ret = std::dynamic_pointer_cast<ReturnGoal>(goal)) {
        for (const auto& field : ret->fields) {
            if (exprMayHaveSideEffects(field.value)) return true;
        }
        return false;
    }
    if (auto group = std::dynamic_pointer_cast<GroupGoal>(goal)) {
        for (const auto& nested : group->goals) {
            if (goalMayHaveSideEffects(nested)) return true;
        }
        return false;
    }
    if (auto orGoal = std::dynamic_pointer_cast<OrGoal>(goal)) {
        for (const auto& branch : orGoal->branches) {
            for (const auto& nested : branch) {
                if (goalMayHaveSideEffects(nested)) return true;
            }
        }
    }
    return false;
}

bool Interpreter::exprMayHaveSideEffects(const std::shared_ptr<Expr>& expr) const {
    if (!expr) return false;
    if (auto term = std::dynamic_pointer_cast<TermExpr>(expr)) {
        if (nativeDeclarationFor(term->name)) return true;
        if (term->builtinId != BuiltinId::Unknown && !isBuiltinPure(term->builtinId)) return true;
        for (const auto& arg : term->args) {
            if (exprMayHaveSideEffects(arg.value)) return true;
        }
        return false;
    }
    if (auto array = std::dynamic_pointer_cast<ArrayExpr>(expr)) {
        for (const auto& item : array->items) {
            if (exprMayHaveSideEffects(item)) return true;
        }
        return false;
    }
    if (auto map = std::dynamic_pointer_cast<MapExpr>(expr)) {
        for (const auto& entry : map->entries) {
            if (exprMayHaveSideEffects(entry.value)) return true;
        }
        return false;
    }
    if (auto access = std::dynamic_pointer_cast<AccessExpr>(expr)) {
        return exprMayHaveSideEffects(access->target);
    }
    if (auto op = std::dynamic_pointer_cast<OperatorExpression>(expr)) {
        for (size_t i = 0; i < op->captureCount(); ++i) {
            if (exprMayHaveSideEffects(op->capture(i))) return true;
        }
        return false;
    }
    if (auto lambda = std::dynamic_pointer_cast<LambdaExpr>(expr)) {
        return exprMayHaveSideEffects(lambda->source) ||
               exprMayHaveSideEffects(lambda->body) ||
               exprMayHaveSideEffects(lambda->right);
    }
    return false;
}

bool Interpreter::isMethodClause(const ClauseStmt& clause) const {
    return clause.clauseKind == ClauseKind::Method ||
           clause.clauseKind == ClauseKind::NativeDeclaration;
}

bool Interpreter::methodMetadataCacheEligible(const ClauseStmt& clause) const {
    if (clause.isFact()) return false;
    if (clause.head.name == "main") return true;
    if (!clause.body.empty() || !clause.fallbackBranches.empty()) return true;
    return false;
}

Interpreter::MethodParamPlan Interpreter::makeMethodParamPlan(const Arg& param) const {
    MethodParamPlan plan;
    plan.localName = param.name;
    auto typeExpr = std::dynamic_pointer_cast<VarExpr>(param.value);
    plan.typedParam = typeExpr && isFelidaeTypeAnnotationName(typeExpr->name);
    if (typeExpr && !plan.typedParam && !typeExpr->name.empty() &&
        !isInternalGeneratedSymbolId(typeExpr->nameId)) {
        plan.localName = typeExpr->name;
    }
    if (plan.typedParam) {
        plan.typeName = typeExpr->name;
        plan.typeId = languageTypeIdForName(typeExpr->name);
        plan.builtinType = isFelidaeBuiltinTypeName(typeExpr->name);
    }
    return plan;
}

std::vector<Interpreter::MethodParamPlan> Interpreter::buildMethodParamPlan(const ClauseStmt& clause) const {
    std::vector<MethodParamPlan> params;
    params.reserve(clause.head.args.size());
    for (const auto& param : clause.head.args) params.push_back(makeMethodParamPlan(param));
    return params;
}

const std::vector<Interpreter::MethodParamPlan>* Interpreter::hotMethodParamPlan(
    const std::shared_ptr<ClauseStmt>& clause) {
    if (!methodMetadataCacheEligible(*clause)) return nullptr;
    auto& info = methodRuntimeCache_[clause.get()];
    info.cacheEligible = true;
    ++info.callCount;
    if (info.paramsPrepared) return &info.params;
    if (info.callCount < kHotMethodPrepareThreshold) return nullptr;
    info.params = buildMethodParamPlan(*clause);
    info.paramsPrepared = true;
    return &info.params;
}

Interpreter::FactMaterialization Interpreter::factToMap(const ClauseStmt& clause) {
    std::vector<MapEntry> entries;
    std::vector<MapEntry> temporalEntries;
    std::vector<MapEntry> inheritedFields;
    std::vector<std::uint64_t> parentFactIds;
    const auto parentNames = clause.parentNames.empty()
        ? std::vector<std::string>{clause.parentName}
        : clause.parentNames;
    const bool requirementSchema = std::any_of(
        parentNames.begin(), parentNames.end(), [&](const std::string& parent) {
            return parent == "OperatorRequirement" ||
                   memory_.isCompatibleType(parent, "OperatorRequirement");
        });
    std::set<std::string> childFields;
    for (const auto& arg : clause.head.args) childFields.insert(arg.name);
    for (const auto& parentType : parentNames) {
        if (parentType.empty()) continue;
        const auto parentIndexes = memory_.compatibleFactIndexes(parentType);
        const auto parent = std::find_if(parentIndexes.begin(), parentIndexes.end(), [&](size_t index) {
            const auto& fact = memory_.fact(index);
            return fact.type == parentType && fact.active;
        });
        if (parent == parentIndexes.end()) {
            if (parentType == "OperatorRequirement") continue;
            throw InterpreterError("Unknown parent fact/type '" + parentType + "'");
        }
        const auto parentValue = memory_.factValue(*parent);
        if (!parentValue) {
            throw InterpreterError("Cannot materialize parent fact/type '" + parentType + "'");
        }
        parentFactIds.push_back(memory_.fact(*parent).id);
        for (const auto& inherited : parentValue->entries) {
            if (inherited.key == internalSymbolString(InternalSymbolKind::Type) ||
                inherited.key == internalSymbolString(InternalSymbolKind::Parent)) {
                continue;
            }
            const auto existing = std::find_if(inheritedFields.begin(), inheritedFields.end(), [&](const MapEntry& entry) {
                return entry.keyId == inherited.keyId && entry.key == inherited.key;
            });
            if (existing == inheritedFields.end()) {
                inheritedFields.push_back(MapEntry{inherited.key, inherited.value->clone()});
            } else if (!exprEqualsLiteral(existing->value, inherited.value) &&
                       !childFields.count(inherited.key)) {
                throw InterpreterError(
                    "Ambiguous inherited field '" + inherited.key + "' for " + clause.head.name +
                    "; provide an explicit value in the child fact");
            }
        }
    }
    upsertEntry(entries, internalSymbolString(InternalSymbolKind::Type), std::make_shared<StringExpr>(clause.head.name));
    if (!parentNames.empty() && !parentNames.front().empty()) {
        upsertEntry(entries, internalSymbolString(InternalSymbolKind::Parent),
                    std::make_shared<StringExpr>(parentNames.front()));
    }
    Env env;
    for (const auto& arg : clause.head.args) {
        std::shared_ptr<Expr> value;
        if (!evalExprValue(arg.value, env, value)) {
            const auto declaredType = std::dynamic_pointer_cast<VarExpr>(arg.value);
            if (requirementSchema && declaredType &&
                isFelidaeTypeAnnotationName(declaredType->name)) {
                value = std::make_shared<StringExpr>(declaredType->name);
            } else {
            throw InterpreterError("Cannot evaluate fact field '" + arg.name + "' for " + clause.head.name);
            }
        }
        // Explicit child fields always override inherited values.  This also
        // supplies the required disambiguation for two parents that expose
        // the same field with different values.
        if (arg.name.rfind("fx:", 0) == 0) {
            upsertEntry(temporalEntries, arg.name, value->clone());
        } else {
            upsertEntry(entries, arg.name, value->clone());
        }
    }
    auto fact = std::make_shared<MapExpr>(std::move(entries));
    fact->factType = clause.head.name;
    auto temporal = temporalEntries.empty()
        ? std::shared_ptr<MapExpr>{}
        : std::make_shared<MapExpr>(std::move(temporalEntries));
    return FactMaterialization{std::move(fact), std::move(temporal), std::move(parentFactIds)};
}

std::vector<std::shared_ptr<Expr>> Interpreter::valuesForLambdaSource(const std::shared_ptr<Expr>& source,
                                                                      const Env& env) {
    auto var = std::dynamic_pointer_cast<VarExpr>(source);
    // A string source is an explicit dynamic fact-type selector.  It keeps
    // multi-model .fx databases usable through normal lambda queries even
    // when a model name is lowercase and therefore indistinguishable from a
    // local variable in source syntax.
    if (const auto typeName = std::dynamic_pointer_cast<StringExpr>(source)) {
        ensurePredicateLoaded(typeName->value);
        std::vector<std::shared_ptr<Expr>> values;
        for (size_t factIndex : memory_.currentFactIndexes(memory_.compatibleFactIndexes(typeName->value))) {
            if (const auto value = memory_.factValue(factIndex)) values.push_back(value);
        }
        return values;
    }
    if (var) {
        auto globalIt = globals_.find(var->name);
        if (globalIt != globals_.end()) {
            std::shared_ptr<Expr> value;
            if (!evalExprValue(globalIt->second, env, value)) return {};
            if (auto array = std::dynamic_pointer_cast<ArrayExpr>(value)) return array->items;
            return {value};
        }
        const SymbolId designationId = symbolIdForName(var->name);
        if (memory_.hasDesignation(designationId)) {
            std::vector<std::shared_ptr<Expr>> values;
            for (const size_t factIndex : memory_.currentFactIndexes(memory_.designationIndexes({designationId}))) {
                if (const auto value = memory_.factValue(factIndex)) values.push_back(value);
            }
            return values;
        }
    }
    if (var && var->isCapitalized) {
        ensurePredicateLoaded(var->name);
        std::vector<std::shared_ptr<Expr>> values;
        for (size_t factIndex : memory_.currentFactIndexes(memory_.compatibleFactIndexes(var->name))) {
            if (const auto value = memory_.factValue(factIndex)) values.push_back(value);
        }
        return values;
    }

    std::shared_ptr<Expr> value;
    if (!evalExprValue(source, env, value)) return {};
    if (auto array = std::dynamic_pointer_cast<ArrayExpr>(value)) return array->items;
    if (std::dynamic_pointer_cast<FactSelectionExpr>(value)) {
        return materializeFactSelection(value)->items;
    }
    return {value};
}

const Arg* Interpreter::findArgByNameOrIndex(const Call& call, const std::string& name, size_t index) const {
    if (!name.empty()) {
        const SymbolId nameId = symbolIdForName(name);
        for (const auto& arg : call.args) {
            if (arg.nameId == nameId) return &arg;
        }
    }
    if (index < call.args.size()) return &call.args[index];
    return nullptr;
}

Interpreter::ClauseList* Interpreter::findClauses(const std::string& name, SymbolId nameId) {
    if (cacheInvalidationDepth_ == 0) {
        auto cached = clauseLookupCache_.find(name);
        if (cached != clauseLookupCache_.end()) {
            ++dispatchCacheHits_;
            return cached->second;
        }
        ++dispatchCacheMisses_;
    }
    if (nameId == 0) nameId = symbolIdForName(name);
    auto found = clauses_->find(nameId);
    if (found == clauses_->end()) return nullptr;
    for (auto& bucket : found->second) {
        if (bucket.name == name) {
            auto* clauses = &bucket.clauses;
            if (cacheInvalidationDepth_ == 0) clauseLookupCache_[name] = clauses;
            return clauses;
        }
    }
    return nullptr;
}

const Interpreter::ClauseList* Interpreter::findClauses(const std::string& name, SymbolId nameId) const {
    if (cacheInvalidationDepth_ == 0) {
        auto cached = clauseLookupCache_.find(name);
        if (cached != clauseLookupCache_.end()) {
            ++dispatchCacheHits_;
            return cached->second;
        }
        ++dispatchCacheMisses_;
    }
    if (nameId == 0) nameId = symbolIdForName(name);
    auto found = clauses_->find(nameId);
    if (found == clauses_->end()) return nullptr;
    for (const auto& bucket : found->second) {
        if (bucket.name == name) {
            auto* clauses = const_cast<ClauseList*>(&bucket.clauses);
            if (cacheInvalidationDepth_ == 0) clauseLookupCache_[name] = clauses;
            return clauses;
        }
    }
    return nullptr;
}

Interpreter::ClauseList& Interpreter::getOrCreateClauseList(const std::string& name, SymbolId nameId) {
    if (nameId == 0) nameId = symbolIdForName(name);
    ensureClauseTableUnique();
    auto& buckets = (*clauses_)[nameId];
    for (auto& bucket : buckets) {
        if (bucket.name == name) return bucket.clauses;
    }
    buckets.push_back(ClauseBucket{name, {}});
    return buckets.back().clauses;
}

void Interpreter::removeClauseBucket(const std::string& name, SymbolId nameId) {
    if (nameId == 0) nameId = symbolIdForName(name);
    ensureClauseTableUnique();
    auto found = clauses_->find(nameId);
    if (found == clauses_->end()) return;
    auto& buckets = found->second;
    buckets.erase(
        std::remove_if(buckets.begin(), buckets.end(), [&](const ClauseBucket& bucket) {
            return bucket.name == name;
        }),
        buckets.end());
    if (buckets.empty()) clauses_->erase(found);
}

void Interpreter::ensureClauseTableUnique() {
    if (clauses_.use_count() != 1) {
        clauses_ = std::make_shared<ClauseTable>(*clauses_);
        // Lookup entries hold ClauseList pointers. Copy-on-write publication
        // changes their owning table, so no cached pointer may survive it.
        clauseLookupCache_.clear();
    }
}

std::string Interpreter::solveCacheKey(const std::vector<std::shared_ptr<Goal>>& goals,
                                       size_t maxSolutions) const {
    std::ostringstream out;
    // Immutable program and fact generations make cached answers valid only
    // for the state that produced them; unrelated registrations no longer
    // require clearing every cached query.
    out << programGeneration_ << '|' << memory_.generation() << '|' << maxSolutions << '|';
    for (const auto& goal : goals) {
        out << goal->debug() << ';';
    }
    return out.str();
}

std::size_t Interpreter::estimateCachedSolutionsBytes(
    const std::string& key,
    const std::vector<Solution>& solutions) const {
    std::size_t bytes = sizeof(SolveCacheEntry) + key.capacity();
    bytes += solutions.capacity() * sizeof(Solution);
    for (const auto& solution : solutions) {
        bytes += solution.env.size() * sizeof(Env::value_type);
    }
    return bytes;
}

void Interpreter::storeCachedSolutions(const std::string& key,
                                       const std::vector<Solution>& solutions) {
    constexpr std::size_t MaxCacheEntries = 128;
    constexpr std::size_t MaxCacheBytes = std::size_t{8} * 1024 * 1024;

    auto existing = solveCache_.find(key);
    if (existing != solveCache_.end()) {
        solveCacheBytes_ -= existing->second.estimatedBytes;
        solveCacheRecency_.erase(existing->second.recency);
        solveCache_.erase(existing);
    }

    const std::size_t estimatedBytes = estimateCachedSolutionsBytes(key, solutions);
    if (estimatedBytes > MaxCacheBytes) return;

    solveCacheRecency_.push_front(key);
    auto recency = solveCacheRecency_.begin();
    auto inserted = solveCache_.emplace(
        *recency,
        SolveCacheEntry{solutions, recency, estimatedBytes});
    if (!inserted.second) {
        solveCacheRecency_.pop_front();
        return;
    }
    solveCacheBytes_ += estimatedBytes;

    while (solveCache_.size() > MaxCacheEntries || solveCacheBytes_ > MaxCacheBytes) {
        const std::string& oldestKey = solveCacheRecency_.back();
        auto oldest = solveCache_.find(oldestKey);
        if (oldest != solveCache_.end()) {
            solveCacheBytes_ -= oldest->second.estimatedBytes;
            solveCache_.erase(oldest);
        }
        solveCacheRecency_.pop_back();
    }
}

void Interpreter::invalidateCaches() {
    ++programGeneration_;
}

void Interpreter::beginCacheInvalidationBatch() {
    ++cacheInvalidationDepth_;
}

void Interpreter::endCacheInvalidationBatch() {
    if (cacheInvalidationDepth_ == 0) return;
    --cacheInvalidationDepth_;
    if (cacheInvalidationDepth_ == 0) pendingCacheInvalidation_ = false;
}

void Interpreter::clearCachesNow() {
    memory_.invalidateCaches();
    solveCache_.clear();
    solveCacheRecency_.clear();
    solveCacheBytes_ = 0;
    methodRuntimeCache_.clear();
    clauseLookupCache_.clear();
    typeAncestryCache_.clear();
    typeAncestorDistanceCache_.clear();
    typeHierarchyDepthCache_.clear();
    ancestryCacheGeneration_ = 0;
    comparisonDispatchCache_.clear();
    tableCache_.clear();
}

bool Interpreter::ensurePredicateLoaded(const std::string& predicate) {
    const SymbolId predicateId = symbolIdForName(predicate);
    if (findClauses(predicate, predicateId)) return true;
    const size_t namespaceEnd = predicate.find(':');
    if (namespaceEnd != std::string::npos && namespaceEnd != 0) {
        const std::string moduleName = predicate.substr(0, namespaceEnd);
        if (packageDiscoveryAttempts_.insert(moduleName).second) {
            const fs::path baseDir = currentLoadingFile_.empty()
                ? fs::current_path()
                : currentLoadingFile_.parent_path();
            const fs::path packageFile =
                sourceRootFromBase(baseDir) / "core" / (moduleName + ".fx");
            if (fs::is_regular_file(packageFile)) loadProgramFile(packageFile);
        }
    }
    return findClauses(predicate, predicateId) != nullptr;
}

void Interpreter::loadProgramFile(const std::filesystem::path& file) {
    fs::path normalized = fs::absolute(file).lexically_normal();
    if (loadedFiles_.count(normalized)) return;
    const bool ownsTransaction = !moduleTransaction_;
    if (ownsTransaction) beginModuleTransaction();
    loadedFiles_.insert(normalized);
    ++moduleLoads_;

    fs::path baseDir = normalized.parent_path();
    fs::path previous = currentLoadingFile_;
    currentLoadingFile_ = normalized;
    try {
        const auto streamStarted = std::chrono::steady_clock::now();
        parseProgramFileStatements(normalized, [&](std::shared_ptr<Statement> statement) {
            if (statement->kind() == StatementKind::Import) {
                const auto import = std::static_pointer_cast<ImportStmt>(statement);
                for (const auto& path : import->paths) addImport(baseDir, path);
                return;
            }
            addStreamedStatement(std::move(statement));
        }, operators_);
        streamedModuleMicros_ += static_cast<std::size_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - streamStarted).count());
        currentLoadingFile_ = previous;
        if (ownsTransaction) commitModuleTransaction();
    } catch (...) {
        currentLoadingFile_ = previous;
        if (ownsTransaction) rollbackModuleTransaction();
        throw;
    }
}

std::shared_ptr<ArrayExpr> Interpreter::materializeFactSelection(
    const std::shared_ptr<Expr>& selection) {
    if (const auto lazy =
            std::dynamic_pointer_cast<FactSelectionExpr>(selection)) {
        const auto indexes = lazy->designationIds.empty()
            ? memory_.selectionIndexes(
                lazy->factType,
                lazy->field,
                lazy->equals && isGroundLiteral(lazy->equals)
                    ? lazy->equals : nullptr,
                lazy->snapshotGeneration)
            : memory_.designationIndexes(lazy->designationIds, lazy->snapshotGeneration);
        std::vector<std::shared_ptr<Expr>> rows;
        rows.reserve(indexes.size());
        const auto appendMatches = [&](const std::vector<size_t>& candidates,
                                       bool allowHistorical) {
        for (const auto index : candidates) {
            const auto& record = memory_.snapshotFact(lazy->snapshotGeneration, index);
            if ((!allowHistorical && !record.active) || (!lazy->factType.empty() &&
                !memory_.isCompatibleType(record.type, lazy->factType))) {
                continue;
            }
            if (!lazy->designationIds.empty() && !std::all_of(
                    lazy->designationIds.begin(), lazy->designationIds.end(),
                    [&](SymbolId designation) {
                        return std::find(record.designations.begin(), record.designations.end(), designation) !=
                            record.designations.end();
                    })) continue;
            const auto fact =
                memory_.factValue(index, lazy->snapshotGeneration);
            if (!fact) continue;
            if (!lazy->field.empty()) {
                const auto actual = findMapValue(fact, lazy->field);
                if (!actual || !lazy->equals || !exprContainsLiteral(actual, lazy->equals)) continue;
            }
            bool matches = true;
            for (const auto& filter : lazy->filters) {
                const auto actual = findMapValue(fact, filter.field);
                if (!actual || !filter.value) {
                    matches = false;
                    break;
                }
                if (filter.op == TokenId::EQUAL) {
                    matches = exprContainsLiteral(actual, filter.value);
                } else if (filter.op == TokenId::NOT_EQUAL) {
                    matches = !exprContainsLiteral(actual, filter.value);
                } else {
                    matches = compareResolved(actual, filter.op, filter.value);
                }
                if (!matches) break;
            }
            if (!matches) continue;
            ++factCandidates_;
            rows.push_back(fact);
        }
        };
        appendMatches(memory_.currentFactIndexes(indexes, lazy->snapshotGeneration), false);
        if (rows.empty()) {
            appendMatches(memory_.relevantPastFactIndexes(
                lazy->factType.empty() ? "Fact" : lazy->factType,
                lazy->factTypeId,
                lazy->snapshotGeneration), true);
        }
        return std::make_shared<ArrayExpr>(std::move(rows));
    }
    const auto kind = std::dynamic_pointer_cast<StringExpr>(
        findMapValue(selection, internalSymbolString(InternalSymbolKind::Type)));
    const auto selectedType = std::dynamic_pointer_cast<StringExpr>(findMapValue(selection, "fact_type"));
    if (!kind || kind->value != "FactSelection" || !selectedType) {
        throw InterpreterError("Expected a FactSelection");
    }
    std::uint64_t snapshotGeneration = 0;
    if (const auto snapshot = std::dynamic_pointer_cast<NumberExpr>(
            findMapValue(selection, "snapshot_generation"))) {
        if (snapshot->value < 0 || std::floor(snapshot->value) != snapshot->value) {
            throw InterpreterError("FactSelection has an invalid snapshot generation");
        }
        snapshotGeneration = static_cast<std::uint64_t>(snapshot->value);
    }
    std::string field;
    std::shared_ptr<Expr> equals;
    if (const auto fieldValue = std::dynamic_pointer_cast<StringExpr>(findMapValue(selection, "field"))) {
        field = fieldValue->value;
        equals = findMapValue(selection, "equals");
    }
    const auto indexes = memory_.selectionIndexes(
        selectedType->value,
        field,
        equals && isGroundLiteral(equals) ? equals : nullptr,
        snapshotGeneration);
    std::vector<std::shared_ptr<Expr>> rows;
    rows.reserve(indexes.size());
    for (const auto index : indexes) {
        const auto fact = memory_.factValue(index, snapshotGeneration);
        if (!fact) continue;
        if (!field.empty()) {
            const auto actual = findMapValue(fact, field);
            if (!actual || !equals || !exprContainsLiteral(actual, equals)) continue;
        }
        ++factCandidates_;
        rows.push_back(fact);
    }
    return std::make_shared<ArrayExpr>(std::move(rows));
}

std::size_t Interpreter::syncFactSource(const std::filesystem::path& file) {
    const fs::path normalized = fs::absolute(file).lexically_normal();
    if (!fs::exists(normalized) || !fs::is_regular_file(normalized)) {
        throw InterpreterError("db.sync cannot read fact source: " + normalized.string());
    }

    // Sync is intentionally restricted to fact-only files.  Reloading method
    // declarations or globals could duplicate executable definitions and
    // violate ordered immediate execution semantics.
    struct StagedFact {
        std::string type;
        std::string parentType;
        std::shared_ptr<MapExpr> value;
        std::shared_ptr<MapExpr> temporalMetadata;
        std::vector<std::uint64_t> parentFactIds;
        std::vector<SymbolId> designationIds;
    };
    std::vector<StagedFact> staged;
    parseProgramFileChunks(normalized, [&](Program&& program) {
        for (const auto& statement : program.statements) {
            if (statement->kind() != StatementKind::Clause ||
                !std::static_pointer_cast<ClauseStmt>(statement)->isFact()) {
                throw InterpreterError("db.sync accepts fact-only .fx sources: " + normalized.string());
            }
            const auto clause = std::static_pointer_cast<ClauseStmt>(statement);
            auto materialized = factToMap(*clause);
            staged.push_back(StagedFact{
                clause->head.name,
                clause->parentName,
                std::move(materialized.value),
                std::move(materialized.temporalMetadata),
                std::move(materialized.parentFactIds),
                clause->designationIds});
        }
    });

    // Match staged rows before mutation without inferring an application
    // schema. An unchanged source fact has a structural source identity. A
    // changed fact that lacks a runtime FactId is intentionally modelled as a
    // delete plus insert: guessing identity from names such as "id" or
    // "orderId" corrupts arbitrary user schemas.
    struct ExistingFact {
        std::uint64_t id = 0;
        std::uint64_t rowVersion = 1;
    };
    std::unordered_map<std::string, std::vector<ExistingFact>> existingByValue;
    const auto structuralSourceKey = [](const std::string& type,
                                        const std::shared_ptr<MapExpr>& value) {
        return type + "\x1f" + (value ? value->debug() : std::string{});
    };
    for (const size_t index : memory_.factIndexesFromOrigin(normalized)) {
        const auto& fact = memory_.fact(index);
        if (!fact.active) continue;
        const auto value = memory_.factValue(index);
        if (!value) continue;
        ExistingFact existing{fact.id, fact.rowVersion};
        existingByValue[structuralSourceKey(fact.type, value)].push_back(existing);
    }
    std::unordered_set<std::uint64_t> reusedIds;

    // Validate the complete replacement before publishing. FactMemory itself
    // is copy-on-write, so rollback restores the previous generation without
    // rebuilding unrelated relations.
    FactMemory previousMemory = memory_;
    try {
        beginCacheInvalidationBatch();
        memory_.removeOrigin(normalized);
        for (const auto& row : staged) {
            std::optional<ExistingFact> existing;
            const auto found = existingByValue.find(structuralSourceKey(row.type, row.value));
            if (found != existingByValue.end()) {
                for (const auto& candidate : found->second) {
                    if (!reusedIds.count(candidate.id)) {
                        existing = candidate;
                        break;
                    }
                }
            }
            if (existing && !reusedIds.insert(existing->id).second) {
                throw InterpreterError("db.sync source identity resolves to the same fact more than once");
            }
            memory_.addFact(
                row.type,
                row.parentType,
                row.value,
                normalized,
                existing ? std::optional<std::uint64_t>(existing->id) : std::nullopt,
                existing ? existing->rowVersion + 1 : 1,
                std::move(row.parentFactIds),
                std::move(row.designationIds),
                std::move(row.temporalMetadata));
            if (!row.parentType.empty()) {
                memory_.setParent(row.type, row.parentType, normalized);
            }
        }
        endCacheInvalidationBatch();
    } catch (...) {
        memory_ = std::move(previousMemory);
        clearCachesNow();
        endCacheInvalidationBatch();
        throw;
    }
    return memory_.factIndexesFromOrigin(normalized).size();
}

std::string Interpreter::runtimeMetricsJson() const {
    const FactMemoryStats factStats = memory_.stats();
    std::ostringstream out;
    out << "{"
        << "\"clauseAttempts\":" << clauseAttempts_ << ","
        << "\"unificationAttempts\":" << unificationAttempts_ << ","
        << "\"factCandidates\":" << factCandidates_ << ","
        << "\"relationshipCandidates\":" << relationshipCandidates_ << ","
        << "\"relationshipCandidatesPruned\":" << relationshipCandidatesPruned_ << ","
        << "\"solutionMaterializations\":" << solutionMaterializations_ << ","
        << "\"environmentFramesCreated\":" << envFramePool_.created() << ","
        << "\"environmentCopies\":" << environmentCopies_ << ","
        << "\"environmentFramesCached\":" << envFramePool_.cached() << ","
        << "\"standardizedClauses\":" << standardizedClauses_ << ","
        << "\"moduleLoads\":" << moduleLoads_ << ","
        << "\"nativeCalls\":" << nativeCalls_ << ","
        << "\"nativeFactProjectionCalls\":" << nativeFactProjectionCalls_ << ","
        << "\"nativeRequestBytes\":" << nativeRequestBytes_ << ","
        << "\"nativeFactProjectionBytes\":" << nativeFactProjectionBytes_ << ","
        << "\"nativeSerializationMicros\":" << nativeSerializationMicros_ << ","
        << "\"streamedModuleMicros\":" << streamedModuleMicros_ << ","
        << "\"parserTokensLexed\":" << parserMetrics_.tokensLexed << ","
        << "\"parserIterations\":" << parserMetrics_.iterations << ","
        << "\"parserPeakRecursionDepth\":" << parserMetrics_.peakRecursionDepth << ","
        << "\"parserBacktrackingAttempts\":" << parserMetrics_.backtrackingAttempts << ","
        << "\"parserOperatorCandidateLookups\":" << parserMetrics_.operatorCandidateLookups << ","
        << "\"parserOperatorCandidatesScored\":" << parserMetrics_.operatorCandidatesScored << ","
        << "\"factRegistrationMicros\":" << factRegistrationMicros_ << ","
        << "\"dispatchCacheHits\":" << dispatchCacheHits_ << ","
        << "\"dispatchCacheMisses\":" << dispatchCacheMisses_ << ","
        << "\"tableCacheHits\":" << tableCacheHits_ << ","
        << "\"tableCacheMisses\":" << tableCacheMisses_ << ","
        << "\"tableRounds\":" << tableRounds_ << ","
        << "\"tableDeltaAnswers\":" << tableDeltaAnswers_ << ","
        << "\"provenanceNodes\":" << provenanceNodes_ << ","
        << "\"factStoreGeneration\":" << factStats.generation << ","
        << "\"activeFacts\":" << factStats.activeFacts << ","
        << "\"tombstonedFacts\":" << factStats.tombstonedFacts << ","
        << "\"factRowVersions\":" << factStats.rowVersions << ","
        << "\"temporalLineages\":" << factStats.temporalLineages << ","
        << "\"temporalPastFacts\":" << factStats.temporalPastFacts << ","
        << "\"temporalFutureFacts\":" << factStats.temporalFutureFacts << ","
        << "\"factRelations\":" << factStats.relations << ","
        << "\"relationRows\":" << factStats.relationRows << ","
        << "\"relationColumnValues\":" << factStats.relationColumnValues << ","
        << "\"internedValues\":" << factStats.internedValues << ","
        << "\"adaptiveEqualityIndexes\":" << factStats.adaptiveEqualityIndexes << ","
        << "\"adaptiveIndexBuildMicros\":" << factStats.adaptiveIndexBuildMicros << ","
        << "\"liveFactSnapshots\":" << factStats.snapshots
        << "}";
    return out.str();
}

void Interpreter::recordStreamedModuleMicros(std::size_t micros) {
    streamedModuleMicros_ += micros;
}

void Interpreter::recordParserMetrics(const ParserMetrics& metrics) {
    parserMetrics_ += metrics;
}

std::vector<std::filesystem::path> Interpreter::expandImportPattern(const std::filesystem::path& baseDir,
                                                                    const std::string& pattern) const {
    std::vector<fs::path> files;
    if (isBareModuleImport(pattern)) {
        fs::path coreFile = resolveCoreImport(baseDir, pattern);
        if (fs::exists(coreFile) && fs::is_regular_file(coreFile)) {
            files.push_back(coreFile);
            return files;
        }
        fs::path nativeFile = resolveNativeImport(baseDir, pattern);
        if (!nativeFile.empty()) return files;
        std::ostringstream message;
        message << "Module '" << pattern << "' not found. Looked for "
                << coreFile.string() << " or native library files";
        for (const auto& fileName : nativeLibraryFileNames(pattern)) message << " " << fileName;
        throw InterpreterError(message.str());
    }

    fs::path raw(pattern);
    if (pattern.rfind("system.flibrary.", 0) == 0 && pattern.find('*') == std::string::npos) {
        std::string moduleName = pattern.substr(std::string("system.flibrary.").size());
        fs::path coreFile = fs::absolute(sourceRootFromBase(baseDir) / "core" / "system" / "flibrary" / (moduleName + ".fx")).lexically_normal();
        if (fs::exists(coreFile) && fs::is_regular_file(coreFile)) {
            files.push_back(coreFile);
            return files;
        }
    }
    fs::path target = raw.is_absolute() ? raw : (baseDir / raw);
    target = fs::absolute(target).lexically_normal();

    if (pattern.size() >= 2 && pattern.substr(pattern.size() - 2) == "/*") {
        fs::path dir = fs::absolute(baseDir / pattern.substr(0, pattern.size() - 2)).lexically_normal();
        if (!fs::exists(dir) || !fs::is_directory(dir)) {
            throw InterpreterError("Import directory not found: " + dir.string());
        }
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".fx") {
                files.push_back(entry.path());
            }
        }
        return files;
    }

    if (fs::exists(target) && fs::is_directory(target)) {
        for (const auto& entry : fs::recursive_directory_iterator(target)) {
            if (entry.is_regular_file() && entry.path().extension() == ".fx") {
                files.push_back(entry.path());
            }
        }
        return files;
    }

    if (!fs::exists(target)) {
        if (!raw.has_parent_path() && raw.extension() == ".fx") {
            fs::path coreFile = fs::absolute(sourceRootFromBase(baseDir) / "core" / raw.filename()).lexically_normal();
            if (fs::exists(coreFile) && fs::is_regular_file(coreFile)) {
                files.push_back(coreFile);
                return files;
            }
        }
        fs::path nativeFile = resolveNativeImport(baseDir, pattern);
        if (!nativeFile.empty()) return files;
        throw InterpreterError("Import file not found: " + target.string());
    }
    if (fs::is_regular_file(target) && hasNativeLibraryExtension(target)) {
        return files;
    }
    files.push_back(target);
    return files;
}

std::filesystem::path Interpreter::resolveNativeImport(const std::filesystem::path& baseDir,
                                                       const std::string& pattern) const {
    fs::path raw(pattern);
    std::vector<fs::path> candidates;

    auto addCandidate = [&](const fs::path& candidate) {
        candidates.push_back(fs::absolute(candidate).lexically_normal());
    };

    if (raw.has_extension() && hasNativeLibraryExtension(raw)) {
        addCandidate(raw.is_absolute() ? raw : (baseDir / raw));
    }

    fs::path root = sourceRootFromBase(baseDir);
    std::string moduleName = raw.stem().string();
    if (isBareModuleImport(pattern)) {
        moduleName = pattern;
    }
    if (pattern.rfind("system.flibrary.", 0) == 0) {
        moduleName = pattern.substr(std::string("system.flibrary.").size());
    }
    if (pattern.rfind("system:flibrary:", 0) == 0) {
        size_t moduleStart = std::string("system:flibrary:").size();
        size_t moduleEnd = pattern.find(':', moduleStart);
        moduleName = pattern.substr(moduleStart, moduleEnd == std::string::npos ? std::string::npos : moduleEnd - moduleStart);
    }
    if (!moduleName.empty() && moduleName.find('*') == std::string::npos) {
#if defined(NDEBUG)
        constexpr const char* nativeConfiguration = "Release";
#else
        constexpr const char* nativeConfiguration = "Debug";
#endif
        for (const auto& fileName : nativeLibraryFileNames(moduleName)) {
            addCandidate(baseDir / fileName);
            addCandidate(
                baseDir / "native_modules" / moduleName /
                nativeConfiguration / fileName);
            addCandidate(baseDir / "native_modules" / moduleName / fileName);
            addCandidate(
                root / "native_modules" / moduleName /
                nativeConfiguration / fileName);
            addCandidate(root / "native_modules" / moduleName / fileName);
            addCandidate(root / "modules" / moduleName / fileName);
        }
    }

    for (const auto& candidate : candidates) {
        if (fs::exists(candidate) && fs::is_regular_file(candidate)) return candidate;
    }

    return {};
}

} // namespace Felidae
