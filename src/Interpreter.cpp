#include "Interpreter.h"
#include "BuiltinRegistry.h"
#include "Lexer.h"
#include "FelidaeRuntime.h"
#include "Parser.h"
#include "OperatorAnnotation.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdlib>
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
            if (entry.keyId == keyId && entry.key == key) return entry.value;
        }
    }
    if (auto t = std::dynamic_pointer_cast<TermExpr>(expr)) {
        if (t->builtinId == BuiltinId::JsonObject) {
            for (const auto& field : t->args) {
                auto pair = termArgs(field.value, BuiltinId::FnPair);
                std::string fieldName;
                if (pair.size() == 2 && argAsString(pair[0], fieldName) &&
                    symbolIdForName(fieldName) == keyId && fieldName == key) {
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
                if (arg.name == "data") {
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
        if (entry.keyId == keyId && entry.key == key) {
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
        [&](const MapEntry& entry) { return entry.keyId == keyId && entry.key == key; }), entries.end());
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
    for (const auto& arg : term.args) {
        if (arg.name == name) return &arg;
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
                if (arg.name == "data") {
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
        std::ifstream input(registryFile);
        Lexer lexer(input);
        Parser parser(lexer);
        const Program registry = parser.parseProgram();
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
                invocation.args.push_back(Arg{argument.name, argument.value->clone()});
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
        const auto* pattern = parsed.pattern.empty()
            ? operators_->findPatternByOperator(parsed.operatorName)
            : operators_->findPattern(parsed.operatorName, parsed.pattern);
        if (!pattern) throw InterpreterError("Operator annotation pattern was not registered");
        if (!parsed.hasVisibility) {
            try {
                parsed.visibility = operators_->visibilityForPattern(
                    pattern->patternId, clause->module);
            } catch (const std::runtime_error& error) {
                throw InterpreterError(error.what());
            }
        }
        if (annotation.builtinId == BuiltinId::MixfixAnnotation &&
            !clause->head.args.empty()) {
            throw InterpreterError(
                "@mixfix implementations receive captures implicitly; remove method parameters");
        }
        if (annotation.builtinId != BuiltinId::MixfixAnnotation &&
            !clause->head.args.empty()) {
            if (clause->head.args.size() != parsed.captures.size()) {
                throw InterpreterError(
                    "Annotated mixfix method parameters must match pattern capture count");
            }
            for (std::size_t index = 0; index < parsed.captures.size(); ++index) {
                const auto& parameter = clause->head.args[index];
                const auto type = std::dynamic_pointer_cast<VarExpr>(parameter.value);
                if (parameter.name.empty() || !type ||
                    !isFelidaeTypeAnnotationName(type->name)) {
                    throw InterpreterError(
                        "Annotated mixfix parameters must be named and typed");
                }
                const auto& capture = parsed.captures[index];
                const bool sameBuiltin = capture.languageTypeId != LanguageTypeId::Unknown &&
                    languageTypeIdForName(type->name) == capture.languageTypeId;
                const bool sameFact = capture.languageTypeId == LanguageTypeId::Unknown &&
                    type->name == capture.type;
                if (!sameBuiltin && !sameFact) {
                    throw InterpreterError(
                        "Annotated mixfix parameter '" + parameter.name +
                        "' type must match capture '" + capture.name + "'");
                }
            }
        }
        if (parsed.effect != OperatorEffect::Pure) {
            throw InterpreterError("Custom operator and matcher methods must be pure");
        }
        std::unordered_set<const ClauseStmt*> purityWalk;
        std::string impurity;
        if (!isReferenceMethodPure(clause, purityWalk, impurity)) {
            throw InterpreterError("Annotated operator method '" + clauseName +
                                   "' is not transitively pure: " + impurity);
        }
        if (annotation.builtinId == BuiltinId::MatcherAnnotation) {
            const auto metadataExpression = [&](const auto& self,
                                                const std::shared_ptr<Expr>& expression) -> bool {
                if (!expression) return false;
                if (std::dynamic_pointer_cast<StringExpr>(expression) ||
                    std::dynamic_pointer_cast<NumberExpr>(expression) ||
                    std::dynamic_pointer_cast<BoolExpr>(expression) ||
                    std::dynamic_pointer_cast<NilExpr>(expression) ||
                    std::dynamic_pointer_cast<VarExpr>(expression)) return true;
                if (auto access = std::dynamic_pointer_cast<AccessExpr>(expression)) {
                    return self(self, access->target);
                }
                if (auto array = std::dynamic_pointer_cast<ArrayExpr>(expression)) {
                    return std::all_of(array->items.begin(), array->items.end(),
                        [&](const std::shared_ptr<Expr>& item) { return self(self, item); });
                }
                if (auto map = std::dynamic_pointer_cast<MapExpr>(expression)) {
                    return std::all_of(map->entries.begin(), map->entries.end(),
                        [&](const MapEntry& field) { return self(self, field.value); });
                }
                return false;
            };
            const ReturnGoal* returned = nullptr;
            for (const auto& goal : clause->body) {
                if (auto candidate = std::dynamic_pointer_cast<ReturnGoal>(goal)) {
                    if (returned) throw InterpreterError("@matcher must return exactly one RequirementMatch");
                    returned = candidate.get();
                    continue;
                }
                if (auto guard = std::dynamic_pointer_cast<WhereGoal>(goal)) {
                    const auto comparison = std::dynamic_pointer_cast<BinaryGoal>(guard->condition);
                    if (!comparison ||
                        (comparison->op != TokenType::EqEq && comparison->op != TokenType::NotEq &&
                         comparison->op != TokenType::LT && comparison->op != TokenType::LTE &&
                         comparison->op != TokenType::GT && comparison->op != TokenType::GTE) ||
                        !metadataExpression(metadataExpression, comparison->left) ||
                        !metadataExpression(metadataExpression, comparison->right)) {
                        throw InterpreterError(
                            "@matcher where guards may only compare immutable context or ExpressionRef metadata");
                    }
                    continue;
                } else {
                    throw InterpreterError(
                        "@matcher bodies may contain only static where guards and RequirementMatch return");
                }
            }
            if (!returned || returned->fields.size() != 1 || !returned->fields.front().name.empty()) {
                throw InterpreterError("@matcher must return RequirementMatch(...)");
            }
            const auto wrapper = std::dynamic_pointer_cast<TermExpr>(returned->fields.front().value);
            if (!wrapper || wrapper->name != "RequirementMatch") {
                throw InterpreterError("@matcher must return the RequirementMatch wrapper");
            }
            if (wrapper->args.size() != parsed.produces.size()) {
                throw InterpreterError("RequirementMatch fields must exactly match @matcher 'produces'");
            }
            for (const auto& produced : parsed.produces) {
                if (!memory_.isCompatibleType(produced.type, "OperatorRequirement")) {
                    throw InterpreterError("@matcher produced type '" + produced.type +
                                           "' must extend OperatorRequirement");
                }
                const Arg* output = nullptr;
                for (const auto& field : wrapper->args) {
                    if (field.nameId == produced.nameId && field.name == produced.name) {
                        if (output) throw InterpreterError(
                            "RequirementMatch repeats factor binding '" + produced.name + "'");
                        output = &field;
                    }
                }
                const auto prototype = output
                    ? std::dynamic_pointer_cast<TermExpr>(output->value)
                    : std::shared_ptr<TermExpr>{};
                if (!prototype || prototype->nameId != produced.typeId ||
                    prototype->name != produced.type) {
                    throw InterpreterError("RequirementMatch binding '" + produced.name +
                                           "' must construct " + produced.type);
                }
            }
            operators_->registerMatcher(makeOperatorMatcherDefinition(
                parsed, *pattern, clause->head.name, clause->head.nameId, clause->module));
            operatorClauses_[pattern->patternId].push_back(clause);
            continue;
        }
        operators_->registerOverload(makeOperatorOverloadDefinition(
            parsed, *pattern, clause->head.name, clause->head.nameId, clause->module));
        operatorClauses_[pattern->patternId].push_back(clause);
    }
    if (clause->isFact() && globals_.count(clause->head.name) == 0) {
        const auto registrationStarted = std::chrono::steady_clock::now();
        auto materialized = factToMap(*clause);
        memory_.addFact(clause->head.name, clause->parentName, materialized.value,
                        currentLoadingFile_, std::nullopt, 1,
                        std::move(materialized.parentFactIds), clause->designationIds);
        const auto& declaredParents = clause->parentNames.empty()
            ? std::vector<std::string>{clause->parentName}
            : clause->parentNames;
        for (const auto& parent : declaredParents) {
            if (!parent.empty()) memory_.setParent(clause->head.name, parent, currentLoadingFile_);
        }
        factRegistrationMicros_ += static_cast<std::size_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - registrationStarted).count());
        return;
    }
    // Fact publication advances its relation generation, not the program
    // generation. Only executable declarations invalidate dispatch/table
    // plans for their symbol.
    clauseLookupCache_.erase(clauseName);
    ++programGeneration_;
    ++symbolGenerations_[clause->head.nameId == 0
        ? symbolIdForName(clauseName)
        : clause->head.nameId];
    if (!currentLoadingFile_.empty()) {
        clauseOrigins_[clause.get()] = currentLoadingFile_;
    }
    getOrCreateClauseList(clauseName, clause->head.nameId).push_back(std::move(clause));
}

void Interpreter::addImport(const std::filesystem::path& baseDir, const std::string& pattern) {
    auto files = expandImportPattern(baseDir, pattern);
    for (const auto& file : files) loadProgramFile(file);
}

std::vector<Solution> Interpreter::solve(const std::vector<std::shared_ptr<Goal>>& queryGoals,
                                         size_t maxSolutions) {
    ++solveEpoch_;
    const bool cacheable = isCacheableQuery(queryGoals);
    const std::string cacheKey = cacheable ? solveCacheKey(queryGoals, maxSolutions) : std::string{};
    if (cacheable) {
        auto cached = solveCache_.find(cacheKey);
        if (cached != solveCache_.end()) {
            solveCacheRecency_.splice(
                solveCacheRecency_.begin(), solveCacheRecency_, cached->second.recency);
            return cached->second.solutions;
        }
    }

    std::vector<Solution> out;
    Env env;
    try {
        solveRecursive(queryGoals, std::move(env), out, maxSolutions, 0);
    } catch (...) {
        collectExecutionGarbage();
        throw;
    }
    collectExecutionGarbage();
    if (cacheable) storeCachedSolutions(cacheKey, out);
    return out;
}

bool Interpreter::hasMethod(const std::string& name) {
    auto clauses = findClauses(name, symbolIdForName(name));
    if (!clauses && ensurePredicateLoaded(name)) {
        clauses = findClauses(name, symbolIdForName(name));
    }
    if (!clauses) return false;
    for (const auto& clause : *clauses) {
        if (isMethodClause(*clause)) return true;
    }
    return false;
}

bool Interpreter::hasAutoEntryCall() const {
    return !autoEntryCalls_.empty();
}

bool Interpreter::hasGlobal(const std::string& name) const {
    return globals_.count(name) > 0;
}

std::shared_ptr<Expr> Interpreter::evaluateGlobal(const std::string& name) const {
    auto it = globals_.find(name);
    if (it == globals_.end()) throw InterpreterError("Unknown global '" + name + "'");
    return it->second->clone();
}

std::shared_ptr<Expr> Interpreter::evaluateExpressionText(const std::string& text) {
    Lexer lexer(text);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpressionText();
    std::shared_ptr<Expr> value;
    Env env;
    if (!evalExprValue(expr, env, value)) {
        throw InterpreterError("Expression did not evaluate to a value");
    }
    return value;
}

std::shared_ptr<Expr> Interpreter::callMain(const std::shared_ptr<Expr>& systemInput) {
    if (!hasMethod("main")) throw InterpreterError("No main() method found");
    std::vector<Solution> out;
    std::shared_ptr<ClauseStmt> mainClause;
    auto* mainClauses = findClauses("main", symbolIdForName("main"));
    if (!mainClauses) throw InterpreterError("No main() method found");
    for (auto clause = mainClauses->rbegin(); clause != mainClauses->rend(); ++clause) {
        if (isMethodClause(**clause)) {
            mainClause = *clause;
            break;
        }
    }
    if (!mainClause) throw InterpreterError("No main() method found");
    Call call("main", {});
    if (!mainClause->head.args.empty()) {
        call.args.push_back(Arg{"arguments", systemInput->clone()});
    }
    const bool previousStrictValueFailures = strictValueFailures_;
    strictValueFailures_ = true;
    try {
        solveMethodCall(call, mainClause, Env{}, out, 1, 0);
    } catch (...) {
        strictValueFailures_ = previousStrictValueFailures;
        throw;
    }
    strictValueFailures_ = previousStrictValueFailures;
    if (out.empty()) {
        throw InterpreterError("main() produced no result. A goal in main failed; add an explicit return or check method calls used as values.");
    }
    auto returned = findReturnValue(out.front().env);
    if (!returned) {
        throw InterpreterError("main() completed without a return value");
    }
    return returned->clone();
}

std::shared_ptr<Expr> Interpreter::callAutoEntry() {
    if (autoEntryCalls_.empty()) throw InterpreterError("No auto entry call found");
    if (autoEntryResults_.size() != autoEntryCalls_.size() || !autoEntryResults_.front()) {
        throw InterpreterError("Auto entry result was not committed at its statement boundary");
    }
    return autoEntryResults_.front()->clone();
}

std::shared_ptr<Expr> Interpreter::executeEntryCall(const Call& entryCall) {
    auto clauses = findClauses(entryCall.name, entryCall.nameId);
    if (!clauses) throw InterpreterError("Auto entry method '" + entryCall.name + "' not found");

    for (const auto& clause : *clauses) {
        if (!isMethodClause(*clause)) continue;
        std::vector<Solution> out;
        solveMethodCall(entryCall, clause, Env{}, out, 1, 0);
        if (out.empty()) continue;
        auto returned = findReturnValue(out.front().env);
        if (returned) return returned->clone();
        return std::make_shared<StringExpr>("true");
    }
    throw InterpreterError("Auto entry method '" + entryCall.name + "' produced no result");
}

std::string Interpreter::valueToString(const std::shared_ptr<Expr>& value) const {
    Env env;
    auto resolved = resolveExpr(value, env);
    std::shared_ptr<Expr> evaluated;
    if (const_cast<Interpreter*>(this)->evalExprValue(resolved, env, evaluated)) {
        return publicValueString(evaluated);
    }
    return publicValueString(resolved);
}

void Interpreter::solveRecursive(const std::vector<std::shared_ptr<Goal>>& goals,
                                 Env env,
                                 std::vector<Solution>& out,
                                 size_t maxSolutions,
                                 size_t depth) {
    try {
        {
            solveIterative(goals, std::move(env), out, maxSolutions, depth);
        }
        if (depth == 0) collectExecutionGarbage();
    } catch (...) {
        if (depth == 0) collectExecutionGarbage();
        throw;
    }
}

void Interpreter::solveIterative(const std::vector<std::shared_ptr<Goal>>& goals,
                                 Env env,
                                 std::vector<Solution>& out,
                                 size_t maxSolutions,
                                 size_t depth) {
    struct WorkFrame {
        std::shared_ptr<std::vector<std::shared_ptr<Goal>>> goals;
        size_t goalIndex = 0;
        Env env;
        size_t depth = 0;
    };
    auto root = std::make_shared<std::vector<std::shared_ptr<Goal>>>(goals);
    std::vector<WorkFrame> work;
    work.push_back(WorkFrame{std::move(root), 0, std::move(env), depth});

    auto combinedGoals = [](const std::vector<std::shared_ptr<Goal>>& prefix,
                            const std::vector<std::shared_ptr<Goal>>& suffix,
                            size_t suffixStart) {
        auto combined = std::make_shared<std::vector<std::shared_ptr<Goal>>>();
        combined->reserve(prefix.size() + suffix.size() - suffixStart);
        combined->insert(combined->end(), prefix.begin(), prefix.end());
        combined->insert(combined->end(), suffix.begin() + static_cast<std::ptrdiff_t>(suffixStart), suffix.end());
        return combined;
    };

    while (!work.empty() && out.size() < maxSolutions) {
        WorkFrame frame = std::move(work.back());
        work.pop_back();
        if (frame.depth > kMaxNativeGoalFrameDepth) {
            throw InterpreterError("Maximum recursion depth reached");
        }
        if (frame.goalIndex >= frame.goals->size()) {
            ++solutionMaterializations_;
            out.push_back(Solution{std::move(frame.env)});
            continue;
        }

        const auto& goal = (*frame.goals)[frame.goalIndex];
        const size_t nextGoalIndex = frame.goalIndex + 1;
        const auto continueFrame = [&](Env nextEnv) {
            work.push_back(WorkFrame{frame.goals, nextGoalIndex, std::move(nextEnv), frame.depth});
        };

        switch (goal->kind()) {
            case GoalKind::Assign: {
                const auto assign = std::static_pointer_cast<AssignGoal>(goal);
                if (solveAssignGoal(*assign, frame.env)) continueFrame(std::move(frame.env));
                continue;
            }
            case GoalKind::MultiAssign: {
                const auto assign = std::static_pointer_cast<MultiAssignGoal>(goal);
                if (solveMultiAssignGoal(*assign, frame.env)) continueFrame(std::move(frame.env));
                continue;
            }
            case GoalKind::Binary: {
                const auto binary = std::static_pointer_cast<BinaryGoal>(goal);
                if (solveBinaryGoal(*binary, frame.env)) continueFrame(std::move(frame.env));
                continue;
            }
            case GoalKind::Where: {
                const auto where = std::static_pointer_cast<WhereGoal>(goal);
                if (solveWhereGoal(*where, frame.env)) continueFrame(std::move(frame.env));
                continue;
            }
            case GoalKind::Return: {
                const auto returned = std::static_pointer_cast<ReturnGoal>(goal);
                if (solveReturnGoal(*returned, frame.env)) continueFrame(std::move(frame.env));
                continue;
            }
            case GoalKind::Not: {
                const auto negated = std::static_pointer_cast<NotGoal>(goal);
                if (solveNotGoal(*negated, frame.env, frame.depth + 1)) continueFrame(std::move(frame.env));
                continue;
            }
            case GoalKind::Group: {
                const auto group = std::static_pointer_cast<GroupGoal>(goal);
                work.push_back(WorkFrame{combinedGoals(group->goals, *frame.goals, nextGoalIndex),
                                         0, std::move(frame.env), frame.depth + 1});
                continue;
            }
            case GoalKind::Or: {
                const auto disjunction = std::static_pointer_cast<OrGoal>(goal);
                for (auto branch = disjunction->branches.rbegin(); branch != disjunction->branches.rend(); ++branch) {
                    // The first source-order branch is pushed last and is
                    // therefore processed first. It can consume the current
                    // environment; only sibling branches need isolation.
                    Env branchEnv = branch == (disjunction->branches.rend() - 1)
                        ? std::move(frame.env)
                        : copyExecutionEnvironment(frame.env);
                    work.push_back(WorkFrame{combinedGoals(*branch, *frame.goals, nextGoalIndex),
                                             0, std::move(branchEnv), frame.depth + 1});
                }
                continue;
            }
            case GoalKind::If: {
                const auto conditional = std::static_pointer_cast<IfGoal>(goal);
                if (auto binary = std::dynamic_pointer_cast<BinaryGoal>(conditional->condition)) {
                    BindingTrail trail;
                    const auto previousTrail = activeBindingTrail_;
                    activeBindingTrail_ = &trail;
                    const auto checkpoint = trail.checkpoint();
                    try {
                        const bool matched = solveBinaryGoal(*binary, frame.env);
                        if (!matched) trail.rollback(checkpoint);
                        activeBindingTrail_ = previousTrail;
                        const auto& branch = matched
                            ? conditional->thenBranch : conditional->elseBranch;
                        work.push_back(WorkFrame{
                            combinedGoals(branch, *frame.goals, nextGoalIndex),
                            0, std::move(frame.env), frame.depth + 1});
                        continue;
                    } catch (...) {
                        trail.rollback(checkpoint);
                        activeBindingTrail_ = previousTrail;
                        throw;
                    }
                }
                std::vector<Solution> conditionSolutions;
                solveRecursive({conditional->condition}, copyExecutionEnvironment(frame.env), conditionSolutions, 1, frame.depth + 1);
                const auto& branch = conditionSolutions.empty()
                    ? conditional->elseBranch : conditional->thenBranch;
                Env branchEnv = conditionSolutions.empty()
                    ? std::move(frame.env) : std::move(conditionSolutions.front().env);
                work.push_back(WorkFrame{combinedGoals(branch, *frame.goals, nextGoalIndex),
                                         0, std::move(branchEnv), frame.depth + 1});
                continue;
            }
            case GoalKind::Call:
                break;
        }

        const auto callGoal = std::static_pointer_cast<CallGoal>(goal);
        auto clauses = findClauses(callGoal->call.name, callGoal->call.nameId);
        if (!clauses && ensurePredicateLoaded(callGoal->call.name)) {
            clauses = findClauses(callGoal->call.name, callGoal->call.nameId);
        }
        std::vector<TableBinding> tableBindings;
        if (tableCallAnswers(
                callGoal->call,
                frame.env,
                tableBindings,
                nullptr,
                true)) {
            for (auto binding = tableBindings.rbegin();
                 binding != tableBindings.rend();
                 ++binding) {
                work.push_back(WorkFrame{
                    frame.goals,
                    nextGoalIndex,
                    std::move(binding->env),
                    frame.depth + 1});
            }
            continue;
        }
        std::vector<WorkFrame> continuations;
        const bool preferLocalClause = clauses && !nativeDeclarationFor(callGoal->call.name);
        if (!preferLocalClause) {
            // A tokenized builtin with no user clauses has exactly one possible
            // continuation. Move the current environment into that path rather
            // than cloning it; fact and user-method calls retain their normal
            // backtracking isolation below.
            const bool deterministicBuiltin =
                callGoal->call.builtinId != BuiltinId::Unknown && !clauses;
            Env builtinEnv = deterministicBuiltin
                ? std::move(frame.env)
                : copyExecutionEnvironment(frame.env);
            if (solveBuiltin(callGoal->call, builtinEnv)) {
                continuations.push_back(WorkFrame{frame.goals, nextGoalIndex,
                                                  std::move(builtinEnv), frame.depth + 1});
            }
        }
        if (clauses) {
            for (const auto& originalClause : *clauses) {
                ++clauseAttempts_;
                if (isMethodClause(*originalClause)) {
                    std::vector<Solution> methodSolutions;
                    solveMethodCall(callGoal->call, originalClause, copyExecutionEnvironment(frame.env),
                                    methodSolutions, maxSolutions - out.size(), frame.depth + 1);
                    for (auto& solution : methodSolutions) {
                        continuations.push_back(WorkFrame{frame.goals, nextGoalIndex,
                                                          std::move(solution.env), frame.depth + 1});
                    }
                    continue;
                }
                const auto clause = standardizeApart(originalClause);
                for (auto& nextEnv : unifyCallAlternatives(callGoal->call, clause->head, frame.env)) {
                    continuations.push_back(WorkFrame{
                        combinedGoals(clause->body, *frame.goals, nextGoalIndex),
                        0, std::move(nextEnv), frame.depth + 1});
                }
            }
        }
        // Prefer a grounded relation-local equality index before building a
        // complete compatible-type candidate vector. The latter is useful for
        // scans and inheritance, but allocating it first makes a cold exact
        // lookup pay O(relation size) work even when its index has one row.
        const std::vector<size_t>* factCandidates = nullptr;
        std::vector<const std::vector<size_t>*> indexedCandidates;
        std::vector<size_t> designationCandidates;
        if (!callGoal->call.designationIds.empty()) {
            designationCandidates = memory_.designationIndexes(callGoal->call.designationIds);
        }
        for (const auto& arg : callGoal->call.args) {
            if (arg.name.empty()) continue;
            std::shared_ptr<Expr> resolved;
            if (!evalExprValue(arg.value, frame.env, resolved) || !isGroundLiteral(resolved)) continue;
            const auto& indexed = memory_.propertyFactIndexes(callGoal->call.name, callGoal->call.nameId,
                                                                arg.name, arg.nameId, resolved);
            indexedCandidates.push_back(&indexed);
            if (!factCandidates || indexed.size() < factCandidates->size()) factCandidates = &indexed;
        }
        // A grounded multi-property fact call is a conjunction.  Selecting
        // only the smallest index still leaves candidates that another known
        // property already disproves.  Intersect the index plans before
        // unification so those branches never enter the recursive solver.
        std::vector<size_t> intersectedCandidates;
        if (factCandidates && indexedCandidates.size() > 1) {
            intersectedCandidates = *factCandidates;
            for (const auto* indexed : indexedCandidates) {
                if (indexed == factCandidates) continue;
                std::unordered_set<size_t> allowed(indexed->begin(), indexed->end());
                intersectedCandidates.erase(
                    std::remove_if(intersectedCandidates.begin(), intersectedCandidates.end(),
                        [&](size_t factIndex) { return !allowed.count(factIndex); }),
                    intersectedCandidates.end());
                if (intersectedCandidates.empty()) break;
            }
            factCandidates = &intersectedCandidates;
        }
        if (!callGoal->call.designationIds.empty()) {
            if (!factCandidates) {
                factCandidates = &designationCandidates;
            } else {
                std::unordered_set<size_t> allowed(
                    designationCandidates.begin(), designationCandidates.end());
                intersectedCandidates.assign(factCandidates->begin(), factCandidates->end());
                intersectedCandidates.erase(
                    std::remove_if(intersectedCandidates.begin(), intersectedCandidates.end(),
                        [&](size_t factIndex) { return !allowed.count(factIndex); }),
                    intersectedCandidates.end());
                factCandidates = &intersectedCandidates;
            }
        }
        if (!factCandidates) {
            factCandidates = &memory_.compatibleFactIndexes(callGoal->call.name, callGoal->call.nameId);
        }
        for (size_t factIndex : *factCandidates) {
            const auto& fact = memory_.fact(factIndex);
            if (!fact.active || !memory_.isCompatibleType(fact.type, callGoal->call.name)) continue;
            ++factCandidates_;
            Call factHead(callGoal->call.name, {});
            factHead.args = memory_.factArguments(factIndex);
            for (auto& nextEnv : unifyCallAlternatives(callGoal->call, factHead, frame.env)) {
                continuations.push_back(WorkFrame{frame.goals, nextGoalIndex,
                                                  std::move(nextEnv), frame.depth + 1});
            }
        }
        for (auto continuation = continuations.rbegin(); continuation != continuations.rend(); ++continuation) {
            work.push_back(std::move(*continuation));
        }
    }
}

bool Interpreter::solveNotGoal(const NotGoal& goal, Env& env, size_t depth) {
    // Negation-as-failure is safe only after every argument has been bound by
    // preceding positive goals.  This avoids an accidental "not exists" scan
    // and gives the construct deterministic Datalog-style semantics.
    for (const auto& argument : goal.call.args) {
        const auto resolved = resolveExpr(argument.value, env);
        if (std::dynamic_pointer_cast<VarExpr>(resolved)) {
            throw InterpreterError("Variables in 'not " + goal.call.name + "(...)' must be bound by preceding positive goals");
        }
    }

    auto clauses = findClauses(goal.call.name, goal.call.nameId);
    if (clauses) {
        for (const auto& clause : *clauses) {
            if (isMethodClause(*clause)) {
                throw InterpreterError("'not " + goal.call.name + "(...)' only permits pure relational predicates");
            }
        }
    } else {
        if (!memory_.hasActiveRelation(goal.call.name, goal.call.nameId)) {
            throw InterpreterError("Unknown relational predicate in negation: " + goal.call.name);
        }
    }

    // A negative dependency cycle is unstratified.  We reject it instead of
    // allowing recursive failure to become an arbitrary truth value.
    if (!activeNegatedPredicates_.insert(goal.call.name).second) {
        throw InterpreterError("Unstratified negative dependency cycle involving '" + goal.call.name + "'");
    }
    try {
        std::vector<Solution> matches;
        solveRecursive(std::vector<std::shared_ptr<Goal>>{
                           std::make_shared<CallGoal>(goal.call)},
                       copyExecutionEnvironment(env), matches, 1, depth + 1);
        activeNegatedPredicates_.erase(goal.call.name);
        return matches.empty();
    } catch (...) {
        activeNegatedPredicates_.erase(goal.call.name);
        throw;
    }
}

bool Interpreter::solveAssignGoal(const AssignGoal& goal, Env& env) {
    auto var = std::make_shared<VarExpr>(goal.name);
    if (globals_.count(goal.name)) {
        throw InterpreterError("Variable '" + goal.name + "' is already assigned and immutable");
    }
    auto existing = env.find(goal.name);
    if (existing != env.end() && !std::dynamic_pointer_cast<NilExpr>(resolveExpr(existing->second, env))) {
        throw InterpreterError("Variable '" + goal.name + "' is already assigned and immutable");
    }

    std::shared_ptr<Expr> value;
    if (!goal.expr || !evalExprValue(goal.expr, env, value)) {
        if (strictValueFailures_) {
            throw InterpreterError("Assignment '" + goal.debug() + "' did not produce a value");
        }
        return false;
    }
    return unifyExpr(var, value, env);
}

bool Interpreter::solveMultiAssignGoal(const MultiAssignGoal& goal, Env& env) {
    std::shared_ptr<Expr> value;
    if (!evalExprValue(goal.expr, env, value)) {
        if (strictValueFailures_) {
            throw InterpreterError("Assignment '" + goal.debug() + "' did not produce a value");
        }
        return false;
    }

    std::vector<std::shared_ptr<Expr>> items;
    if (auto tuple = std::dynamic_pointer_cast<TermExpr>(value)) {
        if (tuple->builtinId == BuiltinId::FnTuple) {
            for (const auto& arg : tuple->args) items.push_back(cloneExprOrNil(arg.value));
        }
    }
    if (items.empty()) exprAsArrayItems(value, items);
    if (items.size() != goal.targets.size()) {
        throw InterpreterError(
            "ProgrammingError: tuple assignment expected " +
            std::to_string(goal.targets.size()) + " value(s), got " + std::to_string(items.size()));
    }

    BindingTrail trail;
    const auto previousTrail = activeBindingTrail_;
    activeBindingTrail_ = &trail;
    const auto checkpoint = trail.checkpoint();
    try {
        for (size_t i = 0; i < goal.targets.size(); ++i) {
            const auto& target = goal.targets[i];
            if (globals_.count(target.name)) {
                throw InterpreterError("Variable '" + target.name + "' is already assigned and immutable");
            }
            auto existing = env.find(target.name);
            if (existing != env.end() &&
                !std::dynamic_pointer_cast<NilExpr>(resolveExpr(existing->second, env))) {
                throw InterpreterError("Variable '" + target.name + "' is already assigned and immutable");
            }
            if (!target.type.empty() && !valueMatchesBuiltinType(items[i], target.type)) {
                throw InterpreterError(
                    "ProgrammingError: tuple assignment target '" + target.name +
                    "' expects " + target.type + ", got " + exprToString(items[i], env));
            }
            if (!unifyExpr(std::make_shared<VarExpr>(target.name), items[i], env)) {
                trail.rollback(checkpoint);
                activeBindingTrail_ = previousTrail;
                return false;
            }
        }
        activeBindingTrail_ = previousTrail;
        return true;
    } catch (...) {
        trail.rollback(checkpoint);
        activeBindingTrail_ = previousTrail;
        throw;
    }
}

bool Interpreter::solveBinaryGoal(const BinaryGoal& goal, Env& env) {
    switch (goal.op) {
        case TokenType::EqEq: {
            std::shared_ptr<Expr> leftValue;
            std::shared_ptr<Expr> rightValue;
            if (evalExprValue(goal.left, env, leftValue) && evalExprValue(goal.right, env, rightValue)) {
                return unifyExpr(leftValue, rightValue, env);
            }
            return unifyExpr(goal.left, goal.right, env);
        }
        case TokenType::NotEq: {
            BindingTrail trail;
            const auto previousTrail = activeBindingTrail_;
            activeBindingTrail_ = &trail;
            const auto checkpoint = trail.checkpoint();
            std::shared_ptr<Expr> leftValue;
            std::shared_ptr<Expr> rightValue;
            try {
                bool result = false;
                if (evalExprValue(goal.left, env, leftValue) && evalExprValue(goal.right, env, rightValue)) {
                    result = !unifyExpr(leftValue, rightValue, env);
                } else {
                    result = !unifyExpr(goal.left, goal.right, env);
                }
                trail.rollback(checkpoint);
                activeBindingTrail_ = previousTrail;
                return result;
            } catch (...) {
                trail.rollback(checkpoint);
                activeBindingTrail_ = previousTrail;
                throw;
            }
        }
        default:
            break;
    }

    auto left = resolveExpr(goal.left, env);
    auto right = resolveExpr(goal.right, env);
    if (!isGroundLiteral(left) || !isGroundLiteral(right)) return false;
    return compareResolved(left, goal.op, right);
}

bool Interpreter::solveWhereGoal(const WhereGoal& goal, Env& env) {
    if (auto binary = std::dynamic_pointer_cast<BinaryGoal>(goal.condition)) {
        return solveBinaryGoal(*binary, env);
    }
    return false;
}

bool Interpreter::solveReturnGoal(const ReturnGoal& goal, Env& env) {
    if (goal.fields.size() == 1 && goal.fields.front().name.empty()) {
        if (valueCallTrampolineDepth_ > 0) {
            if (auto tailCall =
                    std::dynamic_pointer_cast<TermExpr>(goal.fields.front().value)) {
                // A user-method return can be executed by the iterative value
                // call frame. Builtins and native calls retain their normal
                // value contract: they may have output adaptation or ABI
                // result shaping that cannot be bypassed by a tail jump.
                if (tailCall->builtinId == BuiltinId::Unknown &&
                    !nativeDeclarationFor(tailCall->name) &&
                    findClauses(tailCall->name, tailCall->nameId)) {
                    TermExpr next(tailCall->name, {}, tailCall->builtinId);
                    next.nameId = tailCall->nameId;
                    next.args.reserve(tailCall->args.size());
                    for (const auto& argument : tailCall->args) {
                        next.args.push_back(
                            Arg{argument.name, argument.value->clone()});
                    }
                    throw TailCallSignal(std::move(next), env);
                }
            }
        }
        std::shared_ptr<Expr> value;
        if (!evalExprValue(goal.fields.front().value, env, value)) {
            if (strictValueFailures_) {
                throw InterpreterError("return value '" + goal.fields.front().value->debug() + "' did not evaluate");
            }
            return false;
        }
        env[internalSymbolString(InternalSymbolKind::Return)] = value;
        return true;
    }

    std::vector<MapEntry> entries;
    entries.reserve(goal.fields.size());
    for (const auto& field : goal.fields) {
        std::shared_ptr<Expr> value;
        if (!evalExprValue(field.value, env, value)) {
            if (strictValueFailures_) {
                throw InterpreterError("return field '" + field.name + ": " + field.value->debug() + "' did not evaluate");
            }
            return false;
        }
        entries.push_back(MapEntry{field.name, value});
    }
    env[internalSymbolString(InternalSymbolKind::Return)] = std::make_shared<MapExpr>(std::move(entries));
    return true;
}

bool Interpreter::bodyHasReturnGoal(const std::vector<std::shared_ptr<Goal>>& goals) const {
    for (const auto& goal : goals) {
        switch (goal->kind()) {
            case GoalKind::Return: {
                auto returned = std::static_pointer_cast<ReturnGoal>(goal);
                if (!returned->fields.empty()) return true;
                break;
            }
            case GoalKind::Group: {
                auto group = std::static_pointer_cast<GroupGoal>(goal);
                if (bodyHasReturnGoal(group->goals)) return true;
                break;
            }
            case GoalKind::Or: {
                auto orGoal = std::static_pointer_cast<OrGoal>(goal);
                for (const auto& branch : orGoal->branches) {
                    if (bodyHasReturnGoal(branch)) return true;
                }
                break;
            }
            case GoalKind::If: {
                auto ifGoal = std::static_pointer_cast<IfGoal>(goal);
                if (bodyHasReturnGoal(ifGoal->thenBranch) || bodyHasReturnGoal(ifGoal->elseBranch)) return true;
                break;
            }
            case GoalKind::Call:
            case GoalKind::Binary:
            case GoalKind::Assign:
            case GoalKind::MultiAssign:
            case GoalKind::Where:
            case GoalKind::Not:
                break;
        }
    }
    return false;
}

bool Interpreter::evaluateGoalTruth(const std::shared_ptr<Goal>& goal, Env& env) {
    std::vector<Solution> solutions;
    const bool previousValueCallMode = valueCallMode_;
    valueCallMode_ = false;
    try {
        solveRecursive({goal}, env, solutions, 1, 0);
    } catch (...) {
        valueCallMode_ = previousValueCallMode;
        throw;
    }
    valueCallMode_ = previousValueCallMode;
    if (solutions.empty()) return false;
    env = std::move(solutions.front().env);
    return true;
}

std::shared_ptr<Expr> Interpreter::evaluateGoalTruthTuple(const std::vector<std::shared_ptr<Goal>>& goals, Env env) {
    std::vector<Arg> values;
    values.reserve(goals.size());
    for (const auto& goal : goals) {
        if (std::dynamic_pointer_cast<ReturnGoal>(goal)) continue;
        const bool ok = evaluateGoalTruth(goal, env);
        values.push_back(Arg{"value", std::make_shared<BoolExpr>(ok)});
    }
    if (values.empty()) {
        values.push_back(Arg{"value", std::make_shared<BoolExpr>(true)});
    }
    return std::make_shared<TermExpr>("fn:tuple", std::move(values), BuiltinId::FnTuple);
}

std::shared_ptr<Expr> Interpreter::executeGoalTruthTuple(const std::vector<std::shared_ptr<Goal>>& goals, Env env, Env& outEnv) {
    std::vector<Arg> values;
    values.reserve(goals.size());
    for (const auto& goal : goals) {
        if (std::dynamic_pointer_cast<ReturnGoal>(goal)) continue;
        const bool ok = evaluateGoalTruth(goal, env);
        values.push_back(Arg{"value", std::make_shared<BoolExpr>(ok)});
    }
    if (values.empty()) {
        values.push_back(Arg{"value", std::make_shared<BoolExpr>(true)});
    }
    outEnv = std::move(env);
    return std::make_shared<TermExpr>("fn:tuple", std::move(values), BuiltinId::FnTuple);
}

bool Interpreter::solveMethodCall(const Call& call,
                                  const std::shared_ptr<ClauseStmt>& originalClause,
                                  Env env,
                                  std::vector<Solution>& out,
                                  size_t maxSolutions,
                                  size_t depth) {
    if (methodCallDepth_ >= kMaxNativeMethodCallDepth) {
        throw InterpreterError(
            "Maximum non-tail method recursion depth reached; use an explicit tail return");
    }
    CounterScope methodDepth(methodCallDepth_);
    if (originalClause->emptyDeclaration) return false;
    // Overloads are selected by their complete call shape.  Accepting an
    // otherwise compatible prefix silently discarded later named arguments,
    // which routed rich fact APIs through their short overloads.
    for (size_t callIndex = 0; callIndex < call.args.size(); ++callIndex) {
        const auto& callArg = call.args[callIndex];
        bool declared = false;
        for (size_t paramIndex = 0; paramIndex < originalClause->head.args.size(); ++paramIndex) {
            const auto& param = originalClause->head.args[paramIndex];
            if ((!callArg.name.empty() && callArg.name == param.name) ||
                (callArg.name.empty() && callIndex == paramIndex)) {
                declared = true;
                break;
            }
        }
        if (!declared) {
            // Extra unbound variables are the established predicate-output
            // contract. Concrete extras, however, would be silently ignored
            // and must not select this overload.
            const auto unresolved = std::dynamic_pointer_cast<VarExpr>(
                resolveExpr(callArg.value, env));
            if (!unresolved) return false;
        }
    }
    Env callerEnv = std::move(env);
    std::vector<Env> candidates{Env{}};
    const auto* hotParams = hotMethodParamPlan(originalClause);
    std::vector<MethodParamPlan> coldParams;
    if (!hotParams) coldParams = buildMethodParamPlan(*originalClause);
    const auto& paramPlans = hotParams ? *hotParams : coldParams;

    for (size_t paramIndex = 0; paramIndex < originalClause->head.args.size(); ++paramIndex) {
        const auto& param = originalClause->head.args[paramIndex];
        const auto& paramPlan = paramPlans[paramIndex];
        std::vector<Env> nextCandidates;
        const Arg* callArg = findArg(call, param, paramIndex);
        for (auto& candidate : candidates) {
            if (!callArg) {
                if (paramPlan.typedParam && bodyHasReturnGoal(originalClause->body)) continue;
                nextCandidates.push_back(std::move(candidate));
                continue;
            }
            Env attempt = std::move(candidate);
            if (paramPlan.typeId == LanguageTypeId::Expr ||
                paramPlan.typeId == LanguageTypeId::Stmt ||
                paramPlan.typeId == LanguageTypeId::Statements) {
                std::shared_ptr<AstValueExpr> astValue;
                auto alreadyBound = std::dynamic_pointer_cast<AstValueExpr>(
                    resolveExpr(callArg->value, callerEnv));
                if (alreadyBound) {
                    astValue = alreadyBound;
                } else if (paramPlan.typeId == LanguageTypeId::Expr) {
                    astValue = std::make_shared<AstValueExpr>(
                        AstValueKind::Expression,
                        std::vector<std::shared_ptr<AstNode>>{callArg->value},
                        astNodeKind(callArg->value));
                } else {
                    continue;
                }
                if (!valueMatchesBuiltinType(astValue, paramPlan.typeName)) continue;
                if (unifyExpr(std::make_shared<VarExpr>(paramPlan.localName), astValue, attempt)) {
                    nextCandidates.push_back(std::move(attempt));
                }
                continue;
            }
            auto unresolvedCallVar = std::dynamic_pointer_cast<VarExpr>(resolveExpr(callArg->value, callerEnv));
            if (unresolvedCallVar) {
                if (unresolvedCallVar->name == paramPlan.localName ||
                    unifyExpr(std::make_shared<VarExpr>(paramPlan.localName), callArg->value, attempt)) {
                    nextCandidates.push_back(std::move(attempt));
                }
                continue;
            }
            std::shared_ptr<Expr> value;
            if (!evalExprValue(callArg->value, callerEnv, value)) continue;
            if (!paramPlan.typedParam) {
                if (unifyExpr(std::make_shared<VarExpr>(paramPlan.localName), value, attempt)) {
                    nextCandidates.push_back(std::move(attempt));
                }
                continue;
            }
            if (paramPlan.builtinType) {
                if (!valueMatchesBuiltinType(value, paramPlan.typeName)) continue;
                if (unifyExpr(std::make_shared<VarExpr>(paramPlan.localName), value, attempt)) {
                    nextCandidates.push_back(std::move(attempt));
                }
                continue;
            }
            auto map = std::dynamic_pointer_cast<MapExpr>(value);
            std::string actualType;
            if (map) {
                auto typeValue = findMapValue(value, internalSymbolString(InternalSymbolKind::Type));
                auto typeString = std::dynamic_pointer_cast<StringExpr>(typeValue);
                if (typeString) actualType = typeString->value;
            }
            if (actualType.empty() || !memory_.isCompatibleType(actualType, paramPlan.typeName)) continue;
            if (unifyExpr(std::make_shared<VarExpr>(paramPlan.localName), value, attempt)) {
                nextCandidates.push_back(std::move(attempt));
            }
        }
        candidates = std::move(nextCandidates);
        if (candidates.empty()) return false;
    }

    auto appendReturnedSolution = [&](const Env& solutionEnv, const std::shared_ptr<Expr>& returned) -> bool {
        Env attempt = callerEnv;
        attempt[internalSymbolString(InternalSymbolKind::Return)] = returned;
        auto resolvedBinding = [&](const std::string& name) -> std::shared_ptr<Expr> {
            auto bound = solutionEnv.find(name);
            if (bound == solutionEnv.end()) return nullptr;
            return resolveExpr(bound->second, solutionEnv);
        };
        bool ok = true;
        size_t positionalOutputIndex = 0;
        for (size_t argIndex = 0; argIndex < call.args.size(); ++argIndex) {
            const auto& arg = call.args[argIndex];
            bool isInputArg = false;
            for (size_t paramIndex = 0; paramIndex < originalClause->head.args.size(); ++paramIndex) {
                const auto& param = originalClause->head.args[paramIndex];
                if ((!arg.name.empty() && param.name == arg.name) ||
                    (arg.name.empty() && argIndex == paramIndex)) {
                    auto unresolved = std::dynamic_pointer_cast<VarExpr>(resolveExpr(arg.value, callerEnv));
                    isInputArg = !unresolved;
                    break;
                }
            }
            if (isInputArg) continue;
            std::shared_ptr<Expr> returnedField;
            if (!arg.name.empty()) {
                returnedField = findMapValue(returned, arg.name);
                if (!returnedField) returnedField = findMapValue(returned, "value");
                if (!returnedField) returnedField = findMapValue(returned, "output");
                if (!returnedField) returnedField = findMapValue(returned, "");
                if (!returnedField) {
                    for (size_t paramIndex = 0; paramIndex < originalClause->head.args.size(); ++paramIndex) {
                        const auto& param = originalClause->head.args[paramIndex];
                        if (param.name != arg.name) continue;
                        returnedField = resolvedBinding(paramPlans[paramIndex].localName);
                        break;
                    }
                }
            } else if (auto returnedMap = std::dynamic_pointer_cast<MapExpr>(returned)) {
                if (positionalOutputIndex < returnedMap->entries.size()) {
                    returnedField = returnedMap->entries[positionalOutputIndex].value;
                }
                positionalOutputIndex++;
                if (!returnedField && argIndex < originalClause->head.args.size()) {
                    returnedField = resolvedBinding(paramPlans[argIndex].localName);
                }
            } else {
                if (argIndex < originalClause->head.args.size()) {
                    returnedField = resolvedBinding(paramPlans[argIndex].localName);
                }
            }
            if (!returnedField || !unifyExpr(arg.value, returnedField, attempt)) {
                ok = false;
                break;
            }
        }
        if (!ok) return false;
        out.push_back(Solution{std::move(attempt)});
        return out.size() >= maxSolutions;
    };

    if (!originalClause->fallbackBranches.empty()) {
        for (auto& candidate : candidates) {
            std::vector<Solution> preludeSolutions;
            if (originalClause->body.empty()) {
                preludeSolutions.push_back(Solution{std::move(candidate)});
            } else {
                solveRecursive(originalClause->body, std::move(candidate), preludeSolutions, 1, depth + 1);
            }
            for (auto& prelude : preludeSolutions) {
                for (const auto& branch : originalClause->fallbackBranches) {
                    std::vector<Solution> branchSolutions;
                    solveRecursive(branch, prelude.env, branchSolutions, 1, depth + 1);
                    bool branchReturned = false;
                    for (auto& solution : branchSolutions) {
                        auto returnedIt = solution.env.find(internalSymbolString(InternalSymbolKind::Return));
                        if (returnedIt == solution.env.end()) continue;
                        branchReturned = true;
                        auto returnedValue = returnedIt->second;
                        if (appendReturnedSolution(solution.env, returnedValue)) return true;
                        break;
                    }
                    if (branchReturned) break;
                }
            }
        }
        return true;
    }

    for (auto& candidate : candidates) {
        if (valueCallMode_ && originalClause->head.name != "main" && !bodyHasReturnGoal(originalClause->body)) {
            Env truthEnv;
            auto truthTuple = executeGoalTruthTuple(originalClause->body, candidate, truthEnv);
            truthEnv[internalSymbolString(InternalSymbolKind::Return)] = truthTuple;
            if (appendReturnedSolution(truthEnv, truthTuple)) return true;
            continue;
        }

        std::vector<Solution> nested;
        Env startingCandidate = candidate;
        solveRecursive(originalClause->body, std::move(candidate), nested, 1, depth + 1);
        if (nested.empty() && valueCallMode_ && originalClause->head.name != "main") {
            Env failedValueEnv = startingCandidate;
            failedValueEnv[internalSymbolString(InternalSymbolKind::Return)] = evaluateGoalTruthTuple(originalClause->body, startingCandidate);
            nested.push_back(Solution{std::move(failedValueEnv)});
        }
        for (auto& solution : nested) {
            auto returnedIt = solution.env.find(internalSymbolString(InternalSymbolKind::Return));
            if (returnedIt == solution.env.end()) {
                if (originalClause->head.name == "main") {
                    solution.env[internalSymbolString(InternalSymbolKind::Return)] = std::make_shared<MapExpr>(std::vector<MapEntry>{});
                } else {
                    solution.env[internalSymbolString(InternalSymbolKind::Return)] = evaluateGoalTruthTuple(originalClause->body, startingCandidate);
                }
                returnedIt = solution.env.find(internalSymbolString(InternalSymbolKind::Return));
            }
            auto returnedValue = returnedIt->second;
            if (appendReturnedSolution(solution.env, returnedValue)) return true;
        }
    }
    return true;
}

void Interpreter::refreshAncestryCaches() const {
    const std::uint64_t generation = memory_.hierarchyGeneration();
    if (ancestryCacheGeneration_ == generation) return;
    typeAncestryCache_.clear();
    typeAncestorDistanceCache_.clear();
    typeHierarchyDepthCache_.clear();
    ancestryCacheGeneration_ = generation;
}

const std::vector<std::string>& Interpreter::typeAncestry(const std::string& type) const {
    refreshAncestryCaches();
    const SymbolId typeId = symbolIdForName(type);
    const auto cached = typeAncestryCache_.find(typeId);
    if (cached != typeAncestryCache_.end()) return cached->second;
    std::vector<std::string> result;
    std::set<std::string> seen;
    std::vector<std::string> pending{type};
    for (size_t index = 0; index < pending.size(); ++index) {
        const std::string current = pending[index];
        if (!seen.insert(current).second) continue;
        result.push_back(current);
        for (const auto& parent : memory_.parentsOf(current)) pending.push_back(parent);
    }
    // Fact is the implicit base family. It may own source membership methods,
    // but it is deliberately excluded from target comparison dispatch so a
    // generic fallback cannot create a second interpretation path.
    if (type != "Fact") result.push_back("Fact");
    return typeAncestryCache_.emplace(typeId, std::move(result)).first->second;
}

const std::unordered_map<std::string, std::size_t>&
Interpreter::typeAncestorDistances(const std::string& type) const {
    refreshAncestryCaches();
    const SymbolId typeId = symbolIdForName(type);
    const auto cached = typeAncestorDistanceCache_.find(typeId);
    if (cached != typeAncestorDistanceCache_.end()) return cached->second;
    std::unordered_map<std::string, std::size_t> distances;
    std::deque<std::pair<std::string, std::size_t>> pending;
    pending.emplace_back(type, 0);
    while (!pending.empty()) {
        auto [current, distance] = std::move(pending.front());
        pending.pop_front();
        const auto existing = distances.find(current);
        if (existing != distances.end() && existing->second <= distance) continue;
        distances[current] = distance;
        const auto parents = memory_.parentsOf(current);
        if (parents.empty() && current != "Fact") {
            pending.emplace_back("Fact", distance + 1);
        } else {
            for (const auto& parent : parents) {
                pending.emplace_back(parent, distance + 1);
            }
        }
    }
    return typeAncestorDistanceCache_.emplace(typeId, std::move(distances))
        .first->second;
}

double Interpreter::typeHierarchyDepth(const std::string& type) const {
    refreshAncestryCaches();
    std::unordered_set<std::string> visiting;
    std::function<double(const std::string&)> resolve =
        [&](const std::string& current) -> double {
            if (current == "Fact") return 0.0;
            const SymbolId currentId = symbolIdForName(current);
            const auto cached = typeHierarchyDepthCache_.find(currentId);
            if (cached != typeHierarchyDepthCache_.end()) return cached->second;
            if (!visiting.insert(current).second) {
                throw InterpreterError(
                    "Inheritance cycle detected while comparing fact types");
            }
            double parentDepth = 0.0;
            for (const auto& parent : memory_.parentsOf(current)) {
                parentDepth = std::max(parentDepth, resolve(parent));
            }
            visiting.erase(current);
            const double depth = parentDepth + 1.0;
            typeHierarchyDepthCache_.emplace(currentId, depth);
            return depth;
        };
    return resolve(type);
}

bool Interpreter::invokeComparisonMethod(const std::shared_ptr<ClauseStmt>& clause,
                                         const Call& call,
                                         const Env& env,
                                         std::shared_ptr<Expr>& out) {
    std::vector<Solution> solutions;
    solveMethodCall(call, clause, env, solutions, 1, 0);
    if (solutions.empty()) return false;
    const auto returned = solutions.front().env.find(internalSymbolString(InternalSymbolKind::Return));
    if (returned == solutions.front().env.end()) return false;
    out = resolveExpr(returned->second, solutions.front().env)->clone();
    return true;
}

bool Interpreter::evalAncestorAnalysis(const Call& call,
                                        const Env& env,
                                        std::shared_ptr<Expr>& out) {
    auto argument = [&](std::initializer_list<const char*> names,
                        size_t positional) -> std::shared_ptr<Expr> {
        for (const char* name : names) {
            for (const auto& arg : call.args) {
                if (arg.name != name) continue;
                std::shared_ptr<Expr> value;
                if (evalExprValue(arg.value, env, value)) return value;
                return {};
            }
        }
        if (positional < call.args.size() && call.args[positional].name.empty()) {
            std::shared_ptr<Expr> value;
            if (evalExprValue(call.args[positional].value, env, value)) return value;
        }
        return {};
    };
    const auto left = argument({"left", "fact1", "source"}, 0);
    const auto right = argument({"right", "fact2", "target"}, 1);
    const auto factType = [&](const std::shared_ptr<Expr>& value,
                              const char* label) -> std::string {
        const auto map = std::dynamic_pointer_cast<MapExpr>(value);
        const auto type = std::dynamic_pointer_cast<StringExpr>(
            findMapValue(value, internalSymbolString(InternalSymbolKind::Type)));
        if (!map || !type || type->value.empty()) {
            throw InterpreterError(std::string(builtinName(call.builtinId)) +
                                   " requires typed fact values for '" + label + "'");
        }
        return type->value;
    };
    const std::string leftType = factType(left, "left");
    const std::string rightType = factType(right, "right");
    const auto& leftDistances = typeAncestorDistances(leftType);
    const auto& rightDistances = typeAncestorDistances(rightType);

    struct Candidate {
        std::string type;
        std::size_t leftDistance = 0;
        std::size_t rightDistance = 0;
        double depth = 0.0;
    };
    std::vector<Candidate> candidates;
    for (const auto& [type, leftDistance] : leftDistances) {
        if (type == "Fact") continue;
        const auto rightDistance = rightDistances.find(type);
        if (rightDistance == rightDistances.end()) continue;
        candidates.push_back(Candidate{
            type, leftDistance, rightDistance->second, typeHierarchyDepth(type)});
    }
    std::sort(candidates.begin(), candidates.end(),
        [](const Candidate& first, const Candidate& second) {
            if (first.depth != second.depth) return first.depth > second.depth;
            const auto firstDistance = first.leftDistance + first.rightDistance;
            const auto secondDistance = second.leftDistance + second.rightDistance;
            if (firstDistance != secondDistance) return firstDistance < secondDistance;
            return first.type < second.type;
        });

    // In a multiple-inheritance DAG an LCA can be a set.  "lowest" means no
    // other common candidate is a descendant of it; "highest" is the dual.
    // This keeps tied, incomparable ancestors visible to Felidae code.
    std::vector<const Candidate*> lowest;
    std::vector<const Candidate*> highest;
    for (const auto& candidate : candidates) {
        bool hasMoreSpecific = false;
        bool hasMoreGeneral = false;
        for (const auto& other : candidates) {
            if (candidate.type == other.type) continue;
            hasMoreSpecific = hasMoreSpecific ||
                memory_.isCompatibleType(other.type, candidate.type);
            hasMoreGeneral = hasMoreGeneral ||
                memory_.isCompatibleType(candidate.type, other.type);
        }
        if (!hasMoreSpecific) lowest.push_back(&candidate);
        if (!hasMoreGeneral) highest.push_back(&candidate);
    }
    auto evidenceFor = [](const Candidate& candidate) {
        auto evidence = std::make_shared<MapExpr>(std::vector<MapEntry>{
            {internalSymbolString(InternalSymbolKind::Type),
                std::make_shared<StringExpr>("AncestorEvidence")},
            {"ancestor", std::make_shared<StringExpr>(candidate.type)},
            {"left_distance", std::make_shared<NumberExpr>(
                static_cast<double>(candidate.leftDistance))},
            {"right_distance", std::make_shared<NumberExpr>(
                static_cast<double>(candidate.rightDistance))},
            {"depth", std::make_shared<NumberExpr>(candidate.depth)}});
        evidence->factType = "AncestorEvidence";
        return evidence;
    };
    auto evidenceArray = [&](const std::vector<const Candidate*>& selected) {
        std::vector<std::shared_ptr<Expr>> values;
        values.reserve(selected.size());
        for (const auto* candidate : selected) values.push_back(evidenceFor(*candidate));
        return std::make_shared<ArrayExpr>(std::move(values));
    };
    std::vector<const Candidate*> all;
    all.reserve(candidates.size());
    for (const auto& candidate : candidates) all.push_back(&candidate);

    const std::vector<const Candidate*>* requested = &all;
    std::string operation = "common";
    if (call.builtinId == BuiltinId::LowestCommonAncestor) {
        operation = "lowest";
        requested = &lowest;
    } else if (call.builtinId == BuiltinId::HighestCommonAncestor) {
        operation = "highest";
        requested = &highest;
    }
    std::shared_ptr<Expr> selected = std::make_shared<NilExpr>();
    if (requested->size() == 1) {
        selected = std::make_shared<StringExpr>(requested->front()->type);
    }
    std::string status = requested->empty() ? "none" :
        (requested->size() == 1 ? "unique" : "ambiguous");
    auto result = std::make_shared<MapExpr>(std::vector<MapEntry>{
        {internalSymbolString(InternalSymbolKind::Type),
            std::make_shared<StringExpr>("AncestorAnalysis")},
        {"operation", std::make_shared<StringExpr>(operation)},
        {"left", left->clone()},
        {"right", right->clone()},
        {"common", evidenceArray(all)},
        {"lowest", evidenceArray(lowest)},
        {"highest", evidenceArray(highest)},
        {"ancestors", evidenceArray(*requested)},
        {"selected", selected},
        {"status", std::make_shared<StringExpr>(status)},
        {"hierarchy_generation", std::make_shared<NumberExpr>(
            static_cast<double>(memory_.hierarchyGeneration()))}});
    result->factType = "AncestorAnalysis";
    out = std::move(result);
    return true;
}

bool Interpreter::evalFactPropagation(const Call& call,
                                      const Env& env,
                                      std::shared_ptr<Expr>& out) {
    auto argument = [&](std::initializer_list<const char*> names,
                        size_t positional) -> std::shared_ptr<Expr> {
        for (const char* name : names) {
            for (const auto& arg : call.args) {
                if (arg.name != name) continue;
                std::shared_ptr<Expr> value;
                if (evalExprValue(arg.value, env, value)) return value;
                return {};
            }
        }
        if (positional < call.args.size() && call.args[positional].name.empty()) {
            std::shared_ptr<Expr> value;
            if (evalExprValue(call.args[positional].value, env, value)) return value;
        }
        return {};
    };
    const auto parent = argument({"parent", "source"}, 0);
    const auto child = argument({"child", "target"}, 1);
    const auto changes = argument({"changes", "patch"}, 2);
    const auto parentMap = std::dynamic_pointer_cast<MapExpr>(parent);
    const auto childMap = std::dynamic_pointer_cast<MapExpr>(child);
    const auto changesMap = std::dynamic_pointer_cast<MapExpr>(changes);
    const auto parentType = std::dynamic_pointer_cast<StringExpr>(
        findMapValue(parent, internalSymbolString(InternalSymbolKind::Type)));
    const auto childType = std::dynamic_pointer_cast<StringExpr>(
        findMapValue(child, internalSymbolString(InternalSymbolKind::Type)));
    if (!parentMap || !childMap || !changesMap || !parentType || !childType) {
        throw InterpreterError(
            "propagateFact requires typed 'parent' and 'child' facts plus a changes map");
    }
    if (!memory_.isCompatibleType(childType->value, parentType->value)) {
        throw InterpreterError("propagateFact parent must be an ancestor of the child fact type");
    }

    std::vector<MapEntry> derivedEntries = cloneEntries(childMap->entries);
    std::vector<std::shared_ptr<Expr>> propagated;
    std::vector<std::shared_ptr<Expr>> overridden;
    for (const auto& change : changesMap->entries) {
        if (change.keyId == InternalSymbol::TypeId ||
            change.keyId == InternalSymbol::ParentId) {
            throw InterpreterError("propagateFact cannot change internal fact identity fields");
        }
        const auto parentValue = findMapValue(parent, change.key);
        if (!parentValue) {
            throw InterpreterError("propagateFact changes must target a property supplied by the parent fact: '" +
                                   change.key + "'");
        }
        const auto childValue = findMapValue(child, change.key);
        const bool inherited = childValue && exprEqualsLiteral(childValue, parentValue);
        auto evidence = std::make_shared<MapExpr>(std::vector<MapEntry>{
            {internalSymbolString(InternalSymbolKind::Type),
                std::make_shared<StringExpr>("PropagationEvidence")},
            {"property", std::make_shared<StringExpr>(change.key)},
            {"parent_value", parentValue->clone()},
            {"child_value", cloneExprOrNil(childValue)},
            {"change", change.value->clone()},
            {"status", std::make_shared<StringExpr>(
                inherited ? "propagated" : "overridden")}});
        evidence->factType = "PropagationEvidence";
        if (inherited) {
            upsertEntry(derivedEntries, change.key, change.value->clone());
            propagated.push_back(std::move(evidence));
        } else {
            overridden.push_back(std::move(evidence));
        }
    }
    auto derived = std::make_shared<MapExpr>(std::move(derivedEntries));
    derived->factType = childMap->factType.empty() ? childType->value : childMap->factType;
    const bool complete = overridden.empty();
    auto result = std::make_shared<MapExpr>(std::vector<MapEntry>{
        {internalSymbolString(InternalSymbolKind::Type),
            std::make_shared<StringExpr>("FactPropagation")},
        {"state", std::make_shared<StringExpr>(
            complete ? "propagated" : "partially_overridden")},
        {"parent", parent->clone()},
        {"child", child->clone()},
        {"derived", derived},
        {"propagated", std::make_shared<ArrayExpr>(std::move(propagated))},
        {"overridden", std::make_shared<ArrayExpr>(std::move(overridden))}});
    result->factType = "FactPropagation";
    out = std::move(result);
    return true;
}

bool Interpreter::solveFactAttachment(const Call& call, Env& env) {
    const size_t separator = call.name.rfind(':');
    if (separator == std::string::npos) return false;
    const std::string receiverName = call.name.substr(0, separator);
    const std::string operation = call.name.substr(separator + 1);
    if (receiverName.empty() ||
        (operation != "depends" && operation != "relate" && operation != "references")) {
        return false;
    }

    const auto receiver = env.find(receiverName);
    if (receiver == env.end()) {
        throw InterpreterError("'" + receiverName + "." + operation +
                               "' requires a stored fact with stable logical identity");
    }
    const auto source = resolveExpr(receiver->second, env);
    const auto sourceId = memory_.logicalFactId(source);
    if (!sourceId) {
        throw InterpreterError("'" + receiverName + "." + operation + "' requires a stored fact with stable logical identity");
    }
    auto named = [&](const std::string& name) -> const Arg* {
        for (const auto& arg : call.args) if (arg.name == name) return &arg;
        return nullptr;
    };
    if (operation == "references") {
        return attachFactReference(call, env, *sourceId);
    }
    if (operation == "depends") {
        const Arg* on = named("on");
        if (!on) throw InterpreterError(".depends requires an 'on' fact");
        std::shared_ptr<Expr> required;
        if (!evalExprValue(on->value, env, required)) return false;
        const auto requiredMap = std::dynamic_pointer_cast<MapExpr>(required);
        if (!requiredMap || !findMapValue(required, internalSymbolString(InternalSymbolKind::Type))) {
            throw InterpreterError(".depends 'on' must be a fact value");
        }
        memory_.addDependency(*sourceId, std::static_pointer_cast<MapExpr>(requiredMap->clone()));
    } else {
        const Arg* to = named("to");
        const Arg* as = named("as");
        if (!to || !as) throw InterpreterError(".relate requires 'to' and 'as' arguments");
        std::shared_ptr<Expr> target;
        std::shared_ptr<Expr> relationship;
        if (!evalExprValue(to->value, env, target) || !evalExprValue(as->value, env, relationship)) return false;
        const auto targetId = memory_.logicalFactId(target);
        const auto relationMap = std::dynamic_pointer_cast<MapExpr>(relationship);
        const auto relationshipType = std::dynamic_pointer_cast<StringExpr>(
            findMapValue(relationship, internalSymbolString(InternalSymbolKind::Type)));
        if (!targetId || !relationMap || !relationshipType || relationshipType->value != "Relationship") {
            throw InterpreterError(".relate requires a stored target fact and Relationship(...) metadata");
        }
        std::shared_ptr<Expr> degree = std::make_shared<NilExpr>();
        std::shared_ptr<Expr> confidence = std::make_shared<NilExpr>();
        if (const Arg* argument = named("degree")) {
            if (!evalExprValue(argument->value, env, degree)) return false;
            if (!std::dynamic_pointer_cast<NumberExpr>(degree)) {
                throw InterpreterError(".relate degree must be numeric when supplied");
            }
        }
        if (const Arg* argument = named("confidence")) {
            if (!evalExprValue(argument->value, env, confidence)) return false;
            if (!std::dynamic_pointer_cast<NumberExpr>(confidence)) {
                throw InterpreterError(".relate confidence must be numeric when supplied");
            }
        }
        memory_.addRelationship(*sourceId,
                                *targetId,
                                std::static_pointer_cast<MapExpr>(relationMap->clone()),
                                degree,
                                confidence);
    }
    env[internalSymbolString(InternalSymbolKind::Return)] = std::make_shared<StringExpr>("ok");
    return true;
}

bool Interpreter::referenceValueMatchesType(const std::shared_ptr<Expr>& value,
                                            const MethodParamPlan& parameter) const {
    if (!parameter.typedParam || parameter.typeName == "any") return true;
    if (parameter.builtinType) return valueMatchesBuiltinType(value, parameter.typeName);
    const auto typeValue = std::dynamic_pointer_cast<StringExpr>(
        findMapValue(value, internalSymbolString(InternalSymbolKind::Type)));
    return typeValue && memory_.isCompatibleType(typeValue->value, parameter.typeName);
}

std::shared_ptr<ClauseStmt> Interpreter::resolveReferenceCallable(
    const std::shared_ptr<Expr>& callable,
    const std::shared_ptr<Expr>& source,
    const std::shared_ptr<Expr>& factor,
    std::string& normalizedName) {
    const auto reference = std::dynamic_pointer_cast<VarExpr>(callable);
    if (!reference) {
        throw InterpreterError(".references 'by' must be a callable reference such as Physics::velocity");
    }
    normalizedName = reference->name;
    const size_t delimiter = normalizedName.find("::");
    if (delimiter == std::string::npos || delimiter == 0 ||
        delimiter + 2 >= normalizedName.size() || normalizedName.find("::", delimiter + 2) != std::string::npos) {
        throw InterpreterError(".references 'by' must be a two-part callable reference such as Physics::velocity");
    }
    normalizedName.replace(delimiter, 2, ":");
    const auto clauses = findClauses(normalizedName, symbolIdForName(normalizedName));
    if (!clauses) {
        throw InterpreterError("Unknown referenced callable " + reference->name + ".");
    }

    std::shared_ptr<ClauseStmt> selected;
    for (const auto& clause : *clauses) {
        if (!isMethodClause(*clause) || clause->head.args.size() != 2) continue;
        const MethodParamPlan input = makeMethodParamPlan(clause->head.args[0]);
        const MethodParamPlan factorParam = makeMethodParamPlan(clause->head.args[1]);
        if (input.localName != "input" || factorParam.localName != "factor" ||
            !input.typedParam || !factorParam.typedParam ||
            !referenceValueMatchesType(source, input) ||
            !referenceValueMatchesType(factor, factorParam)) {
            continue;
        }
        if (selected) {
            throw InterpreterError("Referenced callable " + reference->name +
                                   " is ambiguous for the source and factor types.");
        }
        selected = clause;
    }
    if (!selected) {
        throw InterpreterError("Referenced callable " + reference->name +
                               " must accept compatible typed input and factor parameters.");
    }
    std::unordered_set<const ClauseStmt*> visiting;
    std::string impurity;
    if (!isReferenceMethodPure(selected, visiting, impurity)) {
        throw InterpreterError("Referenced callable " + reference->name + " is not pure: " + impurity);
    }
    return selected;
}

bool Interpreter::isReferenceMethodPure(const std::shared_ptr<ClauseStmt>& clause,
                                        std::unordered_set<const ClauseStmt*>& visiting,
                                        std::string& reason) const {
    if (!clause || !visiting.insert(clause.get()).second) return true;
    auto inspectExpr = [&](const auto& self, const std::shared_ptr<Expr>& expr) -> bool {
        if (!expr) return true;
        if (auto term = std::dynamic_pointer_cast<TermExpr>(expr)) {
            if (term->builtinId != BuiltinId::Unknown && term->builtinId != BuiltinId::Throw &&
                !isBuiltinPure(term->builtinId)) {
                reason = "uses impure builtin '" + term->name + "'";
                return false;
            }
            if (nativeDeclarationFor(term->name)) {
                reason = "uses native callable '" + term->name + "' without a pure capability declaration";
                return false;
            }
            const auto called = findClauses(term->name, term->nameId);
            if (called) for (const auto& next : *called) {
                if (isMethodClause(*next) && !isReferenceMethodPure(next, visiting, reason)) return false;
            }
            for (const auto& arg : term->args) if (!self(self, arg.value)) return false;
        } else if (auto array = std::dynamic_pointer_cast<ArrayExpr>(expr)) {
            for (const auto& item : array->items) if (!self(self, item)) return false;
        } else if (auto map = std::dynamic_pointer_cast<MapExpr>(expr)) {
            for (const auto& item : map->entries) if (!self(self, item.value)) return false;
        } else if (auto access = std::dynamic_pointer_cast<AccessExpr>(expr)) {
            return self(self, access->target);
        } else if (auto op = std::dynamic_pointer_cast<OperatorExpression>(expr)) {
            for (size_t i = 0; i < op->captureCount(); ++i) {
                if (!self(self, op->capture(i))) return false;
            }
        } else if (auto lambda = std::dynamic_pointer_cast<LambdaExpr>(expr)) {
            return self(self, lambda->source) && self(self, lambda->body) && self(self, lambda->right);
        }
        return true;
    };
    auto inspectGoals = [&](const auto& self, const std::vector<std::shared_ptr<Goal>>& goals) -> bool {
        for (const auto& goal : goals) {
            if (auto call = std::dynamic_pointer_cast<CallGoal>(goal)) {
                const auto hasSuffix = [&](const std::string& suffix) {
                    return call->call.name.size() >= suffix.size() &&
                        call->call.name.compare(call->call.name.size() - suffix.size(),
                                                suffix.size(), suffix) == 0;
                };
                if (hasSuffix(":depends") || hasSuffix(":relate") || hasSuffix(":references")) {
                    reason = "attaches facts";
                    return false;
                }
                if (call->call.builtinId != BuiltinId::Unknown && call->call.builtinId != BuiltinId::Throw &&
                    !isBuiltinPure(call->call.builtinId)) {
                    reason = "uses impure builtin '" + call->call.name + "'";
                    return false;
                }
                if (nativeDeclarationFor(call->call.name)) {
                    reason = "uses native callable '" + call->call.name + "' without a pure capability declaration";
                    return false;
                }
                const auto called = findClauses(call->call.name, call->call.nameId);
                if (called) for (const auto& next : *called) {
                    if (isMethodClause(*next) && !isReferenceMethodPure(next, visiting, reason)) return false;
                }
                for (const auto& arg : call->call.args) if (!inspectExpr(inspectExpr, arg.value)) return false;
            } else if (auto assign = std::dynamic_pointer_cast<AssignGoal>(goal)) {
                if (!inspectExpr(inspectExpr, assign->expr)) return false;
            } else if (auto ret = std::dynamic_pointer_cast<ReturnGoal>(goal)) {
                for (const auto& item : ret->fields) if (!inspectExpr(inspectExpr, item.value)) return false;
            } else if (auto conditional = std::dynamic_pointer_cast<IfGoal>(goal)) {
                if (!self(self, {conditional->condition}) || !self(self, conditional->thenBranch) ||
                    !self(self, conditional->elseBranch)) return false;
            } else if (auto group = std::dynamic_pointer_cast<GroupGoal>(goal)) {
                if (!self(self, group->goals)) return false;
            } else if (auto alternatives = std::dynamic_pointer_cast<OrGoal>(goal)) {
                for (const auto& branch : alternatives->branches) if (!self(self, branch)) return false;
            } else if (auto where = std::dynamic_pointer_cast<WhereGoal>(goal)) {
                if (!self(self, {where->condition})) return false;
            } else if (auto binary = std::dynamic_pointer_cast<BinaryGoal>(goal)) {
                if (!inspectExpr(inspectExpr, binary->left) || !inspectExpr(inspectExpr, binary->right)) return false;
            }
        }
        return true;
    };
    bool pure = inspectGoals(inspectGoals, clause->body);
    for (const auto& branch : clause->fallbackBranches) {
        if (pure && !inspectGoals(inspectGoals, branch)) pure = false;
    }
    visiting.erase(clause.get());
    return pure;
}

bool Interpreter::attachFactReference(const Call& call, Env& env, std::uint64_t sourceFactId) {
    const Arg* by = findArgByNameOrIndex(call, "by", 0);
    const Arg* factor = findArgByNameOrIndex(call, "factor", 1);
    const Arg* descriptor = findArgByNameOrIndex(call, "as", 2);
    if (!by || !factor || !descriptor) {
        throw InterpreterError(".references requires 'by', 'factor', and 'as' arguments");
    }
    std::shared_ptr<Expr> factorValue;
    std::shared_ptr<Expr> descriptorValue;
    if (!evalExprValue(factor->value, env, factorValue) ||
        !evalExprValue(descriptor->value, env, descriptorValue)) return false;
    const auto descriptorMap = std::dynamic_pointer_cast<MapExpr>(descriptorValue);
    const auto descriptorType = std::dynamic_pointer_cast<StringExpr>(
        findMapValue(descriptorValue, internalSymbolString(InternalSymbolKind::Type)));
    if (!descriptorMap || !descriptorType || descriptorType->value != "Reference") {
        throw InterpreterError(".references 'as' must be a Reference fact");
    }
    const auto source = memory_.factValueById(sourceFactId);
    if (!source) throw InterpreterError(".references source fact is no longer available");
    std::string callableName;
    const auto callable = resolveReferenceCallable(by->value, source, factorValue, callableName);
    auto& attachments = referencesBySource_[sourceFactId];
    for (const auto& existing : attachments) {
        if (existing.callableName == callableName && existing.descriptor->debug() == descriptorMap->debug() &&
            existing.defaultFactor->debug() == factorValue->debug()) {
            env[internalSymbolString(InternalSymbolKind::Return)] = std::make_shared<StringExpr>("ok");
            return true;
        }
    }
    attachments.push_back(ReferenceAttachment{
        nextReferenceAttachmentId_++, sourceFactId, callableName, callable,
        factorValue->clone(), std::static_pointer_cast<MapExpr>(descriptorMap->clone()),
        nextReferenceCreationOrder_++, {}, 0, true});
    env[internalSymbolString(InternalSymbolKind::Return)] = std::make_shared<StringExpr>("ok");
    return true;
}

std::shared_ptr<Expr> Interpreter::referenceEffectiveFactor(const ReferenceAttachment& attachment) const {
    if (auto map = std::dynamic_pointer_cast<MapExpr>(attachment.defaultFactor)) {
        if (map->factIdentity != 0) {
            if (auto current = memory_.factValueById(map->factIdentity)) return current;
        }
    }
    return attachment.defaultFactor->clone();
}

bool Interpreter::validateReferenceResult(const std::shared_ptr<Expr>& value,
                                          std::shared_ptr<MapExpr>& result) const {
    result = std::dynamic_pointer_cast<MapExpr>(value);
    const auto resultType = std::dynamic_pointer_cast<StringExpr>(
        findMapValue(value, internalSymbolString(InternalSymbolKind::Type)));
    const auto derived = findMapValue(value, "result");
    const auto derivedType = std::dynamic_pointer_cast<StringExpr>(
        findMapValue(derived, internalSymbolString(InternalSymbolKind::Type)));
    return result && resultType && resultType->value == "ReferenceResult" && derived && derivedType;
}

bool Interpreter::evalFactReferences(const Call& call, const Env& env, std::shared_ptr<Expr>& out) {
    const Arg* input = findArgByNameOrIndex(call, "input", 0);
    if (!input) throw InterpreterError("Fact.references requires stored fact argument 'input'");
    std::shared_ptr<Expr> source;
    if (!evalExprValue(input->value, env, source)) return false;
    const auto sourceId = memory_.logicalFactId(source);
    if (!sourceId) throw InterpreterError("Fact.references requires a stored fact with stable logical identity");
    const auto found = referencesBySource_.find(*sourceId);
    if (found == referencesBySource_.end()) {
        out = std::make_shared<ArrayExpr>(std::vector<std::shared_ptr<Expr>>{});
        return true;
    }

    const Arg* selector = findArgByNameOrIndex(call, "as", std::numeric_limits<size_t>::max());
    const Arg* overrideArg = findArgByNameOrIndex(call, "factor", std::numeric_limits<size_t>::max());
    std::shared_ptr<Expr> selectorValue;
    std::shared_ptr<Expr> overrideValue;
    if (selector && !evalExprValue(selector->value, env, selectorValue)) return false;
    if (overrideArg && !evalExprValue(overrideArg->value, env, overrideValue)) return false;
    const auto selectorMap = std::dynamic_pointer_cast<MapExpr>(selectorValue);
    if (selector && (!selectorMap || !findMapValue(selectorValue, internalSymbolString(InternalSymbolKind::Type)) ||
                     std::dynamic_pointer_cast<StringExpr>(findMapValue(selectorValue, internalSymbolString(InternalSymbolKind::Type)))->value != "Reference")) {
        throw InterpreterError("Fact.references 'as' must be a Reference fact");
    }

    std::vector<ReferenceAttachment*> selected;
    for (auto& attachment : found->second) {
        if (selectorMap && attachment.descriptor->debug() != selectorMap->debug()) continue;
        selected.push_back(&attachment);
    }
    if (selector && selected.empty()) throw InterpreterError("Fact.references selector did not match an attachment");
    if (selector && selected.size() > 1) throw InterpreterError("Fact.references selector is ambiguous");

    std::vector<std::shared_ptr<MapExpr>> staged;
    staged.reserve(selected.size());
    const auto currentSource = memory_.factValueById(*sourceId);
    if (!currentSource) throw InterpreterError("Fact.references source fact is no longer available");
    for (auto* attachment : selected) {
        const std::shared_ptr<Expr> factor = overrideValue ? overrideValue->clone() : referenceEffectiveFactor(*attachment);
        const MethodParamPlan factorParam = makeMethodParamPlan(attachment->callable->head.args[1]);
        if (!referenceValueMatchesType(factor, factorParam)) {
            throw InterpreterError("Fact.references factor is incompatible with referenced callable " + attachment->callableName);
        }
        const std::string cycleKey = std::to_string(*sourceId) + ":" + std::to_string(attachment->id) + ":" + factor->debug();
        if (!activeReferenceEvaluations_.insert(cycleKey).second) {
            throw InterpreterError("ReferenceEvaluationCycle: " + attachment->callableName);
        }
        std::shared_ptr<Expr> evaluated;
        try {
            TermExpr invocation(attachment->callableName, {
                Arg{"input", currentSource->clone()}, Arg{"factor", factor->clone()}});
            if (!evalCallAsValue(invocation, env, evaluated)) {
                throw InterpreterError("Referenced callable " + attachment->callableName + " produced no result");
            }
        } catch (...) {
            activeReferenceEvaluations_.erase(cycleKey);
            throw;
        }
        activeReferenceEvaluations_.erase(cycleKey);
        std::shared_ptr<MapExpr> validated;
        if (!validateReferenceResult(evaluated, validated)) {
            throw InterpreterError("Referenced callable " + attachment->callableName +
                                   " must return ReferenceResult(result: TypedFact(...))");
        }
        staged.push_back(std::static_pointer_cast<MapExpr>(validated->clone()));
    }

    if (!overrideValue) {
        const std::uint64_t generation = ++referenceEvaluationGeneration_;
        for (size_t index = 0; index < selected.size(); ++index) {
            selected[index]->canonicalResult =
                std::static_pointer_cast<MapExpr>(staged[index]->clone());
            selected[index]->canonicalGeneration = generation;
            selected[index]->dirty = false;
        }
    }
    std::vector<std::shared_ptr<Expr>> values;
    values.reserve(staged.size());
    for (const auto& value : staged) values.push_back(value->clone());
    out = std::make_shared<ArrayExpr>(std::move(values));
    return true;
}

bool Interpreter::evalRelationFind(const Call& call,
                                   const Env& env,
                                   std::shared_ptr<Expr>& out) {
    const Arg* inputArg = nullptr;
    const Arg* nameArg = nullptr;
    for (const auto& arg : call.args) {
        if (arg.name == "input" || arg.name == "relationships" || arg.name == "data") inputArg = &arg;
        if (arg.name == "name") nameArg = &arg;
    }
    if (!inputArg && !call.args.empty()) inputArg = &call.args[0];
    if (!nameArg && call.args.size() > 1) nameArg = &call.args[1];
    if (!inputArg || !nameArg) {
        throw InterpreterError("Relation.find expects relationship array 'input' and string 'name'");
    }
    std::shared_ptr<Expr> input;
    std::shared_ptr<Expr> nameValue;
    if (!evalExprValue(inputArg->value, env, input) || !evalExprValue(nameArg->value, env, nameValue)) return false;
    std::string name;
    if (!argAsString(nameValue, name)) throw InterpreterError("Relation.find expects string argument 'name'");
    const auto relationships = std::dynamic_pointer_cast<ArrayExpr>(input);
    if (!relationships) throw InterpreterError("Relation.find expects an array of relationship facts");
    for (const auto& item : relationships->items) {
        auto relationName = findMapValue(item, "name");
        if (!relationName) {
            const auto relationKind = findMapValue(item, "as");
            relationName = findMapValue(relationKind, "name");
        }
        const auto text = std::dynamic_pointer_cast<StringExpr>(relationName);
        if (text && text->value == name) {
            out = item->clone();
            return true;
        }
    }
    out = std::make_shared<NilExpr>();
    return true;
}

bool Interpreter::evalDependencySatisfied(const Call& call,
                                          const Env& env,
                                          std::shared_ptr<Expr>& out) {
    const Arg* inputArg = nullptr;
    for (const auto& arg : call.args) {
        if (arg.name == "input" || arg.name == "fact" || arg.name == "value") {
            inputArg = &arg;
            break;
        }
    }
    if (!inputArg && !call.args.empty()) inputArg = &call.args[0];
    if (!inputArg) throw InterpreterError("Dependency.satisfied expects fact argument 'input'");
    std::shared_ptr<Expr> input;
    if (!evalExprValue(inputArg->value, env, input)) return false;
    const auto id = memory_.logicalFactId(input);
    const bool satisfied = id && !memory_.hasDependencyCycle(*id) &&
                           memory_.missingDependencies(*id).empty();
    out = std::make_shared<BoolExpr>(satisfied);
    return true;
}

bool Interpreter::evalRelationCompare(const Call& call,
                                      const Env& env,
                                      std::shared_ptr<Expr>& out) {
    auto argument = [&](const std::string& name, size_t positional) -> std::shared_ptr<Expr> {
        for (const auto& arg : call.args) {
            if (arg.name == name) {
                std::shared_ptr<Expr> value;
                if (evalExprValue(arg.value, env, value)) return value;
                return {};
            }
        }
        if (positional < call.args.size() && call.args[positional].name.empty()) {
            std::shared_ptr<Expr> value;
            if (evalExprValue(call.args[positional].value, env, value)) return value;
        }
        return {};
    };
    auto source = argument("left", 0);
    auto target = argument("right", 1);
    std::shared_ptr<MapExpr> relationshipSelector;
    if (const auto selector = argument("relationship", 5)) {
        relationshipSelector = std::dynamic_pointer_cast<MapExpr>(selector);
        const auto selectorType = std::dynamic_pointer_cast<StringExpr>(
            findMapValue(selector, internalSymbolString(InternalSymbolKind::Type)));
        if (!relationshipSelector || !selectorType || selectorType->value != "Relationship") {
            throw InterpreterError(
                "Relation.compare relationship must be a Relationship(...) fact pattern");
        }
    }
    std::string comparisonMode = "directed";
    if (const auto modeValue = argument("mode", 3)) {
        const auto mode = std::dynamic_pointer_cast<StringExpr>(modeValue);
        if (!mode || (mode->value != "directed" && mode->value != "symmetric")) {
            throw InterpreterError("Relation.compare mode must be 'directed' or 'symmetric'");
        }
        comparisonMode = mode->value;
    }
    std::size_t maxRelationshipDepth = 4;
    if (const auto depthValue = argument("max_depth", 2)) {
        const auto number = std::dynamic_pointer_cast<NumberExpr>(depthValue);
        if (!number || number->value < 1.0 || number->value > 64.0 ||
            std::floor(number->value) != number->value) {
            throw InterpreterError("Relation.compare max_depth must be an integer from 1 to 64");
        }
        maxRelationshipDepth = static_cast<std::size_t>(number->value);
    }
    std::size_t maxAncestorDepth = 64;
    if (const auto depthValue = argument("max_ancestor_depth", 4)) {
        const auto number = std::dynamic_pointer_cast<NumberExpr>(depthValue);
        if (!number || number->value < 0.0 || number->value > 64.0 ||
            std::floor(number->value) != number->value) {
            throw InterpreterError("Relation.compare max_ancestor_depth must be an integer from 0 to 64");
        }
        maxAncestorDepth = static_cast<std::size_t>(number->value);
    }
    auto sourceMap = std::dynamic_pointer_cast<MapExpr>(source);
    auto targetMap = std::dynamic_pointer_cast<MapExpr>(target);
    auto sourceTypeValue = findMapValue(source, internalSymbolString(InternalSymbolKind::Type));
    auto targetTypeValue = findMapValue(target, internalSymbolString(InternalSymbolKind::Type));
    auto sourceType = std::dynamic_pointer_cast<StringExpr>(sourceTypeValue);
    auto targetType = std::dynamic_pointer_cast<StringExpr>(targetTypeValue);
    if (!sourceMap || !targetMap || !sourceType || !targetType) {
        throw InterpreterError("Relation.compare requires typed fact-compatible 'left' and 'right' values");
    }

    if (comparisonMode == "symmetric") {
        // Membership and target interpretation are deliberately directional.
        // A symmetric request must therefore retain both proofs rather than
        // pretend one direction is canonical or discard a disagreement.
        auto direct = [&](const std::shared_ptr<Expr>& first,
                          const std::shared_ptr<Expr>& second,
                          std::shared_ptr<Expr>& result) {
            Call directional("Relation:compare", {}, BuiltinId::RelationCompare);
            directional.args.push_back(Arg{"left", first->clone()});
            directional.args.push_back(Arg{"right", second->clone()});
            directional.args.push_back(Arg{"max_depth",
                std::make_shared<NumberExpr>(static_cast<double>(maxRelationshipDepth))});
            directional.args.push_back(Arg{"max_ancestor_depth",
                std::make_shared<NumberExpr>(static_cast<double>(maxAncestorDepth))});
            if (relationshipSelector) {
                directional.args.push_back(Arg{"relationship", relationshipSelector->clone()});
            }
            return evalRelationCompare(directional, env, result);
        };
        std::shared_ptr<Expr> forward;
        std::shared_ptr<Expr> reverse;
        if (!direct(source, target, forward) || !direct(target, source, reverse)) return false;
        const auto forwardState = std::dynamic_pointer_cast<StringExpr>(findMapValue(forward, "state"));
        const auto reverseState = std::dynamic_pointer_cast<StringExpr>(findMapValue(reverse, "state"));
        const auto forwardEvidence = findMapValue(forward, "evidence");
        const auto reverseEvidence = findMapValue(reverse, "evidence");
        const auto forwardSimilarity = std::dynamic_pointer_cast<NumberExpr>(
            findMapValue(forwardEvidence, "similarity"));
        const auto reverseSimilarity = std::dynamic_pointer_cast<NumberExpr>(
            findMapValue(reverseEvidence, "similarity"));
        const bool sameState = forwardState && reverseState &&
            forwardState->value == reverseState->value;
        const bool sameSimilarity = forwardSimilarity && reverseSimilarity &&
            std::abs(forwardSimilarity->value - reverseSimilarity->value) <= 1e-12;
        const bool agrees = sameState && sameSimilarity;
        const double similarity = forwardSimilarity && reverseSimilarity
            ? (forwardSimilarity->value + reverseSimilarity->value) / 2.0
            : 0.0;
        auto result = std::make_shared<MapExpr>(std::vector<MapEntry>{
            {internalSymbolString(InternalSymbolKind::Type),
                std::make_shared<StringExpr>("SymmetricComparison")},
            {"mode", std::make_shared<StringExpr>("symmetric")},
            {"state", std::make_shared<StringExpr>(
                agrees ? "agreed" : "directional_difference")},
            {"agrees", std::make_shared<BoolExpr>(agrees)},
            {"similarity", std::make_shared<NumberExpr>(similarity)},
            {"forward_similarity", cloneExprOrNil(forwardSimilarity)},
            {"reverse_similarity", cloneExprOrNil(reverseSimilarity)},
            {"forward", forward->clone()},
            {"reverse", reverse->clone()}});
        result->factType = "SymmetricComparison";
        out = std::move(result);
        return true;
    }

    const auto sourceId = memory_.logicalFactId(source);
    const auto targetId = memory_.logicalFactId(target);
    // Comparison results are ordinary fact-compatible values and can be
    // chained.  Other source/target facts must still come from the store.
    if (!sourceId && sourceType->value != "Comparison") {
        throw InterpreterError("Relation.compare left fact is not present in the fact store or is structurally ambiguous; retrieve it through Fact.all/select");
    }
    if (!targetId && targetType->value != "Comparison") {
        throw InterpreterError("Relation.compare right fact is not present in the fact store or is structurally ambiguous; retrieve it through Fact.all/select");
    }

    // A constructor-shaped value may mention only its identity fields. Once
    // it has been resolved, rules must receive the stored fact's full,
    // inherited field set—not the caller's partial reconstruction.
    if (sourceId) {
        source = memory_.factValueById(*sourceId);
        if (!source) throw InterpreterError("Relation.compare source fact is no longer active");
    }
    if (targetId) {
        target = memory_.factValueById(*targetId);
        if (!target) throw InterpreterError("Relation.compare target fact is no longer active");
    }
    sourceMap = std::dynamic_pointer_cast<MapExpr>(source);
    targetMap = std::dynamic_pointer_cast<MapExpr>(target);
    sourceTypeValue = findMapValue(source, internalSymbolString(InternalSymbolKind::Type));
    targetTypeValue = findMapValue(target, internalSymbolString(InternalSymbolKind::Type));
    sourceType = std::dynamic_pointer_cast<StringExpr>(sourceTypeValue);
    targetType = std::dynamic_pointer_cast<StringExpr>(targetTypeValue);
    if (!sourceMap || !targetMap || !sourceType || !targetType) {
        throw InterpreterError("Relation.compare resolved fact is invalid");
    }

    // Comparison chaining is still ordinary membership dispatch.  The
    // standard implementation lives in the built-in core source module, not
    // in a separate result-comparison engine or native package.  A user
    // declaration loaded before execution takes precedence.
    if (sourceType->value == "Comparison" &&
        !findClauses("Comparison:membership", symbolIdForName("Comparison:membership"))) {
        ensurePredicateLoaded("Comparison:membership");
    }
    const auto& sourceAncestry = typeAncestry(sourceType->value);
    const auto& targetAncestry = typeAncestry(targetType->value);
    const ComparisonDispatchKey dispatchKey{
        symbolIdForName(sourceType->value), symbolIdForName(targetType->value),
        sourceType->value, targetType->value};
    ComparisonDispatchPlan dispatch;
    const auto cachedDispatch = comparisonDispatchCache_.find(dispatchKey);
    if (cachedDispatch != comparisonDispatchCache_.end()) {
        ++dispatchCacheHits_;
        dispatch = cachedDispatch->second;
    } else {
        ++dispatchCacheMisses_;
        dispatch.targetFamily = targetType->value;
        // Resolution is target-owned. Choosing a method is pure dispatch
        // work; its body is still executed only after dependency validation.
        for (const auto& family : targetAncestry) {
            // Fact is the universal value type, not a target fact family.
            // A generic Fact.compareMembership would become an implicit
            // second execution path for every comparison and hide the
            // structural default.  Only concrete target families own
            // comparison interpretation.
            if (family == "Fact") continue;
            const std::string name = family + ":compareMembership";
            auto* clauses = findClauses(name, symbolIdForName(name));
            if (!clauses) continue;
            for (const auto& clause : *clauses) {
                if (!isMethodClause(*clause)) continue;
                bool acceptsContext = false;
                for (const auto& parameter : clause->head.args) {
                    if (parameter.name != "context") continue;
                    const auto parameterPlan = makeMethodParamPlan(parameter);
                    acceptsContext = parameterPlan.typedParam &&
                        (parameterPlan.typeName == "Fact" || parameterPlan.typeName == "any");
                    break;
                }
                if (!acceptsContext) continue;
                dispatch.comparisonClause = clause;
                dispatch.comparisonName = name;
                dispatch.targetFamily = family;
                break;
            }
            if (dispatch.comparisonClause) break;
        }

        for (const auto& family : sourceAncestry) {
            const std::string name = family + ":membership";
            auto* clauses = findClauses(name, symbolIdForName(name));
            if (!clauses) continue;
            int bestDistance = std::numeric_limits<int>::max();
            for (const auto& clause : *clauses) {
                if (!isMethodClause(*clause)) continue;
                int distance = std::numeric_limits<int>::max();
                for (const auto& parameter : clause->head.args) {
                    if (parameter.name != "against") continue;
                    const auto parameterPlan = makeMethodParamPlan(parameter);
                    if (!parameterPlan.typedParam) break;
                    if (parameterPlan.typeName == "any") {
                        distance = static_cast<int>(targetAncestry.size()) + 1;
                    } else if (parameterPlan.typeName == "Fact") {
                        distance = static_cast<int>(targetAncestry.size()) - 1;
                    } else {
                        if (parameterPlan.builtinType) break;
                        const auto found = std::find(targetAncestry.begin(), targetAncestry.end(), parameterPlan.typeName);
                        if (found != targetAncestry.end()) {
                            distance = static_cast<int>(std::distance(targetAncestry.begin(), found));
                        }
                    }
                    break;
                }
                if (distance < bestDistance) {
                    bestDistance = distance;
                    dispatch.membershipClause = clause;
                    dispatch.membershipName = name;
                }
            }
            if (dispatch.membershipClause) break;
        }
        comparisonDispatchCache_.emplace(dispatchKey, dispatch);
    }
    const auto& comparisonClause = dispatch.comparisonClause;
    const auto& comparisonName = dispatch.comparisonName;
    const auto& targetFamily = dispatch.targetFamily;

    auto buildResult = [&](const std::string& state,
                           const std::shared_ptr<Expr>& membership,
                           const std::shared_ptr<Expr>& evidence,
                           const std::string& reason = {}) {
        std::vector<MapEntry> entries{
            {internalSymbolString(InternalSymbolKind::Type), std::make_shared<StringExpr>("Comparison")},
            {"state", std::make_shared<StringExpr>(state)},
            {"source", source->clone()},
            {"target", target->clone()},
            {"targetFamily", std::make_shared<StringExpr>(targetFamily)},
            {"membership", cloneExprOrNil(membership)},
            {"evidence", cloneExprOrNil(evidence)}};
        if (!reason.empty()) entries.push_back(MapEntry{"reason", std::make_shared<StringExpr>(reason)});
        auto result = std::make_shared<MapExpr>(std::move(entries));
        result->factType = "Comparison";
        return result;
    };

    std::vector<std::shared_ptr<Expr>> missing;
    auto appendMissing = [&](const std::optional<std::uint64_t>& id) {
        if (!id) return;
        for (const auto& dependency : memory_.missingDependencies(*id)) missing.push_back(dependency->clone());
    };
    appendMissing(sourceId);
    appendMissing(targetId);
    if ((sourceId && memory_.hasDependencyCycle(*sourceId)) ||
        (targetId && memory_.hasDependencyCycle(*targetId))) {
        throw InterpreterError("Relation.compare detected a hard dependency cycle");
    }
    if (!missing.empty()) {
        auto unresolved = buildResult("unresolved", std::make_shared<NilExpr>(),
                                      std::make_shared<MapExpr>(std::vector<MapEntry>{}),
                                      "required dependency is not satisfied");
        unresolved->entries.push_back(MapEntry{"missingDependencies", std::make_shared<ArrayExpr>(std::move(missing))});
        out = unresolved;
        return true;
    }

    std::shared_ptr<Expr> membership;
    const auto& membershipClause = dispatch.membershipClause;
    const auto& membershipName = dispatch.membershipName;

    const std::string comparisonKey =
        (sourceId ? std::to_string(*sourceId) : source->debug()) + "|" +
        (targetId ? std::to_string(*targetId) : target->debug()) + "|" + membershipName + "|" + targetFamily;
    if (!activeComparisons_.insert(comparisonKey).second) {
        throw InterpreterError("Relation.compare detected a comparison cycle for " + sourceType->value + " -> " + targetType->value);
    }
    struct ComparisonGuard {
        std::unordered_set<std::string>& set;
        std::string key;
        ~ComparisonGuard() { set.erase(key); }
    } guard{activeComparisons_, comparisonKey};

    if (membershipClause) {
        Call membershipCall(membershipName, {{"input", source->clone()}, {"against", target->clone()}});
        if (!invokeComparisonMethod(membershipClause, membershipCall, env, membership)) {
            out = buildResult("incomparable", std::make_shared<NilExpr>(),
                              std::make_shared<MapExpr>(std::vector<MapEntry>{}),
                              "membership method produced no result");
            return true;
        }
    } else {
        std::vector<MapEntry> fields;
        for (const auto& entry : sourceMap->entries) {
            if (entry.key == internalSymbolString(InternalSymbolKind::Type) ||
                entry.key == internalSymbolString(InternalSymbolKind::Parent)) continue;
            fields.push_back(MapEntry{entry.key, entry.value->clone()});
        }
        membership = std::make_shared<MapExpr>(std::move(fields));
    }
    if (std::dynamic_pointer_cast<NilExpr>(membership)) {
        out = buildResult("incomparable", membership,
                          std::make_shared<MapExpr>(std::vector<MapEntry>{}),
                          "source cannot produce membership knowledge for this target");
        return true;
    }
    const auto membershipMap = std::dynamic_pointer_cast<MapExpr>(membership);
    if (!membershipMap) {
        throw InterpreterError("Relation.compare membership method must return a key-value micro-fact or nil");
    }

    std::vector<std::shared_ptr<Expr>> matched;
    std::vector<std::shared_ptr<Expr>> missingFields;
    std::vector<std::shared_ptr<Expr>> conflicting;
    std::vector<std::shared_ptr<Expr>> unknown;
    std::vector<std::shared_ptr<Expr>> propertyEvidence;
    size_t membershipFields = 0;
    const double propertyImportance = membershipMap->entries.empty()
        ? 0.0 : 1.0 / static_cast<double>(membershipMap->entries.size());
    for (const auto& field : membershipMap->entries) {
        ++membershipFields;
        const auto targetValue = findMapValue(target, field.key);
        std::string status;
        if (!targetValue) {
            status = "missing";
            missingFields.push_back(std::make_shared<StringExpr>(field.key));
        } else if (field.value->debug() == targetValue->debug()) {
            status = "matched";
            matched.push_back(std::make_shared<StringExpr>(field.key));
        } else {
            status = "conflicting";
            conflicting.push_back(std::make_shared<StringExpr>(field.key));
        }
        auto detail = std::make_shared<MapExpr>(std::vector<MapEntry>{
            {internalSymbolString(InternalSymbolKind::Type),
                std::make_shared<StringExpr>("PropertyComparisonEvidence")},
            {"property", std::make_shared<StringExpr>(field.key)},
            {"status", std::make_shared<StringExpr>(status)},
            {"source_value", field.value->clone()},
            {"target_value", cloneExprOrNil(targetValue)},
            {"importance", std::make_shared<NumberExpr>(propertyImportance)},
            {"contribution", std::make_shared<NumberExpr>(
                status == "matched" ? propertyImportance : 0.0)}});
        detail->factType = "PropertyComparisonEvidence";
        propertyEvidence.push_back(std::move(detail));
    }
    for (const auto& field : targetMap->entries) {
        if (field.key == internalSymbolString(InternalSymbolKind::Type) ||
            field.key == internalSymbolString(InternalSymbolKind::Parent)) continue;
        if (!findMapValue(membership, field.key)) unknown.push_back(std::make_shared<StringExpr>(field.key));
    }
    const bool allMatched = membershipFields > 0 && matched.size() == membershipFields;
    const bool exact = allMatched && unknown.empty();
    const bool subset = allMatched && !exact;
    const double overlap = membershipFields == 0 ? 0.0 :
        static_cast<double>(matched.size()) / static_cast<double>(membershipFields);
    bool ancestorContained = memory_.isCompatibleType(sourceType->value, targetType->value);
    struct AncestorCandidate {
        std::string type;
        std::size_t sourceDistance = 0;
        std::size_t targetDistance = 0;
        double depth = 0.0;
        double similarity = 0.0;
    };
    std::vector<AncestorCandidate> ancestorCandidates;
    const auto& sourceDistances = typeAncestorDistances(sourceType->value);
    const auto& targetDistances = typeAncestorDistances(targetType->value);
    const double sourceDepth = typeHierarchyDepth(sourceType->value);
    const double targetDepth = typeHierarchyDepth(targetType->value);
    const double depthTotal = sourceDepth + targetDepth;
    for (const auto& [ancestor, sourceDistance] : sourceDistances) {
        if (ancestor == "Fact") continue;
        const auto targetDistance = targetDistances.find(ancestor);
        if (targetDistance == targetDistances.end()) continue;
        if (sourceDistance > maxAncestorDepth ||
            targetDistance->second > maxAncestorDepth) continue;
        const double commonDepth = typeHierarchyDepth(ancestor);
        ancestorCandidates.push_back(AncestorCandidate{
            ancestor,
            sourceDistance,
            targetDistance->second,
            commonDepth,
            depthTotal > 0.0 ? (2.0 * commonDepth) / depthTotal : 0.0});
    }
    std::sort(ancestorCandidates.begin(), ancestorCandidates.end(),
        [](const AncestorCandidate& left, const AncestorCandidate& right) {
            if (left.similarity != right.similarity) {
                return left.similarity > right.similarity;
            }
            const auto leftDistance = left.sourceDistance + left.targetDistance;
            const auto rightDistance = right.sourceDistance + right.targetDistance;
            if (leftDistance != rightDistance) return leftDistance < rightDistance;
            return left.type < right.type;
        });
    const std::string matchedAncestorName = ancestorCandidates.empty()
        ? std::string{} : ancestorCandidates.front().type;
    std::shared_ptr<Expr> matchedAncestor = matchedAncestorName.empty()
        ? std::shared_ptr<Expr>(std::make_shared<NilExpr>())
        : std::shared_ptr<Expr>(std::make_shared<StringExpr>(matchedAncestorName));
    const double ancestorDistance = ancestorCandidates.empty() ? 0.0 :
        static_cast<double>(ancestorCandidates.front().sourceDistance +
            ancestorCandidates.front().targetDistance);
    double ancestorSimilarity = 0.0;
    if (!ancestorCandidates.empty()) {
        ancestorSimilarity = ancestorCandidates.front().similarity;
    }
    std::vector<std::shared_ptr<Expr>> ancestorEvidence;
    ancestorEvidence.reserve(ancestorCandidates.size());
    for (const auto& candidate : ancestorCandidates) {
        auto detail = std::make_shared<MapExpr>(std::vector<MapEntry>{
            {internalSymbolString(InternalSymbolKind::Type),
                std::make_shared<StringExpr>("AncestorComparisonEvidence")},
            {"ancestor", std::make_shared<StringExpr>(candidate.type)},
            {"source_distance", std::make_shared<NumberExpr>(
                static_cast<double>(candidate.sourceDistance))},
            {"target_distance", std::make_shared<NumberExpr>(
                static_cast<double>(candidate.targetDistance))},
            {"depth", std::make_shared<NumberExpr>(candidate.depth)},
            {"importance", std::make_shared<NumberExpr>(candidate.similarity)}});
        detail->factType = "AncestorComparisonEvidence";
        ancestorEvidence.push_back(std::move(detail));
    }

    // Compare bounded relationship neighborhoods as facts, not words. This
    // adds explainable multi-hop evidence while keeping cyclic knowledge
    // graphs finite and deterministic.
    struct RelationPath {
        std::size_t depth = 0;
        std::uint64_t terminal = 0;
        double confidence = 1.0;
    };
    const auto relationshipMatches = [&](const FactRelationship& relation) {
        if (!relationshipSelector) return true;
        if (!relation.relationship) return false;
        for (const auto& field : relationshipSelector->entries) {
            if (field.keyId == InternalSymbol::TypeId ||
                field.keyId == InternalSymbol::ParentId) {
                continue;
            }
            const auto actual = findMapValue(relation.relationship, field.key);
            if (!actual || !exprEqualsLiteral(actual, field.value)) return false;
        }
        return true;
    };
    auto relationshipNeighborhood = [&](const std::optional<std::uint64_t>& root) {
        std::unordered_map<std::string, RelationPath> paths;
        if (!root) return paths;
        struct PendingPath {
            std::uint64_t factId = 0;
            std::size_t depth = 0;
            double confidence = 1.0;
        };
        std::deque<PendingPath> pending{{*root, 0, 1.0}};
        std::unordered_map<std::uint64_t, std::pair<std::size_t, double>> visited;
        while (!pending.empty()) {
            const auto current = pending.front().factId;
            const auto depth = pending.front().depth;
            const auto pathConfidence = pending.front().confidence;
            pending.pop_front();
            const auto seen = visited.find(current);
            if (seen != visited.end() &&
                (seen->second.first < depth ||
                 (seen->second.first == depth && seen->second.second >= pathConfidence))) {
                continue;
            }
            visited[current] = {depth, pathConfidence};
            if (depth >= maxRelationshipDepth) continue;
            const auto visitRelationships = [&](const auto& relationships) {
              for (const auto& relation : relationships) {
                ++relationshipCandidates_;
                if (!relationshipMatches(relation)) {
                    ++relationshipCandidatesPruned_;
                    continue;
                }
                const std::uint64_t next = relation.sourceId == current
                    ? relation.targetId : relation.sourceId;
                if (next == 0 || next == current) continue;
                const std::string signature = relation.relationship
                    ? relation.relationship->debug() : std::string("nil");
                double confidence = 1.0;
                if (const auto number = std::dynamic_pointer_cast<NumberExpr>(relation.confidence)) {
                    confidence = std::clamp(number->value, 0.0, 1.0);
                }
                const std::size_t nextDepth = depth + 1;
                const double combinedConfidence = pathConfidence * confidence;
                auto found = paths.find(signature);
                if (found == paths.end() || nextDepth < found->second.depth ||
                    (nextDepth == found->second.depth &&
                     combinedConfidence > found->second.confidence)) {
                    paths[signature] = RelationPath{nextDepth, next, combinedConfidence};
                }
                pending.push_back(PendingPath{next, nextDepth, combinedConfidence});
              }
            };
            visitRelationships(memory_.outgoingRelationships(current));
            visitRelationships(memory_.incomingRelationships(current));
        }
        return paths;
    };
    const auto sourceRelationships = relationshipNeighborhood(sourceId);
    const auto targetRelationships = relationshipNeighborhood(targetId);
    std::vector<std::shared_ptr<Expr>> relationshipEvidence;
    std::vector<std::shared_ptr<Expr>> relationshipDifferences;
    std::size_t sharedRelationships = 0;
    double terminalSimilaritySum = 0.0;
    std::size_t terminalComparisons = 0;
    double sharedConfidenceSum = 0.0;
    const auto factTypeForId = [&](std::uint64_t id) -> std::shared_ptr<Expr> {
        const auto value = memory_.factValueById(id);
        if (!value) return std::make_shared<NilExpr>();
        const auto type = std::dynamic_pointer_cast<StringExpr>(
            findMapValue(value, internalSymbolString(InternalSymbolKind::Type)));
        return type ? std::static_pointer_cast<Expr>(type->clone())
                    : std::make_shared<NilExpr>();
    };
    const auto factTypeNameForId = [&](std::uint64_t id) -> std::string {
        const auto value = memory_.factValueById(id);
        if (!value) return {};
        const auto type = std::dynamic_pointer_cast<StringExpr>(
            findMapValue(value, internalSymbolString(InternalSymbolKind::Type)));
        return type ? type->value : std::string{};
    };
    const auto findMapValueById = [](const std::shared_ptr<Expr>& value,
                                     SymbolId key) -> std::shared_ptr<Expr> {
        const auto map = std::dynamic_pointer_cast<MapExpr>(value);
        if (!map) return nullptr;
        for (const auto& entry : map->entries) {
            if (entry.keyId == key) return entry.value;
        }
        return nullptr;
    };
    std::function<double(const std::shared_ptr<Expr>&,
                         const std::shared_ptr<Expr>&, std::size_t)> valueSimilarity;
    valueSimilarity = [&](const std::shared_ptr<Expr>& left,
                          const std::shared_ptr<Expr>& right,
                          std::size_t depth) -> double {
        if (!left || !right) return left == right ? 1.0 : 0.0;
        if (exprEqualsLiteral(left, right)) return 1.0;
        if (depth >= 3) return 0.0;
        if (const auto leftMap = std::dynamic_pointer_cast<MapExpr>(left)) {
            const auto rightMap = std::dynamic_pointer_cast<MapExpr>(right);
            if (!rightMap) return 0.0;
            std::set<SymbolId> keys;
            for (const auto& entry : leftMap->entries) {
                if (entry.keyId != InternalSymbol::TypeId &&
                    entry.keyId != InternalSymbol::ParentId) keys.insert(entry.keyId);
            }
            for (const auto& entry : rightMap->entries) {
                if (entry.keyId != InternalSymbol::TypeId &&
                    entry.keyId != InternalSymbol::ParentId) keys.insert(entry.keyId);
            }
            if (keys.empty()) return 1.0;
            double total = 0.0;
            for (const SymbolId key : keys) {
                const auto leftValue = findMapValueById(left, key);
                const auto rightValue = findMapValueById(right, key);
                total += valueSimilarity(leftValue, rightValue, depth + 1);
            }
            return total / static_cast<double>(keys.size());
        }
        if (const auto leftArray = std::dynamic_pointer_cast<ArrayExpr>(left)) {
            const auto rightArray = std::dynamic_pointer_cast<ArrayExpr>(right);
            if (!rightArray) return 0.0;
            if (leftArray->items.empty() || rightArray->items.empty()) {
                return leftArray->items.empty() && rightArray->items.empty() ? 1.0 : 0.0;
            }
            const std::size_t count = std::min(leftArray->items.size(), rightArray->items.size());
            double total = 0.0;
            for (std::size_t index = 0; index < count; ++index) {
                total += valueSimilarity(leftArray->items[index], rightArray->items[index], depth + 1);
            }
            return total / static_cast<double>(std::max(leftArray->items.size(), rightArray->items.size()));
        }
        return 0.0;
    };
    const auto typeSimilarity = [&](const std::string& left,
                                    const std::string& right,
                                    std::string& commonAncestor) {
        if (left.empty() || right.empty()) return 0.0;
        const auto& leftDistances = typeAncestorDistances(left);
        const auto& rightDistances = typeAncestorDistances(right);
        double best = 0.0;
        std::size_t bestDistance = std::numeric_limits<std::size_t>::max();
        for (const auto& [ancestor, leftDistance] : leftDistances) {
            const auto rightDistance = rightDistances.find(ancestor);
            if (rightDistance == rightDistances.end() || ancestor == "Fact") continue;
            const double denominator = typeHierarchyDepth(left) + typeHierarchyDepth(right);
            const double score = denominator == 0.0 ? 0.0 :
                (2.0 * typeHierarchyDepth(ancestor)) / denominator;
            const std::size_t distance = leftDistance + rightDistance->second;
            if (score > best || (score == best && distance < bestDistance)) {
                best = score;
                bestDistance = distance;
                commonAncestor = ancestor;
            }
        }
        return best;
    };
    for (const auto& [signature, sourcePath] : sourceRelationships) {
        const auto targetPath = targetRelationships.find(signature);
        if (targetPath == targetRelationships.end()) continue;
        ++sharedRelationships;
        const std::string sourceTerminalType = factTypeNameForId(sourcePath.terminal);
        const std::string targetTerminalType = factTypeNameForId(targetPath->second.terminal);
        std::string terminalAncestor;
        const double terminalTypeSimilarity = typeSimilarity(
            sourceTerminalType, targetTerminalType, terminalAncestor);
        const auto sourceTerminal = memory_.factValueById(sourcePath.terminal);
        const auto targetTerminal = memory_.factValueById(targetPath->second.terminal);
        const double terminalPropertySimilarity = valueSimilarity(
            sourceTerminal, targetTerminal, 0);
        const double terminalSimilarity = sourceTerminal && targetTerminal
            ? 0.5 * terminalTypeSimilarity + 0.5 * terminalPropertySimilarity
            : terminalTypeSimilarity;
        if (!sourceTerminalType.empty() && !targetTerminalType.empty()) {
            terminalSimilaritySum += terminalSimilarity;
            ++terminalComparisons;
        }
        const double pathConfidence = sourcePath.confidence * targetPath->second.confidence;
        sharedConfidenceSum += pathConfidence;
        auto detail = std::make_shared<MapExpr>(std::vector<MapEntry>{
            {internalSymbolString(InternalSymbolKind::Type),
                std::make_shared<StringExpr>("RelationshipComparisonEvidence")},
            {"relationship", std::make_shared<StringExpr>(signature)},
            {"source_depth", std::make_shared<NumberExpr>(
                static_cast<double>(sourcePath.depth))},
            {"target_depth", std::make_shared<NumberExpr>(
                static_cast<double>(targetPath->second.depth))},
            {"confidence", std::make_shared<NumberExpr>(pathConfidence)},
            {"terminal_type_similarity", std::make_shared<NumberExpr>(terminalTypeSimilarity)},
            {"terminal_property_similarity", std::make_shared<NumberExpr>(
                terminalPropertySimilarity)},
            {"source_terminal", factTypeForId(sourcePath.terminal)},
            {"target_terminal", factTypeForId(targetPath->second.terminal)},
            {"terminal_similarity", std::make_shared<NumberExpr>(terminalSimilarity)},
            {"terminal_common_ancestor", terminalAncestor.empty()
                ? std::shared_ptr<Expr>(std::make_shared<NilExpr>())
                : std::shared_ptr<Expr>(std::make_shared<StringExpr>(terminalAncestor))},
            {"matched", std::make_shared<BoolExpr>(true)}});
        detail->factType = "RelationshipComparisonEvidence";
        relationshipEvidence.push_back(std::move(detail));
    }
    auto appendRelationshipDifference = [&](const std::string& signature,
                                            const RelationPath& path,
                                            const std::string& side) {
        auto detail = std::make_shared<MapExpr>(std::vector<MapEntry>{
            {internalSymbolString(InternalSymbolKind::Type),
                std::make_shared<StringExpr>("RelationshipDifferenceEvidence")},
            {"relationship", std::make_shared<StringExpr>(signature)},
            {"side", std::make_shared<StringExpr>(side)},
            {"depth", std::make_shared<NumberExpr>(static_cast<double>(path.depth))},
            {"terminal", factTypeForId(path.terminal)},
            {"matched", std::make_shared<BoolExpr>(false)}});
        detail->factType = "RelationshipDifferenceEvidence";
        relationshipDifferences.push_back(std::move(detail));
    };
    for (const auto& [signature, path] : sourceRelationships) {
        if (targetRelationships.find(signature) == targetRelationships.end()) {
            appendRelationshipDifference(signature, path, "source");
        }
    }
    for (const auto& [signature, path] : targetRelationships) {
        if (sourceRelationships.find(signature) == sourceRelationships.end()) {
            appendRelationshipDifference(signature, path, "target");
        }
    }
    const std::size_t relationshipUnion = sourceRelationships.size() +
        targetRelationships.size() - sharedRelationships;
    const double pathSimilarity = relationshipUnion == 0 ? 0.0 :
        static_cast<double>(sharedRelationships) /
        static_cast<double>(relationshipUnion);
    const double terminalSimilarity = terminalComparisons == 0 ? 0.0 :
        terminalSimilaritySum / static_cast<double>(terminalComparisons);
    const double relationalConfidence = sharedRelationships == 0 ? 0.0 :
        sharedConfidenceSum / static_cast<double>(sharedRelationships);
    const double relationshipSimilarity = terminalComparisons == 0 ? pathSimilarity :
        0.5 * pathSimilarity + 0.5 * terminalSimilarity;
    const double confidenceWeightedRelationshipSimilarity =
        relationshipSimilarity * relationalConfidence;
    // Both hierarchy and properties are required. A geometric mean prevents
    // coincidentally equal fields in unrelated fact families from producing
    // a positive fact-similarity score.
    const double structuralSimilarity = std::sqrt(ancestorSimilarity * overlap);
    const double similarity = relationshipUnion == 0 ? structuralSimilarity :
        (0.70 * structuralSimilarity +
            0.30 * confidenceWeightedRelationshipSimilarity);
    std::vector<MapEntry> evidenceEntries{
        {"exact", std::make_shared<BoolExpr>(exact)},
        {"subset", std::make_shared<BoolExpr>(subset)},
        {"overlap", std::make_shared<NumberExpr>(overlap)},
        {"propertySimilarity", std::make_shared<NumberExpr>(overlap)},
        {"ancestorSimilarity", std::make_shared<NumberExpr>(ancestorSimilarity)},
        {"relationshipSimilarity", std::make_shared<NumberExpr>(relationshipSimilarity)},
        {"relationalConfidence", std::make_shared<NumberExpr>(relationalConfidence)},
        {"relationshipMaxDepth", std::make_shared<NumberExpr>(
            static_cast<double>(maxRelationshipDepth))},
        {"relationshipSelector", relationshipSelector
            ? std::static_pointer_cast<Expr>(relationshipSelector->clone())
            : std::shared_ptr<Expr>(std::make_shared<NilExpr>())},
        {"ancestorMaxDepth", std::make_shared<NumberExpr>(
            static_cast<double>(maxAncestorDepth))},
        {"sharedRelationshipCount", std::make_shared<NumberExpr>(
            static_cast<double>(sharedRelationships))},
        {"similarity", std::make_shared<NumberExpr>(similarity)},
        {"matchedFields", std::make_shared<ArrayExpr>(std::move(matched))},
        {"missingFields", std::make_shared<ArrayExpr>(std::move(missingFields))},
        {"conflictingFields", std::make_shared<ArrayExpr>(std::move(conflicting))},
        {"unknownFields", std::make_shared<ArrayExpr>(std::move(unknown))},
        {"propertyEvidence", std::make_shared<ArrayExpr>(
            std::move(propertyEvidence))},
        {"ancestorContained", std::make_shared<BoolExpr>(ancestorContained)},
        {"matchedAncestor", std::move(matchedAncestor)},
        {"ancestorDistance", std::make_shared<NumberExpr>(ancestorDistance)},
        {"ancestorEvidence", std::make_shared<ArrayExpr>(
            std::move(ancestorEvidence))},
        {"relationshipEvidence", std::make_shared<ArrayExpr>(
            std::move(relationshipEvidence))},
        {"relationshipDifferences", std::make_shared<ArrayExpr>(
            std::move(relationshipDifferences))}};
    const auto evidence = std::make_shared<MapExpr>(std::move(evidenceEntries));

    std::vector<MapEntry> relationshipEntries;
    std::unordered_set<std::string> seenRelationships;
    auto appendRelationships = [&](const std::optional<std::uint64_t>& id, const std::string& direction) {
        if (!id) return;
        const auto append = [&](const auto& relationships) {
          for (const auto& relation : relationships) {
            const std::string relationshipKey =
                std::to_string(relation.sourceId) + "|" + std::to_string(relation.targetId) + "|" +
                relation.relationship->debug() + "|" + cloneExprOrNil(relation.degree)->debug() + "|" +
                cloneExprOrNil(relation.confidence)->debug();
            if (!seenRelationships.insert(relationshipKey).second) continue;
            relationshipEntries.push_back(MapEntry{"relationship", std::make_shared<MapExpr>(std::vector<MapEntry>{
                {"sourceId", std::make_shared<NumberExpr>(static_cast<double>(relation.sourceId))},
                {"targetId", std::make_shared<NumberExpr>(static_cast<double>(relation.targetId))},
                {"as", relation.relationship->clone()},
                {"degree", cloneExprOrNil(relation.degree)},
                {"confidence", cloneExprOrNil(relation.confidence)},
                {"direction", std::make_shared<StringExpr>(direction)}})});
          }
        };
        append(memory_.outgoingRelationships(*id));
        append(memory_.incomingRelationships(*id));
    };
    appendRelationships(sourceId, "source");
    if (targetId != sourceId) appendRelationships(targetId, "target");
    std::vector<std::shared_ptr<Expr>> relationshipValues;
    for (auto& entry : relationshipEntries) relationshipValues.push_back(entry.value);
    std::vector<std::shared_ptr<Expr>> ancestryValues;
    for (const auto& ancestor : targetAncestry) ancestryValues.push_back(std::make_shared<StringExpr>(ancestor));
    auto context = std::make_shared<MapExpr>(std::vector<MapEntry>{
        {internalSymbolString(InternalSymbolKind::Type),
            std::make_shared<StringExpr>("ComparisonContext")},
        {"source", source->clone()}, {"target", target->clone()},
        {"targetFamily", std::make_shared<StringExpr>(targetFamily)},
        {"membership", membership->clone()}, {"targetFields", target->clone()},
        {"ancestry", std::make_shared<ArrayExpr>(std::move(ancestryValues))},
        {"structuralEvidence", evidence->clone()},
        {"relationships", std::make_shared<ArrayExpr>(std::move(relationshipValues))},
        {"dependencyStatus", std::make_shared<StringExpr>("satisfied")}});
    context->factType = "ComparisonContext";

    if (!comparisonClause) {
        if (exact) out = buildResult("exact-member", membership, evidence);
        else if (subset) out = buildResult("subset-member", membership, evidence);
        else if (overlap > 0.0) out = buildResult("partial-member", membership, evidence);
        else if (!std::dynamic_pointer_cast<ArrayExpr>(findMapValue(evidence, "conflictingFields"))->items.empty()) {
            out = buildResult("conflicting", membership, evidence);
        } else out = buildResult("unknown", membership, evidence);
        return true;
    }

    Call comparisonCall(comparisonName, {{"context", context}});
    std::shared_ptr<Expr> custom;
    if (!invokeComparisonMethod(comparisonClause, comparisonCall, env, custom)) {
        throw InterpreterError("Selected target comparison method '" + comparisonName + "' produced no result");
    }
    const auto customMap = std::dynamic_pointer_cast<MapExpr>(custom);
    if (!customMap) throw InterpreterError("compareMembership must return a structured fact");
    std::vector<MapEntry> completed = cloneEntries(customMap->entries);
    upsertEntry(completed, internalSymbolString(InternalSymbolKind::Type), std::make_shared<StringExpr>("Comparison"));
    if (!findMapValue(custom, "state")) throw InterpreterError("compareMembership result requires a 'state' field");
    if (!findMapValue(custom, "source")) upsertEntry(completed, "source", source->clone());
    if (!findMapValue(custom, "target")) upsertEntry(completed, "target", target->clone());
    if (!findMapValue(custom, "targetFamily")) upsertEntry(completed, "targetFamily", std::make_shared<StringExpr>(targetFamily));
    if (!findMapValue(custom, "membership")) upsertEntry(completed, "membership", membership->clone());
    if (!findMapValue(custom, "evidence")) upsertEntry(completed, "evidence", evidence->clone());
    auto result = std::make_shared<MapExpr>(std::move(completed));
    result->factType = "Comparison";
    out = std::move(result);
    return true;
}

bool Interpreter::solveBuiltin(const Call& call, Env& env) {
    if (call.builtinId == BuiltinId::CommonAncestors ||
        call.builtinId == BuiltinId::LowestCommonAncestor ||
        call.builtinId == BuiltinId::HighestCommonAncestor ||
        call.builtinId == BuiltinId::AncestorAnalysis ||
        call.builtinId == BuiltinId::PropagateFact) {
        std::shared_ptr<Expr> result;
        const bool evaluated = call.builtinId == BuiltinId::PropagateFact
            ? evalFactPropagation(call, env, result)
            : evalAncestorAnalysis(call, env, result);
        if (!evaluated) return false;
        env[InternalSymbol::ReturnId] = result;
        for (const auto& argument : call.args) {
            if ((argument.name == "result" || argument.name == "out") &&
                !unifyExpr(argument.value, result, env)) return false;
        }
        return true;
    }
    if (call.builtinId == BuiltinId::ReasoningContrary ||
        call.builtinId == BuiltinId::ReasoningProve ||
        call.builtinId == BuiltinId::ReasoningGrade ||
        call.builtinId == BuiltinId::ReasoningDecide) {
        TermExpr term(call.name, {}, call.builtinId);
        term.nameId = call.nameId;
        for (const auto& argument : call.args) {
            term.args.emplace_back(
                argument.name,
                argument.value ? argument.value->clone()
                               : std::make_shared<NilExpr>());
        }
        std::shared_ptr<Expr> result;
        if (!evalReasoningBuiltin(term, env, result)) return false;
        env[InternalSymbol::ReturnId] = result;
        for (const auto& argument : call.args) {
            if ((argument.name == "result" ||
                 argument.name == "out") &&
                !unifyExpr(argument.value, result, env)) return false;
        }
        return true;
    }
    if (call.builtinId == BuiltinId::RelationCompare) {
        std::shared_ptr<Expr> result;
        if (!evalRelationCompare(call, env, result)) return false;
        env[internalSymbolString(InternalSymbolKind::Return)] = result;
        for (const auto& arg : call.args) {
            if ((arg.name == "result" || arg.name == "out") && !unifyExpr(arg.value, result, env)) return false;
        }
        return true;
    }
    if (call.builtinId == BuiltinId::FactReferences) {
        std::shared_ptr<Expr> result;
        if (!evalFactReferences(call, env, result)) return false;
        env[internalSymbolString(InternalSymbolKind::Return)] = result;
        for (const auto& arg : call.args) {
            if ((arg.name == "result" || arg.name == "out") && !unifyExpr(arg.value, result, env)) return false;
        }
        return true;
    }
    if (solveFactAttachment(call, env)) return true;
    const ClauseStmt* nativeDeclaration = nativeDeclarationFor(call.name);
    if (!nativeDeclaration) {
        ensurePredicateLoaded(call.name);
        nativeDeclaration = nativeDeclarationFor(call.name);
    }
    if (nativeDeclaration) {
        Env nativeEnv = env;
        if (solveNativeCall(call, nativeEnv)) {
            env = std::move(nativeEnv);
            return true;
        }
    }

    auto outArg = [&](const std::string& name, size_t index) -> const Arg* {
        return findArgByNameOrIndex(call, name, index);
    };
    auto namedArg = [&](const std::string& name) -> const Arg* {
        for (const auto& arg : call.args) {
            if (arg.name == name) return &arg;
        }
        return nullptr;
    };
    auto valueArg = [&](std::initializer_list<const char*> names, size_t index, std::shared_ptr<Expr>& out) -> bool {
        const Arg* arg = nullptr;
        for (const char* name : names) {
            arg = namedArg(name);
            if (arg) break;
        }
        if (!arg && index < call.args.size() && call.args[index].name.empty()) arg = &call.args[index];
        return arg && evalExprValue(arg->value, env, out);
    };

    std::shared_ptr<Expr> a;
    std::shared_ptr<Expr> b;
    std::shared_ptr<Expr> c;
    std::shared_ptr<Expr> result;

    auto callNativeValueBuiltin = [&]() -> bool {
        TermExpr term(call.name, {}, call.builtinId);
        for (const auto& arg : call.args) {
            if (arg.name == "result" || arg.name == "out" || arg.name == "access") {
                continue;
            }
            term.args.push_back(Arg{arg.name, arg.value});
        }
        std::shared_ptr<Expr> value;
        if (!evalCallAsValue(term, env, value)) return false;
        const Arg* out = namedArg("result");
        if (!out) out = namedArg("out");
        if (!out) out = namedArg("access");
        if (!out) out = namedArg("equals");
        if (!out) return true;
        return unifyExpr(out->value, value, env);
    };

    const bool mathPredicateBuiltin =
        call.builtinId == BuiltinId::MathAdd ||
        call.builtinId == BuiltinId::MathSub ||
        call.builtinId == BuiltinId::MathMul ||
        call.builtinId == BuiltinId::MathDiv ||
        call.builtinId == BuiltinId::MathMod;

    const bool stringPredicateBuiltin =
        call.builtinId == BuiltinId::StrLen ||
        call.builtinId == BuiltinId::StrContains ||
        call.builtinId == BuiltinId::StrConcat ||
        call.builtinId == BuiltinId::StrJoin ||
        call.builtinId == BuiltinId::StrLower ||
        call.builtinId == BuiltinId::StrUpper ||
        call.builtinId == BuiltinId::StrTrim ||
        call.builtinId == BuiltinId::StrSplit ||
        call.builtinId == BuiltinId::StrReplace ||
        call.builtinId == BuiltinId::StrStartsWith ||
        call.builtinId == BuiltinId::StrEndsWith;
    const bool arrayPredicateBuiltin =
        call.builtinId == BuiltinId::ArrayGet ||
        call.builtinId == BuiltinId::ArrayLen ||
        call.builtinId == BuiltinId::ArrayPush;
    const bool pairPredicateBuiltin =
        call.builtinId == BuiltinId::PairFirst ||
        call.builtinId == BuiltinId::PairSecond;
    const bool jsonPredicateBuiltin =
        call.builtinId == BuiltinId::JsonParse ||
        call.builtinId == BuiltinId::JsonGet ||
        call.builtinId == BuiltinId::JsonHas ||
        call.builtinId == BuiltinId::JsonKeys ||
        call.builtinId == BuiltinId::JsonSet ||
        call.builtinId == BuiltinId::JsonRemove ||
        call.builtinId == BuiltinId::JsonToText;

    if (call.builtinId != BuiltinId::Unknown &&
        call.builtinId != BuiltinId::Throw &&
        call.builtinId != BuiltinId::Type &&
        call.builtinId != BuiltinId::Instanceof &&
        !mathPredicateBuiltin &&
        !stringPredicateBuiltin &&
        !arrayPredicateBuiltin &&
        !pairPredicateBuiltin &&
        !jsonPredicateBuiltin) {
        return callNativeValueBuiltin();
    }

    if (call.builtinId == BuiltinId::Throw) {
        if (!valueArg({"exception"}, 0, a)) {
            throw InterpreterError("throw expects a resolvable typed 'exception'");
        }
        auto exceptionMap = std::dynamic_pointer_cast<MapExpr>(a);
        auto exceptionKind = findMapValue(a, "kind");
        auto kindString = std::dynamic_pointer_cast<StringExpr>(exceptionKind);
        if (!exceptionMap || !kindString) {
            throw InterpreterError(
                "throw exception must be an object with a string 'kind' field");
        }
        env["error_reason"] = exceptionKind->clone();

        const Arg* target = namedArg("target");
        if (target) {
            std::string targetName;
            if (auto targetVar = std::dynamic_pointer_cast<VarExpr>(target->value)) {
                targetName = targetVar->name;
            } else {
                throw InterpreterError(
                    "throw target must be a callable reference such as someFunction::Function");
            }
            const auto separator = targetName.find("::");
            if (separator == std::string::npos ||
                targetName.find("::", separator + 2) != std::string::npos) {
                throw InterpreterError(
                    "throw target must be a callable reference such as someFunction::Function");
            }
            targetName.replace(separator, 2, ":");

            Call handler(targetName, {});
            handler.args.push_back(Arg{"exception", a});

            auto* clauses = findClauses(handler.name, handler.nameId);
            if (!clauses && ensurePredicateLoaded(handler.name)) {
                clauses = findClauses(handler.name, handler.nameId);
            }
            if (!clauses) {
                throw InterpreterError("Exception handler not found: " + targetName);
            }
            std::vector<Solution> handled;
            solveRecursive(
                {std::make_shared<CallGoal>(std::move(handler))},
                env,
                handled,
                1,
                0);
            if (handled.empty()) return false;
            env = std::move(handled.front().env);
        }

        const Arg* out = namedArg("out");
        if (!out && call.args.size() > 1 && call.args[1].name.empty()) out = &call.args[1];
        if (out) return unifyExpr(out->value, a, env);
        return true;
    }

    if (call.builtinId == BuiltinId::Type) {
        if (!valueArg({"value", "data", "input"}, 0, a)) return false;
        std::string resolvedType;
        if (auto ast = std::dynamic_pointer_cast<AstValueExpr>(a)) resolvedType = ast->nodeKind;
        else if (auto typeString = std::dynamic_pointer_cast<StringExpr>(
                     findMapValue(a, internalSymbolString(InternalSymbolKind::Type)))) {
            resolvedType = typeString->value;
        } else if (std::dynamic_pointer_cast<NumberExpr>(a)) resolvedType = "number";
        else if (std::dynamic_pointer_cast<StringExpr>(a)) resolvedType = "string";
        else if (std::dynamic_pointer_cast<BoolExpr>(a)) resolvedType = "bool";
        else if (std::dynamic_pointer_cast<ArrayExpr>(a)) resolvedType = "array";
        else if (std::dynamic_pointer_cast<NilExpr>(a)) resolvedType = "nil";
        else return false;
        const Arg* out = namedArg("name");
        if (!out) out = namedArg("type");
        if (!out) out = namedArg("out");
        if (!out) out = outArg("out", 1);
        return out && unifyExpr(out->value, std::make_shared<StringExpr>(resolvedType), env);
    }

    if (call.builtinId == BuiltinId::Instanceof) {
        if (!valueArg({"value", "data", "input"}, 0, a)) return false;
        const Arg* typeArg = namedArg("type");
        if (!typeArg) typeArg = namedArg("parent");
        if (!typeArg) typeArg = namedArg("of");
        if (!typeArg && call.args.size() > 1) typeArg = &call.args[1];
        if (!typeArg) return false;
        auto typeValue = findMapValue(a, internalSymbolString(InternalSymbolKind::Type));
        auto typeString = std::dynamic_pointer_cast<StringExpr>(typeValue);
        if (!typeString) return false;
        std::string expected;
        if (auto expectedString = std::dynamic_pointer_cast<StringExpr>(typeArg->value)) {
            expected = expectedString->value;
        } else if (auto expectedVar = std::dynamic_pointer_cast<VarExpr>(typeArg->value)) {
            expected = expectedVar->name;
        } else {
            return false;
        }
        return memory_.isCompatibleType(typeString->value, expected);
    }

    if (mathPredicateBuiltin) {
        if (!valueArg({"lhs", "left"}, 0, a) || !valueArg({"rhs", "right"}, 1, b)) return false;
        double left = 0.0;
        double right = 0.0;
        if (!argAsNumber(a, left) || !argAsNumber(b, right)) return false;
        switch (call.builtinId) {
            case BuiltinId::MathAdd:
                result = std::make_shared<NumberExpr>(left + right);
                break;
            case BuiltinId::MathSub:
                result = std::make_shared<NumberExpr>(left - right);
                break;
            case BuiltinId::MathMul:
                result = std::make_shared<NumberExpr>(left * right);
                break;
            case BuiltinId::MathDiv:
                if (std::fabs(right) < 1e-12) throw InterpreterError("DivisionByZero");
                result = std::make_shared<NumberExpr>(left / right);
                break;
            case BuiltinId::MathMod:
                if (std::fabs(right) < 1e-12) throw InterpreterError("DivisionByZero");
                result = std::make_shared<NumberExpr>(std::fmod(left, right));
                break;
            default:
                return false;
        }
        const Arg* out = namedArg("result");
        if (!out) out = outArg("out", 2);
        return out && unifyExpr(out->value, result, env);
    }

    if (stringPredicateBuiltin) {
        if (!valueArg({"left", "data", "value"}, 0, a)) return false;
        if (call.builtinId == BuiltinId::StrJoin) {
            std::vector<std::shared_ptr<Expr>> items;
            if (!exprAsArrayItems(a, items)) return false;
            std::string delimiter;
            if (!valueArg({"delimiter", "separator"}, 1, b) || !argAsString(b, delimiter)) return false;
            std::ostringstream joined;
            for (size_t i = 0; i < items.size(); ++i) {
                if (i) joined << delimiter;
                std::string itemText;
                if (argAsString(items[i], itemText)) joined << itemText;
                else joined << valueToString(items[i]);
            }
            result = std::make_shared<StringExpr>(joined.str());
            const Arg* out = namedArg("result");
            if (!out) out = outArg("out", 2);
            return out && unifyExpr(out->value, result, env);
        }
        std::string text;
        if (!argAsString(a, text)) return false;
        if (call.builtinId == BuiltinId::StrLen) {
            result = std::make_shared<NumberExpr>(static_cast<double>(text.size()));
            const Arg* out = namedArg("equals");
            if (!out) out = outArg("out", 1);
            return out && unifyExpr(out->value, result, env);
        }
        if (call.builtinId == BuiltinId::StrContains) {
            if (!valueArg({"needle"}, 1, b)) return false;
            std::string needle;
            if (!argAsString(b, needle)) return false;
            result = std::make_shared<BoolExpr>(
                text.find(needle) != std::string::npos);
            const Arg* out = namedArg("access");
            if (!out) out = outArg("out", 2);
            return out ? unifyExpr(out->value, result, env) : text.find(needle) != std::string::npos;
        }
        if (call.builtinId == BuiltinId::StrConcat) {
            if (!valueArg({"right", "rhs"}, 1, b)) return false;
            std::string right;
            if (!argAsString(b, right)) return false;
            result = std::make_shared<StringExpr>(text + right);
            const Arg* out = namedArg("result");
            if (!out) out = outArg("out", 2);
            return out && unifyExpr(out->value, result, env);
        }
        if (call.builtinId == BuiltinId::StrTrim) {
            result = std::make_shared<StringExpr>(Felidae::trim(text));
            const Arg* out = namedArg("access");
            if (!out) out = outArg("out", 1);
            return out && unifyExpr(out->value, result, env);
        }
        if (call.builtinId == BuiltinId::StrSplit) {
            if (!valueArg({"delimiter", "separator"}, 1, b)) return false;
            std::string delimiter;
            if (!argAsString(b, delimiter) || delimiter.empty()) return false;
            std::vector<std::shared_ptr<Expr>> parts;
            size_t start = 0;
            while (true) {
                size_t pos = text.find(delimiter, start);
                parts.push_back(std::make_shared<StringExpr>(text.substr(start, pos == std::string::npos ? pos : pos - start)));
                if (pos == std::string::npos) break;
                start = pos + delimiter.size();
            }
            const Arg* out = namedArg("access");
            if (!out) out = outArg("out", 2);
            return out && unifyExpr(out->value, std::make_shared<ArrayExpr>(std::move(parts)), env);
        }
        if (call.builtinId == BuiltinId::StrReplace) {
            if (!valueArg({"search", "needle"}, 1, b)) return false;
            std::shared_ptr<Expr> replacementValue;
            const Arg* replacementArg = namedArg("replacement");
            if (!replacementArg) replacementArg = namedArg("with");
            if (!replacementArg || !evalExprValue(replacementArg->value, env, replacementValue)) return false;
            std::string search;
            std::string replacement;
            if (!argAsString(b, search) || !argAsString(replacementValue, replacement) || search.empty()) return false;
            size_t pos = 0;
            while ((pos = text.find(search, pos)) != std::string::npos) {
                text.replace(pos, search.size(), replacement);
                pos += replacement.size();
            }
            const Arg* out = namedArg("access");
            if (!out) out = outArg("out", 3);
            return out && unifyExpr(out->value, std::make_shared<StringExpr>(text), env);
        }
        if (call.builtinId == BuiltinId::StrStartsWith ||
            call.builtinId == BuiltinId::StrEndsWith) {
            const bool startsWith = call.builtinId == BuiltinId::StrStartsWith;
            if (!valueArg({startsWith ? "prefix" : "suffix"}, 1, b)) return false;
            std::string needle;
            if (!argAsString(b, needle)) return false;
            bool ok = startsWith
                ? text.rfind(needle, 0) == 0
                : (needle.size() <= text.size() && text.compare(text.size() - needle.size(), needle.size(), needle) == 0);
            const Arg* out = namedArg("access");
            if (!out) out = outArg("out", 2);
            return out && unifyExpr(
                out->value, std::make_shared<BoolExpr>(ok), env);
        }
        for (char& ch : text) {
            ch = static_cast<char>(call.builtinId == BuiltinId::StrLower
                ? std::tolower(static_cast<unsigned char>(ch))
                : std::toupper(static_cast<unsigned char>(ch)));
        }
        result = std::make_shared<StringExpr>(text);
        const Arg* out = namedArg("equals");
        if (!out) out = outArg("out", 1);
        return out && unifyExpr(out->value, result, env);
    }

    if (arrayPredicateBuiltin) {
        if (!valueArg({"data", "array"}, 0, a)) return false;
        auto args = termArgs(a, BuiltinId::FnArray);
        auto arrayTerm = std::dynamic_pointer_cast<TermExpr>(a);
        if (args.empty() &&
            !(arrayTerm && arrayTerm->builtinId == BuiltinId::FnArray) &&
            !std::dynamic_pointer_cast<ArrayExpr>(a)) {
            return false;
        }
        if (call.builtinId == BuiltinId::ArrayLen) {
            const Arg* out = namedArg("access");
            if (!out) out = outArg("out", 1);
            return out && unifyExpr(out->value, std::make_shared<NumberExpr>(static_cast<double>(args.size())), env);
        }
        if (call.builtinId == BuiltinId::ArrayGet) {
            if (!valueArg({"position", "index"}, 1, b)) return false;
            double index = 0.0;
            if (!argAsNumber(b, index) || index < 0 || std::floor(index) != index) return false;
            size_t i = static_cast<size_t>(index);
            if (i >= args.size()) return false;
            const Arg* out = namedArg("access");
            if (!out) out = outArg("out", 2);
            return out && unifyExpr(out->value, args[i], env);
        }
        if (call.builtinId == BuiltinId::ArrayPush) {
            if (!valueArg({"value"}, 1, b)) return false;
            args.push_back(b);
            const Arg* out = namedArg("result");
            if (!out) out = outArg("out", 2);
            return out && unifyExpr(out->value, std::make_shared<ArrayExpr>(std::move(args)), env);
        }
    }

    if (pairPredicateBuiltin) {
        if (!valueArg({"data", "pair"}, 0, a)) return false;
        auto args = termArgs(a, BuiltinId::FnPair);
        if (args.size() != 2) return false;
        const Arg* out = namedArg("access");
        if (!out) out = outArg("out", 1);
        return out && unifyExpr(out->value, call.builtinId == BuiltinId::PairFirst ? args[0] : args[1], env);
    }

    if (call.builtinId == BuiltinId::JsonParse) {
        if (!valueArg({"data", "value"}, 0, a)) return false;
        std::string jsonText;
        if (!argAsString(a, jsonText)) return false;
        std::shared_ptr<Expr> parsed;
        if (!parseFlatJsonObject(jsonText, parsed)) return false;
        const Arg* out = namedArg("access");
        if (!out) out = outArg("out", 1);
        return out && unifyExpr(out->value, parsed, env);
    }

    if (call.builtinId == BuiltinId::JsonGet) {
        if (!valueArg({"data", "object"}, 0, a) || !valueArg({"key"}, 1, b)) return false;
        std::string key;
        if (!argAsString(b, key)) return false;
        auto value = findMapValue(a, key);
        const Arg* out = namedArg("access");
        if (!out) out = outArg("out", 2);
        if (value && out) return unifyExpr(out->value, value, env);
        return false;
    }

    if (call.builtinId == BuiltinId::JsonHas ||
        call.builtinId == BuiltinId::JsonKeys ||
        call.builtinId == BuiltinId::JsonSet ||
        call.builtinId == BuiltinId::JsonRemove) {
        if (!valueArg({"data", "object"}, 0, a)) return false;
        std::vector<MapEntry> entries;
        if (!exprAsMapEntries(a, entries)) return false;
        std::shared_ptr<Expr> resultValue;
        if (call.builtinId == BuiltinId::JsonKeys) {
            std::vector<std::shared_ptr<Expr>> keys;
            keys.reserve(entries.size());
            for (const auto& entry : entries) keys.push_back(std::make_shared<StringExpr>(entry.key));
            resultValue = std::make_shared<ArrayExpr>(std::move(keys));
            const Arg* out = namedArg("access");
            if (!out) out = outArg("out", 1);
            return out && unifyExpr(out->value, resultValue, env);
        }
        if (!valueArg({"key"}, 1, b)) return false;
        std::string key;
        if (!argAsString(b, key)) return false;
        if (call.builtinId == BuiltinId::JsonHas) {
            resultValue = std::make_shared<BoolExpr>(
                static_cast<bool>(findMapValue(a, key)));
            const Arg* out = namedArg("access");
            if (!out) out = outArg("out", 2);
            return out && unifyExpr(out->value, resultValue, env);
        }
        if (call.builtinId == BuiltinId::JsonSet) {
            std::shared_ptr<Expr> value;
            const Arg* valueArgPtr = namedArg("value");
            if (!valueArgPtr || !evalExprValue(valueArgPtr->value, env, value)) return false;
            upsertEntry(entries, key, cloneExprOrNil(value));
        } else {
            removeEntry(entries, key);
        }
        const Arg* out = namedArg("access");
        if (!out) out = outArg("out", call.builtinId == BuiltinId::JsonSet ? 3 : 2);
        return out && unifyExpr(out->value, std::make_shared<MapExpr>(std::move(entries)), env);
    }

    if (call.builtinId == BuiltinId::JsonToText) {
        if (!valueArg({"data", "value"}, 0, a)) return false;
        const Arg* out = namedArg("access");
        if (!out) out = outArg("out", 1);
        return out && unifyExpr(out->value, std::make_shared<StringExpr>(exprToJson(a)), env);
    }

    return false;
}

const ClauseStmt* Interpreter::nativeDeclarationFor(const std::string& name) const {
    auto clauses = findClauses(name, symbolIdForName(name));
    if (!clauses) return nullptr;
    for (const auto& clause : *clauses) {
        if (clause && clause->emptyDeclaration) return clause.get();
    }
    return nullptr;
}

void Interpreter::validateNativeCallTypes(const Call& call,
                                          const ClauseStmt& declaration,
                                          const Env& env,
                                          bool requireDeclaredInputs) {
    auto isOutputName = [](const std::string& name) {
        return name == "access" || name == "out" || name == "result" || name == "equals";
    };
    if (requireDeclaredInputs) {
        for (size_t providedIndex = 0; providedIndex < call.args.size(); ++providedIndex) {
            const Arg& provided = call.args[providedIndex];
            if (provided.name.empty()) continue;
            bool declaredName = false;
            for (const auto& declared : declaration.head.args) {
                if (declared.name == provided.name) {
                    declaredName = true;
                    break;
                }
            }
            if (!declaredName) {
                throw InterpreterError(call.name + " does not declare argument '" + provided.name + "'");
            }
        }
    }

    for (size_t i = 0; i < declaration.head.args.size(); ++i) {
        const Arg& declared = declaration.head.args[i];
        const Arg* provided = findArg(call, declared, i);
        auto typeName = std::dynamic_pointer_cast<VarExpr>(declared.value);
        if (!provided) {
            if (requireDeclaredInputs && typeName && isFelidaeBuiltinTypeName(typeName->name) && !isOutputName(declared.name)) {
                throw InterpreterError(call.name + " expects argument '" +
                                       (declared.name.empty() ? std::to_string(i) : declared.name) +
                                       "' before calling native library");
            }
            continue;
        }
        if (!typeName || !isFelidaeBuiltinTypeName(typeName->name)) continue;

        std::shared_ptr<Expr> value;
        if (!evalExprValue(provided->value, env, value)) {
            if (std::dynamic_pointer_cast<VarExpr>(provided->value)) continue;
            throw InterpreterError(call.name + " cannot evaluate argument '" +
                                   (declared.name.empty() ? std::to_string(i) : declared.name) +
                                   "' before native type checking");
        }
        if (!valueMatchesBuiltinType(value, typeName->name)) {
            throw InterpreterError(call.name + " expects argument '" +
                                   (declared.name.empty() ? std::to_string(i) : declared.name) +
                                   "' to be " + typeName->name + " before calling native library");
        }
    }
}

bool Interpreter::solveNativeCall(const Call& call, Env& env) {
    const ClauseStmt* declaration = nativeDeclarationFor(call.name);
    if (!declaration) return false;
    const bool systemLibraryCall = call.name.rfind("system:flibrary:", 0) == 0;
    if (!systemLibraryCall && call.builtinId != BuiltinId::Unknown) return false;
    const bool genericLibraryLoader = call.name == "system:flibrary:call";

    std::string moduleName = call.name;
    size_t sep = moduleName.find(':');
    if (sep != std::string::npos) moduleName = moduleName.substr(0, sep);
    if (systemLibraryCall) {
        size_t moduleStart = std::string("system:flibrary:").size();
        size_t moduleEnd = call.name.find(':', moduleStart);
        moduleName = call.name.substr(moduleStart, moduleEnd == std::string::npos ? std::string::npos : moduleEnd - moduleStart);
    }
    std::string nativeFunctionName = call.name;
    std::shared_ptr<MapExpr> loaderArgs;
    if (genericLibraryLoader) {
        const Arg* moduleArg = findArgByNameOrIndex(call, "module", 0);
        const Arg* functionArg = findArgByNameOrIndex(call, "function", 1);
        const Arg* argsArg = findArgByNameOrIndex(call, "args", 2);
        if (!moduleArg || !functionArg || !argsArg) {
            throw InterpreterError("system.flibrary.call expects module, function, and args");
        }
        std::shared_ptr<Expr> moduleValue;
        std::shared_ptr<Expr> functionValue;
        std::shared_ptr<Expr> argsValue;
        if (!evalExprValue(moduleArg->value, env, moduleValue) ||
            !evalExprValue(functionArg->value, env, functionValue) ||
            !evalExprValue(argsArg->value, env, argsValue)) {
            return false;
        }
        if (!argAsString(moduleValue, moduleName) || moduleName.empty()) {
            throw InterpreterError("system.flibrary.call expects string argument 'module'");
        }
        std::string functionName;
        if (!argAsString(functionValue, functionName) || functionName.empty()) {
            throw InterpreterError("system.flibrary.call expects string argument 'function'");
        }
        loaderArgs = std::dynamic_pointer_cast<MapExpr>(argsValue);
        if (!loaderArgs) {
            throw InterpreterError("system.flibrary.call expects map argument 'args'");
        }
        nativeFunctionName = "system:flibrary:" + moduleName + ":" + functionName;
        Call targetCall(nativeFunctionName, {});
        for (const auto& entry : loaderArgs->entries) {
            targetCall.args.push_back(Arg{entry.key, entry.value->clone()});
        }
        auto* targetClauses = findClauses(targetCall.name, targetCall.nameId);
        if (targetClauses) {
            bool matchedDeclaration = false;
            std::string firstMismatch;
            for (const auto& targetDeclaration : *targetClauses) {
                if (!targetDeclaration || !targetDeclaration->emptyDeclaration) continue;
                try {
                    validateNativeCallTypes(targetCall, *targetDeclaration, env, true);
                    matchedDeclaration = true;
                    break;
                } catch (const InterpreterError& error) {
                    if (firstMismatch.empty()) firstMismatch = error.what();
                }
            }
            if (!matchedDeclaration) {
                throw InterpreterError(firstMismatch.empty()
                    ? ("No native declaration overload matches " + nativeFunctionName)
                    : firstMismatch);
            }
        }
    }

    const NativeLibrary* library = nullptr;
    for (const auto& candidate : nativeLibraries_) {
        if (candidate.moduleName == moduleName) {
            library = &candidate;
            break;
        }
    }
    if (!library) {
        fs::path baseDir = fs::current_path();
        auto origin = clauseOrigins_.find(declaration);
        if (origin != clauseOrigins_.end()) baseDir = origin->second.parent_path();
        fs::path nativeFile = resolveNativeImport(baseDir, moduleName);
        if (nativeFile.empty()) {
            fs::path root = sourceRootFromBase(baseDir);
            const bool nativeModuleExpected =
                systemLibraryCall ||
                fs::exists(root / "native_modules" / moduleName) ||
                fs::exists(root / "modules" / moduleName);
            if (!nativeModuleExpected) return false;
            throw InterpreterError("Native module library for '" + moduleName +
                                   "' not found while executing '" + call.name + "'");
        }
        loadNativeLibrary(nativeFile);
        for (const auto& candidate : nativeLibraries_) {
            if (candidate.moduleName == moduleName) {
                library = &candidate;
                break;
            }
        }
        if (!library) {
            throw InterpreterError("Native module library '" + nativeFile.string() +
                                   "' did not register as module '" + moduleName + "'");
        }
    }

    validateNativeCallTypes(call, *declaration, env);
    const NativeContract& nativeContract =
        library->manifest.contractFor(nativeFunctionName);
    if (!nativeContract.argumentConstraints.empty()) {
        auto valueFor = [&](const std::string& name, size_t index) -> std::shared_ptr<Expr> {
            if (genericLibraryLoader && loaderArgs) {
                for (const auto& entry : loaderArgs->entries) {
                    if (entry.key == name) {
                        std::shared_ptr<Expr> value;
                        return evalExprValue(entry.value, env, value) ? value : nullptr;
                    }
                }
                return {};
            }
            const Arg* arg = findArgByNameOrIndex(call, name, index);
            if (!arg) return {};
            std::shared_ptr<Expr> value;
            if (!evalExprValue(arg->value, env, value)) return {};
            return value;
        };
        auto numberValueFor = [&](const std::string& name, size_t index, double& out) -> bool {
            auto value = valueFor(name, index);
            if (!value) return false;
            auto number = std::dynamic_pointer_cast<NumberExpr>(value);
            if (!number) return false;
            out = number->value;
            return true;
        };
        auto validateConstraint = [&](const NativeArgumentConstraint& constraint) {
            const auto value = valueFor(constraint.name, constraint.positionalIndex);
            if (!value) {
                if (constraint.required) {
                    throw InterpreterError("FactConfigError: missing '" + constraint.name + "' before calling native library");
                }
                return;
            }
            switch (constraint.kind) {
                case NativeArgumentConstraintKind::StringOption: {
                    std::string text;
                    if (!argAsString(value, text)) {
                        throw InterpreterError("FactConfigError: '" + constraint.name + "' expects a string before calling native library");
                    }
                    if (std::find(constraint.allowedValues.begin(), constraint.allowedValues.end(), text) ==
                        constraint.allowedValues.end()) {
                        throw InterpreterError("FactConfigError: unsupported " + constraint.name + " '" + text + "' before calling native library");
                    }
                    return;
                }
                case NativeArgumentConstraintKind::UnitInterval:
                case NativeArgumentConstraintKind::PositiveFinite: {
                    double number = 0.0;
                    const bool valid = numberValueFor(constraint.name, constraint.positionalIndex, number) &&
                        std::isfinite(number) &&
                        (constraint.kind != NativeArgumentConstraintKind::UnitInterval ||
                         (number >= 0.0 && number <= 1.0)) &&
                        (constraint.kind != NativeArgumentConstraintKind::PositiveFinite || number >= 1.0);
                    if (!valid) {
                        throw InterpreterError(constraint.kind == NativeArgumentConstraintKind::UnitInterval
                            ? ("FactConfigError: '" + constraint.name + "' must be between 0 and 1 before calling native library")
                            : ("FactConfigError: '" + constraint.name + "' must be a positive finite number before calling native library"));
                    }
                    return;
                }
                case NativeArgumentConstraintKind::StringArray: {
                    const auto array = std::dynamic_pointer_cast<ArrayExpr>(value);
                    if (!array) {
                        throw InterpreterError("FactConfigError: '" + constraint.name + "' expects an array of strings before calling native library");
                    }
                    for (const auto& item : array->items) {
                        const auto text = std::dynamic_pointer_cast<StringExpr>(item);
                        if (!text || text->value.empty()) {
                            throw InterpreterError("FactConfigError: '" + constraint.name + "' expects non-empty string field names before calling native library");
                        }
                    }
                    return;
                }
                case NativeArgumentConstraintKind::BooleanText: {
                    if (std::dynamic_pointer_cast<BoolExpr>(value)) return;
                    std::string text;
                    if (!argAsString(value, text) || (text != "true" && text != "false")) {
                        throw InterpreterError("FactConfigError: '" + constraint.name + "' expects \"true\" or \"false\" before calling native library");
                    }
                    return;
                }
            }
        };
        for (const auto& constraint : nativeContract.argumentConstraints) {
            validateConstraint(constraint);
        }

        #if 0 // Replaced by native manifest argument_constraints.
        if (false) {
            requireOption("lexical_algorithm", 3, {"path", "wup", "wu_palmer", "Wu-Palmer", "Wu Palmer", "resnik", "jiang_conrath", "Jiang-Conrath", "Jiang Conrath", "lin", "edit", "Leacock-Chodorow", "Leacock–Chodorow", "Leacock Chodorow", "leacock_chodorow", "lch"}, false);
        }
    }

        #endif
    }
    const auto serializationStart = std::chrono::steady_clock::now();
    std::ostringstream json;
    json << "{";
    bool first = true;
    std::vector<const Arg*> outputArgs;
    const bool selectionAwareNative = nativeContract.capabilities.acceptsFactSelections;
    std::function<std::shared_ptr<Expr>(const std::shared_ptr<Expr>&)> materializeNativeValue;
    materializeNativeValue = [&](const std::shared_ptr<Expr>& value) -> std::shared_ptr<Expr> {
        if (!selectionAwareNative || !value) return value;
        const auto kind = std::dynamic_pointer_cast<StringExpr>(
            findMapValue(value, internalSymbolString(InternalSymbolKind::Type)));
        if (kind && kind->value == "FactSelection") {
            return materializeFactSelection(value);
        }
        if (auto array = std::dynamic_pointer_cast<ArrayExpr>(value)) {
            std::vector<std::shared_ptr<Expr>> items;
            items.reserve(array->items.size());
            for (const auto& item : array->items) items.push_back(materializeNativeValue(item));
            return std::make_shared<ArrayExpr>(std::move(items));
        }
        return value;
    };
    if (genericLibraryLoader) {
        for (const auto& entry : loaderArgs->entries) {
            std::shared_ptr<Expr> value;
            if (!evalExprValue(entry.value, env, value)) return false;
            if (!first) json << ",";
            first = false;
            json << "\"" << jsonEscape(entry.key) << "\":"
                 << exprToJson(materializeNativeValue(value));
        }
    } else {
        for (size_t i = 0; i < call.args.size(); ++i) {
            const Arg& arg = call.args[i];
            std::shared_ptr<Expr> value;
            if (!evalExprValue(arg.value, env, value)) {
                if (std::dynamic_pointer_cast<VarExpr>(arg.value)) {
                    outputArgs.push_back(&arg);
                    continue;
                }
                return false;
            }
            value = materializeNativeValue(value);
            if (!first) json << ",";
            first = false;
            std::string key = arg.name.empty() ? ("arg" + std::to_string(i)) : arg.name;
            json << "\"" << jsonEscape(key) << "\":" << exprToJson(value);
        }
    }
    const NativeCapabilities& capabilities = nativeContract.capabilities;
    bool callerProvidedFacts = false;
    if (genericLibraryLoader && loaderArgs) {
        callerProvidedFacts = std::any_of(
            loaderArgs->entries.begin(), loaderArgs->entries.end(),
            [](const MapEntry& entry) { return entry.key == "facts"; });
    } else {
        callerProvidedFacts = std::any_of(
            call.args.begin(), call.args.end(),
            [](const Arg& arg) { return arg.name == "facts"; });
    }
    if (capabilities.needsFactProjection && !callerProvidedFacts) {
        if (!first) json << ",";
        first = false;
        const std::streampos projectionStart = json.tellp();
        json << "\"__facts\":[";
        bool firstFact = true;
        std::size_t projectedRows = 0;
        std::unordered_set<std::size_t> projectedIndexes;
        for (const auto& requestedType : capabilities.requestedFactTypes) {
            for (const size_t factIndex :
                 memory_.selectionIndexes(requestedType)) {
                if (!projectedIndexes.insert(factIndex).second) continue;
                if (projectedRows >= capabilities.maximumProjectedRows) {
                    throw InterpreterError(
                        "NativeProjectionLimit: native function '" +
                        nativeFunctionName + "' requested more than " +
                        std::to_string(capabilities.maximumProjectedRows) +
                        " fact rows");
                }
                const auto value = memory_.factValue(factIndex);
                if (!value) continue;
                std::shared_ptr<MapExpr> projected = value;
                if (!capabilities.requestedFactFields.empty()) {
                    std::vector<MapEntry> fields;
                    fields.reserve(
                        capabilities.requestedFactFields.size() + 1);
                    for (const auto& entry : value->entries) {
                        if (entry.key ==
                                internalSymbolString(
                                    InternalSymbolKind::Type) ||
                            std::find(
                                capabilities.requestedFactFields.begin(),
                                capabilities.requestedFactFields.end(),
                                entry.key) !=
                                capabilities.requestedFactFields.end()) {
                            fields.push_back(entry);
                        }
                    }
                    projected =
                        std::make_shared<MapExpr>(std::move(fields));
                }
                if (!firstFact) json << ",";
                firstFact = false;
                json << exprToJson(projected);
                ++projectedRows;
            }
        }
        json << "]";
        const std::streampos projectionEnd = json.tellp();
        if (projectionStart >= 0 && projectionEnd >= projectionStart) {
            nativeFactProjectionBytes_ +=
                static_cast<std::size_t>(projectionEnd - projectionStart);
        }
        ++nativeFactProjectionCalls_;
    }
    if (capabilities.needsFactHierarchy && !callerProvidedFacts) {
        if (!first) json << ",";
        json << "\"__parents\":{";
        bool firstParent = true;
        std::set<std::string> emittedChildren;
        for (const auto& relation : memory_.hierarchyEdges()) {
            if (!emittedChildren.insert(relation.first).second) continue;
            if (!firstParent) json << ",";
            firstParent = false;
            const auto parents = memory_.parentsOf(relation.first);
            json << "\"" << jsonEscape(relation.first) << "\":[";
            for (size_t index = 0; index < parents.size(); ++index) {
                if (index) json << ",";
                json << "\"" << jsonEscape(parents[index]) << "\"";
            }
            json << "]";
        }
        json << "}";
    }
    json << "}";

    const std::string requestJson = json.str();
    nativeSerializationMicros_ += static_cast<std::size_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - serializationStart).count());
    ++nativeCalls_;
    nativeRequestBytes_ += requestJson.size();
    char* response = library->call(nativeFunctionName.c_str(), requestJson.c_str());
    if (!response) throw InterpreterError("Native function '" + nativeFunctionName + "' returned a null response");
    std::string responseText(response);
    library->free(response);

    std::shared_ptr<Expr> parsed;
    size_t pos = 0;
    if (!parseJsonValue(responseText, pos, parsed)) {
        throw InterpreterError("Native function '" + nativeFunctionName + "' returned invalid JSON");
    }
    skipJsonWs(responseText, pos);
    if (pos != responseText.size()) {
        throw InterpreterError("Native function '" + nativeFunctionName + "' returned trailing data after JSON");
    }

    if (auto errorValue = findMapValue(parsed, "error")) {
        std::string message;
        if (argAsString(errorValue, message) && !message.empty()) {
            throw InterpreterError("Native function '" + nativeFunctionName + "' failed: " + message);
        }
    }

    env[internalSymbolString(InternalSymbolKind::Return)] = parsed->clone();
    if (outputArgs.empty()) return true;

    auto returnedMap = std::dynamic_pointer_cast<MapExpr>(parsed);
    for (const Arg* outArg : outputArgs) {
        std::shared_ptr<Expr> value;
        if (returnedMap) {
            if (!outArg->name.empty()) value = findMapValue(parsed, outArg->name);
            if (!value) value = findMapValue(parsed, "access");
            if (!value) value = findMapValue(parsed, "out");
            if (!value) value = findMapValue(parsed, "result");
            if (!value) value = findMapValue(parsed, "value");
            if (!value && returnedMap->entries.size() == 1) value = returnedMap->entries.front().value;
            if (!value && (outArg->name == "result" || outArg->name == "out" ||
                           outArg->name == "access" || outArg->name == "equals")) {
                value = parsed;
            }
        } else {
            value = parsed;
        }
        if (!value || !unifyExpr(outArg->value, value, env)) return false;
    }
    return true;
}

bool Interpreter::evalBuiltinTerm(const TermExpr& term, const Env& env, std::shared_ptr<Expr>& out) {
    if (term.builtinId == BuiltinId::CommonAncestors ||
        term.builtinId == BuiltinId::LowestCommonAncestor ||
        term.builtinId == BuiltinId::HighestCommonAncestor ||
        term.builtinId == BuiltinId::AncestorAnalysis ||
        term.builtinId == BuiltinId::PropagateFact) {
        Call call(term.name, {}, term.builtinId);
        for (const auto& arg : term.args) call.args.push_back(Arg{arg.name, arg.value->clone()});
        return term.builtinId == BuiltinId::PropagateFact
            ? evalFactPropagation(call, env, out)
            : evalAncestorAnalysis(call, env, out);
    }
    if (term.builtinId == BuiltinId::ReasoningContrary ||
        term.builtinId == BuiltinId::ReasoningProve ||
        term.builtinId == BuiltinId::ReasoningGrade ||
        term.builtinId == BuiltinId::ReasoningDecide) {
        // Reasoning.prove/decide must receive the predicate syntax itself;
        // evaluating that term first would execute it and discard proof
        // identity. The reasoning runtime resolves only its query arguments.
        return evalReasoningBuiltin(term, env, out);
    }
    std::vector<std::shared_ptr<Expr>> args;
    args.reserve(term.args.size());
    for (const auto& arg : term.args) {
        std::shared_ptr<Expr> value;
        if (!evalExprValue(arg.value, env, value)) {
            if (auto var = std::dynamic_pointer_cast<VarExpr>(arg.value)) {
                if (!var->name.empty() && std::isupper(static_cast<unsigned char>(var->name.front()))) {
                    value = arg.value->clone();
                } else {
                    return false;
                }
            } else {
                return false;
            }
        }
        args.push_back(value);
    }

    if (term.builtinId == BuiltinId::SystemRun) {
        if (args.size() != 1) {
            throw InterpreterError("system.run expects one Felidae source string");
        }
        const auto source = std::dynamic_pointer_cast<StringExpr>(args.front());
        if (!source) throw InterpreterError("system.run expects a string source");
        try {
            Lexer lexer(source->value);
            Parser parser(lexer, operators_, currentLoadingFile_.string());
            const auto first = std::find_if_not(source->value.begin(), source->value.end(), [](unsigned char c) {
                return std::isspace(c) != 0;
            });
            if (first != source->value.end() && *first == '?') {
                const auto solutions = solve(parser.parseQuery());
                std::vector<std::shared_ptr<Expr>> resultItems;
                resultItems.reserve(solutions.size());
                for (const auto& solution : solutions) {
                    std::vector<MapEntry> bindings;
                    for (const auto& binding : solution.env) {
                        if (binding.first <= InternalSymbol::SystemResultId) continue;
                        const std::string name = symbolNameForId(binding.first);
                        if (name.empty() || isInternalGeneratedSymbolName(name)) continue;
                        const auto value = resolveExpr(binding.second, solution.env);
                        if (value) bindings.emplace_back(name, value->clone());
                    }
                    std::sort(bindings.begin(), bindings.end(), [](const MapEntry& left, const MapEntry& right) {
                        return left.key < right.key;
                    });
                    auto solutionValue = std::make_shared<MapExpr>(std::move(bindings));
                    solutionValue->factType = "QuerySolution";
                    resultItems.push_back(std::move(solutionValue));
                }
                auto result = std::make_shared<MapExpr>(std::vector<MapEntry>{
                    {"solutions", std::make_shared<ArrayExpr>(std::move(resultItems))},
                    {"count", std::make_shared<NumberExpr>(static_cast<double>(solutions.size()))}
                });
                result->factType = "QueryResult";
                out = std::move(result);
                return true;
            }
            return evalExprValue(parser.parseExpressionText(), env, out);
        } catch (const ParserError& error) {
            throw InterpreterError(std::string("system.run parse error: ") + error.what());
        }
    }

    if (term.builtinId == BuiltinId::Type) {
        if (args.size() != 1) return false;
        if (auto ast = std::dynamic_pointer_cast<AstValueExpr>(args.front())) {
            out = std::make_shared<StringExpr>(ast->nodeKind);
            return true;
        }
        if (auto type = std::dynamic_pointer_cast<StringExpr>(
                findMapValue(args.front(), internalSymbolString(InternalSymbolKind::Type)))) {
            out = type->clone();
            return true;
        }
        if (std::dynamic_pointer_cast<NumberExpr>(args.front())) out = std::make_shared<StringExpr>("number");
        else if (std::dynamic_pointer_cast<StringExpr>(args.front())) out = std::make_shared<StringExpr>("string");
        else if (std::dynamic_pointer_cast<BoolExpr>(args.front())) out = std::make_shared<StringExpr>("bool");
        else if (std::dynamic_pointer_cast<ArrayExpr>(args.front())) out = std::make_shared<StringExpr>("array");
        else if (std::dynamic_pointer_cast<NilExpr>(args.front())) out = std::make_shared<StringExpr>("nil");
        else return false;
        return true;
    }

    if (term.builtinId == BuiltinId::FnPair) {
        if (args.size() != 2) return false;
        out = std::make_shared<TermExpr>("fn:pair", std::vector<Arg>{{"first", args[0]}, {"last", args[1]}}, BuiltinId::FnPair);
        return true;
    }
    if (term.builtinId == BuiltinId::FnTuple) {
        std::vector<Arg> termArgs;
        termArgs.reserve(args.size());
        for (auto& arg : args) termArgs.push_back(Arg{"value", arg});
        out = std::make_shared<TermExpr>("fn:tuple", std::move(termArgs), BuiltinId::FnTuple);
        return true;
    }
    if (term.builtinId == BuiltinId::FnArray) {
        if (args.size() == 1) {
            if (auto array = std::dynamic_pointer_cast<ArrayExpr>(args[0])) {
                out = array->clone();
                return true;
            }
        }
        std::vector<Arg> termArgs;
        termArgs.reserve(args.size());
        for (auto& arg : args) termArgs.push_back(Arg{"data", arg});
        out = std::make_shared<TermExpr>("fn:array", std::move(termArgs), BuiltinId::FnArray);
        return true;
    }
    if (term.builtinId == BuiltinId::JsonObject) {
        std::vector<Arg> termArgs;
        termArgs.reserve(args.size());
        for (auto& arg : args) termArgs.push_back(Arg{"field", arg});
        out = std::make_shared<TermExpr>("json:object", std::move(termArgs), BuiltinId::JsonObject);
        return true;
    }
    auto hasCallableBody = [&]() {
        auto* clauses = findClauses(term.name, term.nameId);
        if (!clauses && ensurePredicateLoaded(term.name)) clauses = findClauses(term.name, term.nameId);
        if (!clauses) return false;
        for (const auto& clause : *clauses) {
            if (!clause->body.empty() || isMethodClause(*clause)) return true;
        }
        return false;
    };
    if (hasMethod(term.name) || hasCallableBody()) return evalCallAsValue(term, env, out);
    if (term.builtinId != BuiltinId::Unknown) return evalCallAsValue(term, env, out);
    if (term.name.find(':') != std::string::npos && !term.args.empty()) {
        if (evalCallAsValue(term, env, out)) return true;
    }

    if (term.name.find(':') == std::string::npos && !term.args.empty()) {
        std::vector<MapEntry> entries;
        entries.push_back(MapEntry{std::string(internalSymbolName(InternalSymbolKind::Type)), std::make_shared<StringExpr>(term.name)});
        for (size_t i = 0; i < term.args.size(); ++i) {
            const auto& arg = term.args[i];
            if (arg.name.empty()) {
                entries.push_back(MapEntry{"value", args[i]});
            } else {
                entries.push_back(MapEntry{arg.name, args[i]});
            }
        }
        auto fact = std::make_shared<MapExpr>(std::move(entries));
        fact->factType = term.name;
        out = std::move(fact);
        return true;
    }

    std::vector<Arg> termArgs;
    termArgs.reserve(args.size());
    for (auto& arg : args) termArgs.push_back(Arg{"value", arg});
    out = std::make_shared<TermExpr>(term.name, std::move(termArgs), term.builtinId);
    return true;
}

bool Interpreter::evalCallAsValue(const TermExpr& term, const Env& env, std::shared_ptr<Expr>& out) {
    TermExpr current(term.name, {}, term.builtinId);
    current.nameId = term.nameId;
    current.args.reserve(term.args.size());
    for (const auto& argument : term.args) {
        current.args.push_back(Arg{argument.name, argument.value->clone()});
    }
    Env currentEnv = env;
    ++valueCallTrampolineDepth_;
    try {
        while (true) {
            try {
                const bool result = evalCallAsValueOnce(current, currentEnv, out);
                --valueCallTrampolineDepth_;
                return result;
            } catch (TailCallSignal& signal) {
                current = std::move(signal.term);
                currentEnv = std::move(signal.env);
            }
        }
    } catch (...) {
        --valueCallTrampolineDepth_;
        throw;
    }
}

bool Interpreter::evalCallAsValueOnce(
    const TermExpr& term,
    const Env& env,
    std::shared_ptr<Expr>& out) {
    const BuiltinId builtin = term.builtinId;
    if (builtin == BuiltinId::CommonAncestors ||
        builtin == BuiltinId::LowestCommonAncestor ||
        builtin == BuiltinId::HighestCommonAncestor ||
        builtin == BuiltinId::AncestorAnalysis ||
        builtin == BuiltinId::PropagateFact) {
        Call call(term.name, {}, builtin);
        for (const auto& arg : term.args) call.args.push_back(Arg{arg.name, arg.value->clone()});
        return builtin == BuiltinId::PropagateFact
            ? evalFactPropagation(call, env, out)
            : evalAncestorAnalysis(call, env, out);
    }
    if (builtin == BuiltinId::RelationCompare) {
        Call call(term.name, {}, builtin);
        for (const auto& arg : term.args) call.args.push_back(Arg{arg.name, arg.value->clone()});
        return evalRelationCompare(call, env, out);
    }
    if (builtin == BuiltinId::FactReferences) {
        Call call(term.name, {}, builtin);
        for (const auto& arg : term.args) call.args.push_back(Arg{arg.name, arg.value->clone()});
        return evalFactReferences(call, env, out);
    }
    if (builtin == BuiltinId::RelationFind || builtin == BuiltinId::DependencySatisfied) {
        Call call(term.name, {}, builtin);
        for (const auto& arg : term.args) call.args.push_back(Arg{arg.name, arg.value->clone()});
        return builtin == BuiltinId::RelationFind
            ? evalRelationFind(call, env, out)
            : evalDependencySatisfied(call, env, out);
    }
    {
        Call attachment(term.name, {});
        for (const auto& arg : term.args) attachment.args.push_back(Arg{arg.name, arg.value->clone()});
        Env attachmentEnv = env;
        if (solveFactAttachment(attachment, attachmentEnv)) {
            const auto returned = attachmentEnv.find(internalSymbolString(InternalSymbolKind::Return));
            if (returned == attachmentEnv.end()) return false;
            out = resolveExpr(returned->second, attachmentEnv)->clone();
            return true;
        }
    }
    if (!nativeDeclarationFor(term.name)) {
        ensurePredicateLoaded(term.name);
    }

    auto* declaredClauses = findClauses(term.name, term.nameId);
    if (declaredClauses) {
        for (const auto& clause : *declaredClauses) {
            if (bodyHasReturnGoal(clause->body)) continue;
            std::string outputName;
            size_t missingCount = 0;
            for (const auto& parameter : clause->head.args) {
                if (parameter.name.empty()) continue;
                bool supplied = false;
                for (const auto& argument : term.args) {
                    if (argument.name == parameter.name) {
                        supplied = true;
                        break;
                    }
                }
                if (!supplied) {
                    outputName = parameter.name;
                    ++missingCount;
                }
            }
            if (missingCount != 1) continue;

            const std::string outputVariable = "\x1fvalue_call_output";
            Call outputCall(term.name, {}, term.builtinId);
            for (const auto& argument : term.args) {
                outputCall.args.push_back(Arg{argument.name, argument.value->clone()});
            }
            outputCall.args.push_back(
                Arg{outputName, std::make_shared<VarExpr>(outputVariable)});

            std::vector<Solution> solutions;
            {
                PipelineResultClearScope clearPipelineResults(pipelineResults_);
                solveRecursive(
                    {std::make_shared<CallGoal>(std::move(outputCall))},
                    env,
                    solutions,
                    1,
                    0);
            }
            if (!solutions.empty()) {
                auto value = solutions.front().env.find(outputVariable);
                if (value != solutions.front().env.end()) {
                    out = resolveExpr(value->second, solutions.front().env)->clone();
                    return true;
                }
            }
        }

    }

    if (nativeDeclarationFor(term.name)) {
        Call nativeCall(term.name, {}, term.builtinId);
        for (const auto& arg : term.args) nativeCall.args.push_back(Arg{arg.name, arg.value->clone()});
        Env nativeEnv = env;
        if (solveNativeCall(nativeCall, nativeEnv)) {
            auto returned = nativeEnv.find(internalSymbolString(InternalSymbolKind::Return));
            if (returned != nativeEnv.end()) {
                if (auto returnedMap = std::dynamic_pointer_cast<MapExpr>(returned->second)) {
                    if (returnedMap->entries.size() == 1) {
                        out = returnedMap->entries.front().value->clone();
                        return true;
                    }
                }
                out = returned->second->clone();
                return true;
            }
        }
    }

    std::vector<std::shared_ptr<Expr>> args;
    args.reserve(term.args.size());
    for (const auto& arg : term.args) {
        std::shared_ptr<Expr> value;
        if (!evalExprValue(arg.value, env, value)) {
            if (auto var = std::dynamic_pointer_cast<VarExpr>(arg.value)) {
                if (!var->name.empty() && std::isupper(static_cast<unsigned char>(var->name.front()))) {
                    value = arg.value->clone();
                } else {
                    return false;
                }
            } else {
                return false;
            }
        }
        args.push_back(value);
    }

    auto* callableClauses = findClauses(term.name, term.nameId);
    if (!callableClauses && ensurePredicateLoaded(term.name)) {
        callableClauses = findClauses(term.name, term.nameId);
    }
    bool callableBody = false;
    if (callableClauses) {
        for (const auto& clause : *callableClauses) {
            if (!clause->body.empty() || isMethodClause(*clause)) {
                callableBody = true;
                break;
            }
        }
    }
    if (callableBody && !nativeDeclarationFor(term.name)) {
        if (!callableClauses) return false;
        for (const auto& clause : *callableClauses) {
            if (clause->body.empty() && !isMethodClause(*clause)) continue;
            Call call(term.name, {}, term.builtinId);
            const auto parameterPlans = buildMethodParamPlan(*clause);
            for (size_t argumentIndex = 0; argumentIndex < term.args.size(); ++argumentIndex) {
                const auto& argument = term.args[argumentIndex];
                bool preserveAst = false;
                for (size_t parameterIndex = 0; parameterIndex < clause->head.args.size(); ++parameterIndex) {
                    const auto& parameter = clause->head.args[parameterIndex];
                    if ((!argument.name.empty() && argument.name == parameter.name) ||
                        (argument.name.empty() && argumentIndex == parameterIndex)) {
                        const auto typeId = parameterPlans[parameterIndex].typeId;
                        preserveAst = typeId == LanguageTypeId::Expr ||
                                      typeId == LanguageTypeId::Stmt ||
                                      typeId == LanguageTypeId::Statements;
                        break;
                    }
                }
                call.args.push_back(Arg{
                    argument.name,
                    preserveAst ? argument.value->clone() : args[argumentIndex]});
            }
            std::vector<Solution> solutions;
            const bool previousValueCallMode = valueCallMode_;
            {
                PipelineResultClearScope clearPipelineResults(pipelineResults_);
                valueCallMode_ = true;
                try {
                    solveMethodCall(call, clause, env, solutions, 1, 0);
                } catch (...) {
                    valueCallMode_ = previousValueCallMode;
                    throw;
                }
                valueCallMode_ = previousValueCallMode;
            }
            if (!solutions.empty()) {
                auto returned = solutions.front().env.find(internalSymbolString(InternalSymbolKind::Return));
                if (returned != solutions.front().env.end()) {
                    if (auto returnedMap = std::dynamic_pointer_cast<MapExpr>(returned->second)) {
                        if (returnedMap->entries.size() == 1 && returnedMap->entries.front().key.empty()) {
                            out = returnedMap->entries.front().value->clone();
                            return true;
                        }
                    }
                    out = returned->second->clone();
                    return true;
                }
            }
        }
        Call call(term.name, {}, term.builtinId);
        for (size_t i = 0; i < args.size(); ++i) {
            call.args.push_back(Arg{term.args[i].name, args[i]});
        }
        std::vector<Solution> goalSolutions;
        {
            PipelineResultClearScope clearPipelineResults(pipelineResults_);
            solveRecursive({std::make_shared<CallGoal>(call)}, env, goalSolutions, 1, 0);
        }
        if (!goalSolutions.empty()) {
            out = std::make_shared<StringExpr>("true");
            return true;
        }
        return false;
    }

    auto evalNamed = [&](const std::string& name, size_t index, std::shared_ptr<Expr>& value) -> bool {
        const Arg* arg = findTermArgByNameOrIndex(term, name, index);
        if (!arg) return false;
        return evalExprValue(arg->value, env, value);
    };

    auto requireNamedString = [&](std::initializer_list<const char*> names, size_t index, const std::string& label) {
        std::shared_ptr<Expr> value;
        const Arg* arg = nullptr;
        for (const char* name : names) {
            arg = findTermArgByNameOrIndex(term, name, index);
            if (arg) break;
        }
        if (!arg || !evalExprValue(arg->value, env, value)) {
            throw InterpreterError(term.name + " expects string argument '" + label + "'");
        }
        return requireString(value, term.name, label);
    };

    auto requireNamedNumber = [&](std::initializer_list<const char*> names, size_t index, const std::string& label) {
        std::shared_ptr<Expr> value;
        const Arg* arg = nullptr;
        for (const char* name : names) {
            arg = findTermArgByNameOrIndex(term, name, index);
            if (arg) break;
        }
        if (!arg || !evalExprValue(arg->value, env, value)) {
            throw InterpreterError(term.name + " expects numeric argument '" + label + "'");
        }
        return requireNumber(value, term.name, label);
    };

    auto arrayItems = [&](const std::shared_ptr<Expr>& value) -> std::vector<std::shared_ptr<Expr>> {
        if (auto array = std::dynamic_pointer_cast<ArrayExpr>(value)) return array->items;
        if (auto var = std::dynamic_pointer_cast<VarExpr>(term.args.empty() ? nullptr : term.args[0].value)) {
            if (!var->name.empty() && std::isupper(static_cast<unsigned char>(var->name.front()))) {
                return valuesForLambdaSource(term.args[0].value, env);
            }
        }
        return {};
    };

    if (builtin == BuiltinId::ArrayGet) {
        std::shared_ptr<Expr> dataValue;
        std::shared_ptr<Expr> indexValue;
        if (!evalNamed("data", 0, dataValue) ||
            (!evalNamed("position", 1, indexValue) && !evalNamed("index", 1, indexValue))) {
            throw InterpreterError("array:get expects array argument 'data' and numeric argument 'position'");
        }
        auto data = std::dynamic_pointer_cast<ArrayExpr>(dataValue);
        double index = 0.0;
        if (!data || !argAsNumber(indexValue, index) || index < 0 || std::floor(index) != index) {
            throw InterpreterError("array:get expects an array and a non-negative integer position");
        }
        const size_t resolvedIndex = static_cast<size_t>(index);
        if (resolvedIndex >= data->items.size()) {
            throw InterpreterError("array:get position is outside the array");
        }
        out = data->items[resolvedIndex]->clone();
        return true;
    }

    if (builtin == BuiltinId::ConsoleReadLine ||
        builtin == BuiltinId::ConsoleInput ||
        builtin == BuiltinId::ConsoleInputNumber) {
        if (builtin == BuiltinId::ConsoleInput || builtin == BuiltinId::ConsoleInputNumber) {
            if (args.size() > 1) throw InterpreterError(term.name + " expects zero arguments or one prompt argument");
            const Arg* promptArg = findTermArgByNameOrIndex(term, "print", 0);
            if (!promptArg) promptArg = findTermArgByNameOrIndex(term, "prompt", 0);
            if (promptArg) {
                std::shared_ptr<Expr> promptValue;
                if (!evalExprValue(promptArg->value, env, promptValue)) {
                    throw InterpreterError(term.name + " prompt did not evaluate");
                }
                std::string promptText = requireString(promptValue, term.name, "print");
                std::cout << promptText;
                std::cout.flush();
            }
        } else if (!args.empty()) {
            throw InterpreterError("console.readLine expects no arguments");
        }
        std::string line;
        if (!std::getline(std::cin, line)) line.clear();
        if (builtin == BuiltinId::ConsoleInputNumber) {
            char* end = nullptr;
            const double number = std::strtod(line.c_str(), &end);
            while (end && *end && std::isspace(static_cast<unsigned char>(*end))) ++end;
            if (line.empty() || !end || *end != '\0' || !std::isfinite(number)) {
                throw InterpreterError("console.inputNumber expected a numeric input");
            }
            out = std::make_shared<NumberExpr>(number);
        } else {
            out = std::make_shared<StringExpr>(line);
        }
        return true;
    }

    if (builtin == BuiltinId::ConsoleWriteLine ||
        builtin == BuiltinId::ConsoleWrite ||
        builtin == BuiltinId::SystemPrint) {
        if (args.size() != 1) throw InterpreterError(term.name + " expects one value");
        std::string text = args[0]->debug();
        if (auto str = std::dynamic_pointer_cast<StringExpr>(args[0])) text = str->value;
        std::cout << text;
        if (builtin == BuiltinId::ConsoleWriteLine || builtin == BuiltinId::SystemPrint) std::cout << "\n";
        out = std::make_shared<StringExpr>("ok");
        return true;
    }

    if (builtin == BuiltinId::SystemPrintf) {
        if (args.size() != 1) throw InterpreterError("system.printf expects one format string");
        const auto format = std::dynamic_pointer_cast<StringExpr>(args.front());
        if (!format) throw InterpreterError("system.printf expects a string format");

        auto placeholderValue = [&](std::string name) -> std::shared_ptr<Expr> {
            const auto begin = name.find_first_not_of(" \t");
            const auto end = name.find_last_not_of(" \t");
            if (begin == std::string::npos) {
                throw InterpreterError("system.printf does not allow an empty placeholder");
            }
            name = name.substr(begin, end - begin + 1);
            std::shared_ptr<Expr> value;
            const size_t separator = name.find('.');
            const std::string root = name.substr(0, separator);
            if (!evalExprValue(std::make_shared<VarExpr>(root), env, value)) {
                throw InterpreterError("system.printf cannot resolve '{" + name + "}'");
            }
            size_t fieldStart = separator;
            while (fieldStart != std::string::npos) {
                const size_t next = name.find('.', fieldStart + 1);
                const std::string field = name.substr(fieldStart + 1, next - fieldStart - 1);
                if (field.empty()) throw InterpreterError("system.printf has an invalid placeholder '{" + name + "}'");
                value = findMapValue(value, field);
                if (!value) throw InterpreterError("system.printf cannot resolve '{" + name + "}'");
                value = resolveExpr(value, env);
                fieldStart = next;
            }
            return value;
        };

        std::ostringstream rendered;
        for (size_t index = 0; index < format->value.size(); ++index) {
            const char current = format->value[index];
            if (current == '{') {
                if (index + 1 < format->value.size() && format->value[index + 1] == '{') {
                    rendered << '{';
                    ++index;
                    continue;
                }
                const size_t close = format->value.find('}', index + 1);
                if (close == std::string::npos) throw InterpreterError("system.printf has an unmatched '{'");
                const auto value = placeholderValue(format->value.substr(index + 1, close - index - 1));
                if (const auto text = std::dynamic_pointer_cast<StringExpr>(value)) rendered << text->value;
                else rendered << valueToString(value);
                index = close;
                continue;
            }
            if (current == '}') {
                if (index + 1 < format->value.size() && format->value[index + 1] == '}') {
                    rendered << '}';
                    ++index;
                    continue;
                }
                throw InterpreterError("system.printf has an unmatched '}'");
            }
            rendered << current;
        }
        std::cout << rendered.str();
        std::cout.flush();
        out = std::make_shared<StringExpr>("ok");
        return true;
    }

    if (builtin == BuiltinId::StrConcat) {
        std::string left = requireNamedString({"left", "data", "value"}, 0, "left");
        std::string right = requireNamedString({"right", "rhs"}, 1, "right");
        out = std::make_shared<StringExpr>(left + right);
        return true;
    }

    if (builtin == BuiltinId::StrJoin) {
        if (args.empty()) throw InterpreterError("str.join expects array argument 'data'");
        std::vector<std::shared_ptr<Expr>> items;
        if (!exprAsArrayItems(args[0], items)) throw InterpreterError("str.join expects array argument 'data'");
        std::string delimiter = requireNamedString({"delimiter", "separator"}, 1, "delimiter");
        std::ostringstream joined;
        for (size_t i = 0; i < items.size(); ++i) {
            if (i) joined << delimiter;
            std::string itemText;
            if (argAsString(items[i], itemText)) joined << itemText;
            else joined << valueToString(items[i]);
        }
        out = std::make_shared<StringExpr>(joined.str());
        return true;
    }
    if (builtin == BuiltinId::StrLower || builtin == BuiltinId::StrUpper) {
        std::string text = requireNamedString({"data", "value"}, 0, "data");
        for (char& ch : text) {
            ch = static_cast<char>(builtin == BuiltinId::StrLower
                ? std::tolower(static_cast<unsigned char>(ch))
                : std::toupper(static_cast<unsigned char>(ch)));
        }
        out = std::make_shared<StringExpr>(text);
        return true;
    }

    if (builtin == BuiltinId::StrTrim) {
        out = std::make_shared<StringExpr>(Felidae::trim(requireNamedString({"data", "value"}, 0, "data")));
        return true;
    }

    if (builtin == BuiltinId::StrSplit) {
        std::string text = requireNamedString({"data", "value"}, 0, "data");
        std::string delimiter = requireNamedString({"delimiter", "separator"}, 1, "delimiter");
        if (delimiter.empty()) throw InterpreterError("str.split expects non-empty delimiter");
        std::vector<std::shared_ptr<Expr>> parts;
        size_t start = 0;
        while (true) {
            size_t pos = text.find(delimiter, start);
            parts.push_back(std::make_shared<StringExpr>(text.substr(start, pos == std::string::npos ? pos : pos - start)));
            if (pos == std::string::npos) break;
            start = pos + delimiter.size();
        }
        out = std::make_shared<ArrayExpr>(std::move(parts));
        return true;
    }

    if (builtin == BuiltinId::StrReplace) {
        std::string text = requireNamedString({"data", "value"}, 0, "data");
        std::string search = requireNamedString({"search", "needle"}, 1, "search");
        std::string replacement = requireNamedString({"replacement", "with"}, 2, "replacement");
        if (search.empty()) throw InterpreterError("str.replace expects non-empty search text");
        size_t pos = 0;
        while ((pos = text.find(search, pos)) != std::string::npos) {
            text.replace(pos, search.size(), replacement);
            pos += replacement.size();
        }
        out = std::make_shared<StringExpr>(text);
        return true;
    }

    if (builtin == BuiltinId::StrContains ||
        builtin == BuiltinId::StrStartsWith ||
        builtin == BuiltinId::StrEndsWith) {
        std::string text = requireNamedString({"data", "value"}, 0, "data");
        std::string needle = builtin == BuiltinId::StrContains
            ? requireNamedString({"needle", "search"}, 1, "needle")
            : requireNamedString({builtin == BuiltinId::StrStartsWith ? "prefix" : "suffix"}, 1, "needle");
        bool ok = builtin == BuiltinId::StrContains
            ? text.find(needle) != std::string::npos
            : (builtin == BuiltinId::StrStartsWith
                ? text.rfind(needle, 0) == 0
                : (needle.size() <= text.size() && text.compare(text.size() - needle.size(), needle.size(), needle) == 0));
        out = std::make_shared<BoolExpr>(ok);
        return true;
    }

    if (builtin == BuiltinId::StrLen) {
        std::string text = requireNamedString({"data", "value"}, 0, "data");
        out = std::make_shared<NumberExpr>(static_cast<double>(text.size()));
        return true;
    }

    if (builtin == BuiltinId::FileReadFile) {
        std::string pathText = requireNamedString({"path", "file"}, 0, "path");
        fs::path target(pathText);
        if (fs::is_directory(target)) throw InterpreterError("file.readFile expected a file, got directory: " + pathText);
        try {
            out = std::make_shared<StringExpr>(readSourceFile(target));
        } catch (const std::exception& e) {
            throw InterpreterError("file.readFile failed: " + std::string(e.what()));
        }
        return true;
    }

    if (builtin == BuiltinId::FileReadLines) {
        std::string pathText = requireNamedString({"path", "file"}, 0, "path");
        fs::path target(pathText);
        if (fs::is_directory(target)) throw InterpreterError("file.readLines expected a file, got directory: " + pathText);
        std::vector<std::shared_ptr<Expr>> lines;
        try {
            readSourceLines(target, [&](const std::string& line) {
                lines.push_back(std::make_shared<StringExpr>(line));
            });
        } catch (const std::exception& e) {
            throw InterpreterError("file.readLines failed: " + std::string(e.what()));
        }
        out = std::make_shared<ArrayExpr>(std::move(lines));
        return true;
    }

    if (builtin == BuiltinId::FileReadLine) {
        std::string pathText = requireNamedString({"path", "file"}, 0, "path");
        double lineNumber = requireNamedNumber({"line", "index"}, 1, "line");
        if (lineNumber < 0 || std::floor(lineNumber) != lineNumber) {
            throw InterpreterError("file.readLine expects non-negative integer argument 'line'");
        }
        fs::path target(pathText);
        if (fs::is_directory(target)) throw InterpreterError("file.readLine expected a file, got directory: " + pathText);
        std::ifstream in(target, std::ios::binary);
        if (!in) throw InterpreterError("file.readLine cannot open: " + pathText);
        std::string line;
        size_t wanted = static_cast<size_t>(lineNumber);
        for (size_t index = 0; std::getline(in, line); ++index) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (index == wanted) {
                out = std::make_shared<StringExpr>(line);
                return true;
            }
        }
        return false;
    }

    if (builtin == BuiltinId::FileWriteFile ||
        builtin == BuiltinId::FileAppendFile ||
        builtin == BuiltinId::FileWriteLines) {
        std::string pathText = requireNamedString({"path", "file"}, 0, "path");
        std::string data;
        if (builtin == BuiltinId::FileWriteLines) {
            std::shared_ptr<Expr> linesValue;
            if (!evalNamed("data", 1, linesValue) && !evalNamed("lines", 1, linesValue)) {
                throw InterpreterError("file.writeLines expects array argument 'data'");
            }
            std::vector<std::shared_ptr<Expr>> lines;
            if (!exprAsArrayItems(linesValue, lines)) throw InterpreterError("file.writeLines expects array argument 'data'");
            std::ostringstream joined;
            for (const auto& line : lines) joined << exprTextValue(line) << "\n";
            data = joined.str();
        } else {
            data = requireNamedString({"data", "text", "content"}, 1, "data");
        }
        std::string modeText = builtin == BuiltinId::FileAppendFile ? "append" : "write";
        if (const Arg* modeArg = findTermArgByNameOrIndex(term, "mode", 2)) {
            std::shared_ptr<Expr> modeValue;
            if (!evalExprValue(modeArg->value, env, modeValue) || !argAsString(modeValue, modeText)) {
                throw InterpreterError(term.name + " expects string argument 'mode'");
            }
        }
        if (modeText != "write" && modeText != "append") {
            throw InterpreterError(term.name + " mode must be 'write' or 'append'");
        }
        std::ios::openmode mode = std::ios::binary;
        mode |= modeText == "append" ? std::ios::app : std::ios::trunc;
        std::ofstream outFile(fs::path(pathText), mode);
        if (!outFile) throw InterpreterError(term.name + " cannot open: " + pathText);
        outFile << data;
        out = std::make_shared<StringExpr>("ok");
        return true;
    }

    if (builtin == BuiltinId::FileExists) {
        std::string pathText = requireNamedString({"path", "file"}, 0, "path");
        out = std::make_shared<BoolExpr>(fs::exists(fs::path(pathText)));
        return true;
    }

    if (builtin == BuiltinId::FileDeleteFile) {
        std::string pathText = requireNamedString({"path", "file"}, 0, "path");
        std::error_code ec;
        fs::path target(pathText);
        if (fs::is_directory(target, ec)) {
            if (ec) throw InterpreterError("file.deleteFile failed: " + ec.message());
            throw InterpreterError("file.deleteFile expected a file, got directory: " + pathText);
        }
        bool removed = fs::remove(target, ec);
        if (ec) throw InterpreterError("file.deleteFile failed: " + ec.message());
        out = std::make_shared<BoolExpr>(removed);
        return true;
    }

    if (builtin == BuiltinId::JsonParse) {
        std::string jsonText = requireNamedString({"data", "value"}, 0, "data");
        std::shared_ptr<Expr> parsed;
        if (!parseFlatJsonObject(jsonText, parsed)) {
            throw InterpreterError("json.parse failed: invalid JSON");
        }
        out = parsed;
        return true;
    }

    if (builtin == BuiltinId::JsonGet) {
        std::shared_ptr<Expr> dataValue;
        if (!evalNamed("data", 0, dataValue) && !evalNamed("object", 0, dataValue)) {
            throw InterpreterError("json.get expects argument 'data'");
        }
        std::string key = requireNamedString({"key"}, 1, "key");
        auto value = findMapValue(dataValue, key);
        if (!value) return false;
        out = value->clone();
        return true;
    }

    if (builtin == BuiltinId::JsonHas || builtin == BuiltinId::JsonKeys ||
        builtin == BuiltinId::JsonSet || builtin == BuiltinId::JsonRemove) {
        std::shared_ptr<Expr> dataValue;
        if (!evalNamed("data", 0, dataValue) && !evalNamed("object", 0, dataValue)) {
            throw InterpreterError(term.name + " expects argument 'data'");
        }
        std::vector<MapEntry> entries;
        if (!exprAsMapEntries(dataValue, entries)) throw InterpreterError(term.name + " expects map/object argument 'data'");
        if (builtin == BuiltinId::JsonKeys) {
            std::vector<std::shared_ptr<Expr>> keys;
            keys.reserve(entries.size());
            for (const auto& entry : entries) keys.push_back(std::make_shared<StringExpr>(entry.key));
            out = std::make_shared<ArrayExpr>(std::move(keys));
            return true;
        }
        std::string key = requireNamedString({"key"}, 1, "key");
        if (builtin == BuiltinId::JsonHas) {
            out = std::make_shared<BoolExpr>(
                static_cast<bool>(findMapValue(dataValue, key)));
            return true;
        }
        if (builtin == BuiltinId::JsonSet) {
            std::shared_ptr<Expr> value;
            if (!evalNamed("value", 2, value)) throw InterpreterError("json.set expects argument 'value'");
            upsertEntry(entries, key, cloneExprOrNil(value));
        } else {
            removeEntry(entries, key);
        }
        out = std::make_shared<MapExpr>(std::move(entries));
        return true;
    }

    if (builtin == BuiltinId::JsonToText) {
        std::shared_ptr<Expr> dataValue;
        if (!evalNamed("data", 0, dataValue) && !evalNamed("value", 0, dataValue)) {
            throw InterpreterError("json.toText expects argument 'data'");
        }
        out = std::make_shared<StringExpr>(exprToJson(dataValue));
        return true;
    }

    if (builtin == BuiltinId::ThreadCreateThread) {
        std::string functionName = requireNamedString({"function", "name"}, 0, "function");
        out = makeThreadHandle(createThreadTask(functionName));
        return true;
    }

    if (builtin == BuiltinId::ThreadStart ||
        builtin == BuiltinId::ThreadStatus ||
        builtin == BuiltinId::ThreadResult) {
        std::shared_ptr<Expr> handle;
        if (!evalNamed("thread", 0, handle)) throw InterpreterError(term.name + " expects thread argument 'thread'");
        if (builtin == BuiltinId::ThreadStart) {
            out = std::make_shared<StringExpr>(startThreadTask(handle));
        } else if (builtin == BuiltinId::ThreadStatus) {
            out = std::make_shared<StringExpr>(threadTaskStatus(handle));
        } else {
            out = threadTaskResult(handle);
        }
        return true;
    }

    if (builtin == BuiltinId::ThreadPause || builtin == BuiltinId::ThreadStop) {
        throw InterpreterError(term.name + " is not supported; Felidae threads run immutable snapshots to completion");
    }

    const bool factStoreBuiltin =
        builtin == BuiltinId::FactAll || builtin == BuiltinId::FactFind ||
        builtin == BuiltinId::FactCount || builtin == BuiltinId::FactFirst ||
        builtin == BuiltinId::FactTypes || builtin == BuiltinId::FactFields ||
        builtin == BuiltinId::FactExists || builtin == BuiltinId::FactSelect ||
        builtin == BuiltinId::FactMaterialize || builtin == BuiltinId::FactRelease;
    if (factStoreBuiltin || builtin == BuiltinId::DbSync) {
        std::string_view op;
        switch (builtin) {
            case BuiltinId::DbSync: op = "sync"; break;
            case BuiltinId::FactAll: op = "all"; break;
            case BuiltinId::FactFind: op = "find"; break;
            case BuiltinId::FactCount: op = "count"; break;
            case BuiltinId::FactFirst: op = "first"; break;
            case BuiltinId::FactTypes: op = "types"; break;
            case BuiltinId::FactFields: op = "fields"; break;
            case BuiltinId::FactExists: op = "exists"; break;
            case BuiltinId::FactSelect: op = "select"; break;
            case BuiltinId::FactMaterialize: op = "materialize"; break;
            case BuiltinId::FactRelease: op = "release"; break;
            default: break;
        }
        if (op == "sync") {
            const std::string pathText = requireNamedString({"path", "file"}, 0, "path");
            out = std::make_shared<NumberExpr>(static_cast<double>(syncFactSource(fs::path(pathText))));
            return true;
        }
        if (op == "materialize") {
            std::shared_ptr<Expr> selection;
            if (!evalNamed("selection", 0, selection) && !evalNamed("value", 0, selection)) {
                throw InterpreterError("Fact.materialize expects a FactSelection");
            }
            out = materializeFactSelection(selection);
            return true;
        }
        if (op == "release") {
            std::shared_ptr<Expr> selection;
            if (!evalNamed("selection", 0, selection) && !evalNamed("value", 0, selection)) {
                throw InterpreterError("Fact.release expects a FactSelection");
            }
            const auto kind = std::dynamic_pointer_cast<StringExpr>(
                findMapValue(selection, internalSymbolString(InternalSymbolKind::Type)));
            const auto generation = std::dynamic_pointer_cast<NumberExpr>(
                findMapValue(selection, "snapshot_generation"));
            if (!kind || kind->value != "FactSelection" || !generation ||
                generation->value <= 0 || std::floor(generation->value) != generation->value) {
                throw InterpreterError("Fact.release expects a captured FactSelection");
            }
            out = std::make_shared<BoolExpr>(memory_.releaseSnapshot(
                static_cast<std::uint64_t>(generation->value)));
            return true;
        }
        if (op == "types") {
            std::set<std::string> types;
            for (const size_t factIndex : memory_.activeFactIndexes()) {
                types.insert(memory_.fact(factIndex).type);
            }
            for (const auto& parent : memory_.parents()) {
                types.insert(parent.first);
                types.insert(parent.second);
            }
            std::vector<std::shared_ptr<Expr>> items;
            items.reserve(types.size());
            for (const auto& type : types) items.push_back(std::make_shared<StringExpr>(type));
            out = std::make_shared<ArrayExpr>(std::move(items));
            return true;
        }

        std::string typeName = requireNamedString({"type", "fact"}, 0, "type");
        if (typeName.empty()) throw InterpreterError(term.name + " expects non-empty type");
        ensurePredicateLoaded(typeName);

        auto matchingFacts = [&]() {
            std::vector<std::shared_ptr<Expr>> rows;
            for (size_t factIndex : memory_.compatibleFactIndexes(typeName)) {
                if (const auto value = memory_.factValue(factIndex)) {
                    rows.push_back(value);
                }
            }
            return rows;
        };

        if (op == "select") {
            std::string field;
            std::shared_ptr<Expr> expected;
            if (term.args.size() > 1) {
                field = requireNamedString({"field", "key"}, 1, "field");
                if (!evalNamed("equals", 2, expected) && !evalNamed("value", 2, expected)) {
                    throw InterpreterError("Fact.select expects argument 'equals'");
                }
            }
            out = std::make_shared<FactSelectionExpr>(
                typeName,
                memory_.captureSnapshot(),
                std::move(field),
                expected ? expected->clone() : nullptr);
            return true;
        }

        if (op == "all") {
            out = std::make_shared<ArrayExpr>(matchingFacts());
            return true;
        }
        if (op == "count") {
            out = std::make_shared<NumberExpr>(static_cast<double>(matchingFacts().size()));
            return true;
        }
        if (op == "fields") {
            std::set<std::string> fields;
            for (const auto& row : matchingFacts()) {
                std::vector<MapEntry> entries;
                if (!exprAsMapEntries(row, entries)) continue;
                for (const auto& entry : entries) {
                    if (entry.keyId != InternalSymbol::TypeId && entry.keyId != InternalSymbol::ParentId) fields.insert(entry.key);
                }
            }
            std::vector<std::shared_ptr<Expr>> items;
            items.reserve(fields.size());
            for (const auto& field : fields) items.push_back(std::make_shared<StringExpr>(field));
            out = std::make_shared<ArrayExpr>(std::move(items));
            return true;
        }
        if (op == "find" || op == "first") {
            std::string field = requireNamedString({"field", "key"}, 1, "field");
            std::shared_ptr<Expr> expected;
            if (!evalNamed("equals", 2, expected) && !evalNamed("value", 2, expected)) {
                throw InterpreterError(term.name + " expects argument 'equals'");
            }
            std::vector<std::shared_ptr<Expr>> rows;
            for (const auto& row : matchingFacts()) {
                auto actual = findMapValue(row, field);
                if (actual && exprContainsLiteral(actual, expected)) {
                    if (op == "first") {
                        out = row->clone();
                        return true;
                    }
                    rows.push_back(row->clone());
                }
            }
            if (op == "first") return false;
            out = std::make_shared<ArrayExpr>(std::move(rows));
            return true;
        }
        throw InterpreterError("Unknown in-memory fact-store builtin: " + term.name);
    }

    switch (term.builtinId) {
        case BuiltinId::MathPi:
            out = std::make_shared<NumberExpr>(3.14159265358979323846);
            return true;
        case BuiltinId::MathE:
            out = std::make_shared<NumberExpr>(2.71828182845904523536);
            return true;
        case BuiltinId::MathRandom: {
            double min = term.args.empty() ? 0.0 : requireNamedNumber({"min"}, 0, "min");
            double max = term.args.size() < 2 ? 1.0 : requireNamedNumber({"max"}, 1, "max");
            if (max < min) throw InterpreterError("math.random expects max >= min");
            static thread_local std::mt19937 rng{std::random_device{}()};
            std::uniform_real_distribution<double> dist(min, max);
            out = std::make_shared<NumberExpr>(dist(rng));
            return true;
        }
        case BuiltinId::MathPow:
        case BuiltinId::MathAtan2:
        case BuiltinId::MathAdd:
        case BuiltinId::MathSub:
        case BuiltinId::MathMul:
        case BuiltinId::MathDiv:
        case BuiltinId::MathMod: {
            double left = requireNamedNumber({"lhs", "left", "base", "y"}, 0, "lhs");
            double right = requireNamedNumber({"rhs", "right", "exponent", "x"}, 1, "rhs");
            switch (term.builtinId) {
                case BuiltinId::MathPow:
                    out = std::make_shared<NumberExpr>(std::pow(left, right));
                    break;
                case BuiltinId::MathAtan2:
                    out = std::make_shared<NumberExpr>(std::atan2(left, right));
                    break;
                case BuiltinId::MathAdd:
                    out = std::make_shared<NumberExpr>(left + right);
                    break;
                case BuiltinId::MathSub:
                    out = std::make_shared<NumberExpr>(left - right);
                    break;
                case BuiltinId::MathMul:
                    out = std::make_shared<NumberExpr>(left * right);
                    break;
                case BuiltinId::MathDiv:
                    if (std::fabs(right) < 1e-12) throw InterpreterError("DivisionByZero");
                    out = std::make_shared<NumberExpr>(left / right);
                    break;
                case BuiltinId::MathMod:
                    if (std::fabs(right) < 1e-12) throw InterpreterError("DivisionByZero");
                    out = std::make_shared<NumberExpr>(std::fmod(left, right));
                    break;
                default:
                    return false;
            }
            return true;
        }
        case BuiltinId::MathSqrt:
        case BuiltinId::MathSin:
        case BuiltinId::MathCos:
        case BuiltinId::MathTan:
        case BuiltinId::MathAsin:
        case BuiltinId::MathAcos:
        case BuiltinId::MathAtan:
        case BuiltinId::MathLog:
        case BuiltinId::MathLog10:
        case BuiltinId::MathExp:
        case BuiltinId::MathAbs:
        case BuiltinId::MathFloor:
        case BuiltinId::MathCeil:
        case BuiltinId::MathRound: {
            double value = requireNamedNumber({"value", "data", "x"}, 0, "value");
            switch (term.builtinId) {
                case BuiltinId::MathSqrt: out = std::make_shared<NumberExpr>(std::sqrt(value)); break;
                case BuiltinId::MathSin: out = std::make_shared<NumberExpr>(std::sin(value)); break;
                case BuiltinId::MathCos: out = std::make_shared<NumberExpr>(std::cos(value)); break;
                case BuiltinId::MathTan: out = std::make_shared<NumberExpr>(std::tan(value)); break;
                case BuiltinId::MathAsin: out = std::make_shared<NumberExpr>(std::asin(value)); break;
                case BuiltinId::MathAcos: out = std::make_shared<NumberExpr>(std::acos(value)); break;
                case BuiltinId::MathAtan: out = std::make_shared<NumberExpr>(std::atan(value)); break;
                case BuiltinId::MathLog: out = std::make_shared<NumberExpr>(std::log(value)); break;
                case BuiltinId::MathLog10: out = std::make_shared<NumberExpr>(std::log10(value)); break;
                case BuiltinId::MathExp: out = std::make_shared<NumberExpr>(std::exp(value)); break;
                case BuiltinId::MathAbs: out = std::make_shared<NumberExpr>(std::fabs(value)); break;
                case BuiltinId::MathFloor: out = std::make_shared<NumberExpr>(std::floor(value)); break;
                case BuiltinId::MathCeil: out = std::make_shared<NumberExpr>(std::ceil(value)); break;
                case BuiltinId::MathRound: out = std::make_shared<NumberExpr>(std::round(value)); break;
                default: break;
            }
            return true;
        }
        default:
            break;
    }

    if (term.name.rfind("probability:", 0) == 0) {
        const std::string op = term.name.substr(12);
        static thread_local std::mt19937 rng{std::random_device{}()};

        auto requireProbability = [&](std::initializer_list<const char*> names, size_t index, const std::string& label) {
            double p = requireNamedNumber(names, index, label);
            if (p < 0.0 || p > 1.0) throw InterpreterError(term.name + " expects " + label + " between 0 and 1");
            return p;
        };

        if (op == "bernoulli") {
            double p = requireProbability({"p", "probability"}, 0, "p");
            std::bernoulli_distribution dist(p);
            out = std::make_shared<BoolExpr>(dist(rng));
            return true;
        }

        if (op == "binomialPmf" || op == "binomialCdf") {
            double trialsNumber = factorialTerm(requireNamedNumber({"trials", "n"}, 0, "trials"), term.name, "trials");
            double successesNumber = factorialTerm(requireNamedNumber({"successes", "k"}, 1, "successes"), term.name, "successes");
            double p = requireProbability({"p", "probability"}, 2, "p");
            int n = static_cast<int>(trialsNumber);
            int k = static_cast<int>(successesNumber);
            if (k > n) {
                out = std::make_shared<NumberExpr>(0.0);
                return true;
            }
            auto pmf = [&](int x) {
                double logComb = std::lgamma(n + 1.0) - std::lgamma(x + 1.0) - std::lgamma(n - x + 1.0);
                if (p == 0.0) return x == 0 ? 1.0 : 0.0;
                if (p == 1.0) return x == n ? 1.0 : 0.0;
                return std::exp(logComb + x * std::log(p) + (n - x) * std::log(1.0 - p));
            };
            double value = 0.0;
            if (op == "binomialPmf") {
                value = pmf(k);
            } else {
                for (int x = 0; x <= k; ++x) value += pmf(x);
            }
            out = std::make_shared<NumberExpr>(value);
            return true;
        }

        if (op == "poissonPmf" || op == "poissonCdf") {
            double lambda = requireNamedNumber({"lambda", "rate"}, 0, "lambda");
            double eventsNumber = factorialTerm(requireNamedNumber({"events", "k"}, 1, "events"), term.name, "events");
            if (lambda < 0.0) throw InterpreterError(term.name + " expects lambda >= 0");
            int k = static_cast<int>(eventsNumber);
            auto pmf = [&](int x) {
                if (lambda == 0.0) return x == 0 ? 1.0 : 0.0;
                return std::exp(x * std::log(lambda) - lambda - std::lgamma(x + 1.0));
            };
            double value = 0.0;
            if (op == "poissonPmf") {
                value = pmf(k);
            } else {
                for (int x = 0; x <= k; ++x) value += pmf(x);
            }
            out = std::make_shared<NumberExpr>(value);
            return true;
        }

        if (op == "normalPdf" || op == "normalCdf") {
            double x = requireNamedNumber({"x", "value"}, 0, "x");
            double mean = term.args.size() > 1 ? requireNamedNumber({"mean", "mu"}, 1, "mean") : 0.0;
            double stddev = term.args.size() > 2 ? requireNamedNumber({"stddev", "sigma"}, 2, "stddev") : 1.0;
            if (stddev <= 0.0) throw InterpreterError(term.name + " expects stddev > 0");
            double z = (x - mean) / stddev;
            double value = op == "normalPdf"
                ? std::exp(-0.5 * z * z) / (stddev * std::sqrt(2.0 * 3.14159265358979323846))
                : 0.5 * (1.0 + std::erf(z / std::sqrt(2.0)));
            out = std::make_shared<NumberExpr>(value);
            return true;
        }

        if (op == "uniformPdf" || op == "uniformCdf") {
            double x = requireNamedNumber({"x", "value"}, 0, "x");
            double min = requireNamedNumber({"min", "a"}, 1, "min");
            double max = requireNamedNumber({"max", "b"}, 2, "max");
            if (max <= min) throw InterpreterError(term.name + " expects max > min");
            double value = 0.0;
            if (op == "uniformPdf") {
                value = (x >= min && x <= max) ? 1.0 / (max - min) : 0.0;
            } else {
                value = x <= min ? 0.0 : (x >= max ? 1.0 : (x - min) / (max - min));
            }
            out = std::make_shared<NumberExpr>(value);
            return true;
        }

        std::shared_ptr<Expr> dataValue;
        if ((op == "mean" || op == "variance" || op == "stddev" || op == "normalize" || op == "entropy") &&
            (!evalNamed("data", 0, dataValue) && !evalNamed("values", 0, dataValue))) {
            throw InterpreterError(term.name + " expects numeric array argument 'data'");
        }

        if (op == "mean" || op == "variance" || op == "stddev" || op == "normalize" || op == "entropy") {
            auto values = requireNumberArray(dataValue, term.name, "data");
            if (values.empty()) throw InterpreterError(term.name + " expects a non-empty array");
            double total = 0.0;
            for (double value : values) total += value;
            if (op == "mean") {
                out = std::make_shared<NumberExpr>(total / static_cast<double>(values.size()));
                return true;
            }
            if (op == "variance" || op == "stddev") {
                double mean = total / static_cast<double>(values.size());
                double squared = 0.0;
                for (double value : values) squared += (value - mean) * (value - mean);
                double variance = squared / static_cast<double>(values.size());
                out = std::make_shared<NumberExpr>(op == "variance" ? variance : std::sqrt(variance));
                return true;
            }
            if (op == "normalize") {
                if (std::fabs(total) < 1e-12) throw InterpreterError("probability.normalize expects a non-zero sum");
                std::vector<double> normalized;
                normalized.reserve(values.size());
                for (double value : values) normalized.push_back(value / total);
                out = numbersToArray(normalized);
                return true;
            }
            double entropy = 0.0;
            for (double p : values) {
                if (p < 0.0) throw InterpreterError("probability.entropy expects non-negative probabilities");
                if (p > 0.0) entropy -= p * std::log2(p);
            }
            out = std::make_shared<NumberExpr>(entropy);
            return true;
        }

        if (op == "covariance" || op == "correlation") {
            std::shared_ptr<Expr> leftValue;
            std::shared_ptr<Expr> rightValue;
            if (!evalNamed("left", 0, leftValue) || !evalNamed("right", 1, rightValue)) {
                throw InterpreterError(term.name + " expects left and right arrays");
            }
            auto left = requireNumberArray(leftValue, term.name, "left");
            auto right = requireNumberArray(rightValue, term.name, "right");
            if (left.empty() || left.size() != right.size()) throw InterpreterError(term.name + " expects arrays of equal non-zero length");
            double leftMean = 0.0;
            double rightMean = 0.0;
            for (size_t i = 0; i < left.size(); ++i) {
                leftMean += left[i];
                rightMean += right[i];
            }
            leftMean /= static_cast<double>(left.size());
            rightMean /= static_cast<double>(right.size());
            double covariance = 0.0;
            double leftVar = 0.0;
            double rightVar = 0.0;
            for (size_t i = 0; i < left.size(); ++i) {
                double ld = left[i] - leftMean;
                double rd = right[i] - rightMean;
                covariance += ld * rd;
                leftVar += ld * ld;
                rightVar += rd * rd;
            }
            covariance /= static_cast<double>(left.size());
            if (op == "covariance") {
                out = std::make_shared<NumberExpr>(covariance);
                return true;
            }
            if (std::fabs(leftVar) < 1e-12 || std::fabs(rightVar) < 1e-12) {
                throw InterpreterError("probability.correlation expects non-constant arrays");
            }
            const double count = static_cast<double>(left.size());
            out = std::make_shared<NumberExpr>(covariance / std::sqrt((leftVar / count) * (rightVar / count)));
            return true;
        }

        if (op == "sample" || op == "weightedChoice") {
            std::shared_ptr<Expr> valuesValue;
            if (!evalNamed("data", 0, valuesValue) && !evalNamed("values", 0, valuesValue)) {
                throw InterpreterError(term.name + " expects array argument 'data'");
            }
            std::vector<std::shared_ptr<Expr>> values;
            if (!exprAsArrayItems(valuesValue, values) || values.empty()) throw InterpreterError(term.name + " expects a non-empty array");
            if (op == "sample") {
                std::uniform_int_distribution<size_t> dist(0, values.size() - 1);
                out = values[dist(rng)]->clone();
                return true;
            }
            std::shared_ptr<Expr> weightsValue;
            if (!evalNamed("weights", 1, weightsValue)) throw InterpreterError("probability.weightedChoice expects weights");
            auto weights = requireNumberArray(weightsValue, term.name, "weights");
            if (weights.size() != values.size()) throw InterpreterError("probability.weightedChoice expects weights length to match data length");
            for (double weight : weights) {
                if (weight < 0.0) throw InterpreterError("probability.weightedChoice expects non-negative weights");
            }
            std::discrete_distribution<size_t> dist(weights.begin(), weights.end());
            out = values[dist(rng)]->clone();
            return true;
        }

        throw InterpreterError("Unknown probability builtin: " + term.name);
    }

    if (term.name.rfind("ml:", 0) == 0) {
        const std::string op = term.name.substr(3);
        if (op == "sigmoid" || op == "relu") {
            double value = requireNamedNumber({"value", "x"}, 0, "value");
            out = std::make_shared<NumberExpr>(op == "sigmoid" ? 1.0 / (1.0 + std::exp(-value)) : std::max(0.0, value));
            return true;
        }
        if (op == "dot" || op == "meanSquaredError") {
            std::shared_ptr<Expr> leftExpr;
            std::shared_ptr<Expr> rightExpr;
            if (!evalNamed("left", 0, leftExpr) || !evalNamed("right", 1, rightExpr)) {
                throw InterpreterError(term.name + " expects left and right arrays");
            }
            std::vector<std::shared_ptr<Expr>> left;
            std::vector<std::shared_ptr<Expr>> right;
            if (!exprAsArray(leftExpr, left) || !exprAsArray(rightExpr, right) || left.size() != right.size()) {
                throw InterpreterError(term.name + " expects arrays of equal length");
            }
            if (left.empty()) throw InterpreterError(term.name + " expects non-empty arrays");
#ifdef FELIDAE_HAS_EIGEN
            Eigen::VectorXd lhs(static_cast<Eigen::Index>(left.size()));
            Eigen::VectorXd rhs(static_cast<Eigen::Index>(right.size()));
            for (size_t i = 0; i < left.size(); ++i) {
                lhs(static_cast<Eigen::Index>(i)) = requireNumber(left[i], term.name, "left");
                rhs(static_cast<Eigen::Index>(i)) = requireNumber(right[i], term.name, "right");
            }
            if (op == "dot") {
                out = std::make_shared<NumberExpr>(lhs.dot(rhs));
            } else {
                out = std::make_shared<NumberExpr>((lhs - rhs).squaredNorm() / static_cast<double>(left.size()));
            }
#else
            double total = 0.0;
            for (size_t i = 0; i < left.size(); ++i) {
                double a = requireNumber(left[i], term.name, "left");
                double b = requireNumber(right[i], term.name, "right");
                total += op == "dot" ? a * b : (a - b) * (a - b);
            }
            out = std::make_shared<NumberExpr>(op == "dot" ? total : total / static_cast<double>(left.size()));
#endif
            return true;
        }
        throw InterpreterError("Unknown ml builtin: " + term.name);
    }

    if (builtin == BuiltinId::ParseDoc) {
        if (args.size() != 1) throw InterpreterError("ParseDoc expects one argument");
        std::string text;
        if (!argAsString(args[0], text)) throw InterpreterError("ParseDoc expects a string");
        out = std::make_shared<StringExpr>("Parsed: " + text);
        return true;
    }

    if (builtin == BuiltinId::Count || builtin == BuiltinId::Length) {
        if (args.size() != 1) throw InterpreterError(term.name + " expects one argument");
        if (auto array = std::dynamic_pointer_cast<ArrayExpr>(args[0])) {
            out = std::make_shared<NumberExpr>(static_cast<double>(array->items.size()));
            return true;
        }
        std::string text;
        if (argAsString(args[0], text)) {
            out = std::make_shared<NumberExpr>(static_cast<double>(text.size()));
            return true;
        }
        auto items = arrayItems(args[0]);
        if (!items.empty()) {
            out = std::make_shared<NumberExpr>(static_cast<double>(items.size()));
            return true;
        }
        throw InterpreterError(term.name + " expects an array, collection, or string");
    }

    if (builtin == BuiltinId::Sum || builtin == BuiltinId::Average) {
        if (args.size() != 1) throw InterpreterError(term.name + " expects one numeric array");
        auto array = std::dynamic_pointer_cast<ArrayExpr>(args[0]);
        if (!array) throw InterpreterError(term.name + " expects a numeric array");
        double total = 0.0;
        for (const auto& item : array->items) {
            double number = 0.0;
            if (!argAsNumber(item, number)) throw InterpreterError(term.name + " expects only numbers");
            total += number;
        }
        if (builtin == BuiltinId::Average && array->items.empty()) throw InterpreterError("average expects a non-empty array");
        out = std::make_shared<NumberExpr>(builtin == BuiltinId::Average ? total / static_cast<double>(array->items.size()) : total);
        return true;
    }

    if (builtin == BuiltinId::Min || builtin == BuiltinId::Max || builtin == BuiltinId::Sort) {
        if (args.size() != 1) throw InterpreterError(term.name + " expects one array");
        auto array = std::dynamic_pointer_cast<ArrayExpr>(args[0]);
        if (!array) throw InterpreterError(term.name + " expects an array");
        std::vector<std::shared_ptr<Expr>> items;
        for (const auto& item : array->items) items.push_back(item->clone());
        auto less = [](const std::shared_ptr<Expr>& lhs, const std::shared_ptr<Expr>& rhs) {
            double ln = 0.0, rn = 0.0;
            if (argAsNumber(lhs, ln) && argAsNumber(rhs, rn)) return ln < rn;
            std::string ls, rs;
            if (argAsString(lhs, ls) && argAsString(rhs, rs)) return ls < rs;
            return lhs->debug() < rhs->debug();
        };
        std::sort(items.begin(), items.end(), less);
        if (builtin == BuiltinId::Sort) {
            out = std::make_shared<ArrayExpr>(std::move(items));
        } else {
            if (items.empty()) throw InterpreterError(term.name + " expects a non-empty array");
            out = builtin == BuiltinId::Min ? items.front() : items.back();
        }
        return true;
    }

    if (builtin == BuiltinId::Contains || builtin == BuiltinId::Search) {
        if (args.size() != 2) throw InterpreterError(term.name + " expects two arguments");
        std::string query;
        if (!argAsString(args[1], query)) throw InterpreterError(term.name + " query must be a string");
        std::string text;
        if (argAsString(args[0], text)) {
            bool found = text.find(query) != std::string::npos;
            out = std::make_shared<BoolExpr>(found);
            return true;
        }
        auto array = std::dynamic_pointer_cast<ArrayExpr>(args[0]);
        if (!array) throw InterpreterError(term.name + " expects a string or array");
        std::vector<std::shared_ptr<Expr>> matches;
        for (const auto& item : array->items) {
            std::string itemText;
            if (argAsString(item, itemText) && itemText.find(query) != std::string::npos) {
                if (builtin == BuiltinId::Contains) {
                    out = std::make_shared<StringExpr>("true");
                    return true;
                }
                matches.push_back(item->clone());
            } else if (builtin == BuiltinId::Contains && exprEqualsLiteral(item, args[1])) {
                out = std::make_shared<StringExpr>("true");
                return true;
            }
        }
        out = builtin == BuiltinId::Contains
            ? std::static_pointer_cast<Expr>(std::make_shared<StringExpr>("false"))
            : std::static_pointer_cast<Expr>(std::make_shared<ArrayExpr>(std::move(matches)));
        return true;
    }

    if (builtin == BuiltinId::Lower || builtin == BuiltinId::Upper) {
        if (args.size() != 1) throw InterpreterError(term.name + " expects one string");
        std::string text;
        if (!argAsString(args[0], text)) throw InterpreterError(term.name + " expects a string");
        out = std::make_shared<StringExpr>(builtin == BuiltinId::Lower ? lowerText(text) : upperText(text));
        return true;
    }

    return false;
}

bool Interpreter::evalExprValue(const std::shared_ptr<Expr>& expr, const Env& env, std::shared_ptr<Expr>& out) {
    if (auto op = std::dynamic_pointer_cast<OperatorExpression>(expr)) {
        return evalOperatorExpr(*op, env, out);
    }
    auto resolved = resolveExpr(expr, env);
    if (auto lambda = std::dynamic_pointer_cast<LambdaExpr>(resolved)) {
        auto sourceValues = valuesForLambdaSource(lambda->source, env);
        // A type selector is a fact query.  Its documented predicate form
        // filters facts, including compound predicates that are represented
        // as a normal boolean expression rather than LambdaExpr::op/right.
        // Array sources retain their mapping behaviour for compatibility.
        const auto sourceVariable = std::dynamic_pointer_cast<VarExpr>(lambda->source);
        const bool sourceIsFactType =
            std::dynamic_pointer_cast<StringExpr>(lambda->source) != nullptr ||
            (sourceVariable && !sourceVariable->name.empty() &&
             std::isupper(static_cast<unsigned char>(sourceVariable->name.front())) &&
             globals_.find(sourceVariable->name) == globals_.end());
        std::vector<std::shared_ptr<Expr>> results;
        for (const auto& item : sourceValues) {
            Env lambdaEnv = env;
            lambdaEnv[lambda->variable] = item;
            PipelineResultClearScope clearPipelineResults(pipelineResults_);
            if (lambda->op != TokenType::End) {
                std::shared_ptr<Expr> left;
                std::shared_ptr<Expr> right;
                if (!evalExprValue(lambda->body, lambdaEnv, left) ||
                    !evalExprValue(lambda->right, lambdaEnv, right)) {
                    continue;
                }
                bool ok = lambda->op == TokenType::EqEq ? unifyExpr(left, right, lambdaEnv) :
                          lambda->op == TokenType::NotEq ? !unifyExpr(left, right, lambdaEnv) :
                          compareResolved(left, lambda->op, right);
                if (ok) results.push_back(item->clone());
                continue;
            }
            std::shared_ptr<Expr> mapped;
            if (evalExprValue(lambda->body, lambdaEnv, mapped) && !isMethodTruthTupleWithFalse(mapped)) {
                if (sourceIsFactType) {
                    if (const auto predicate = std::dynamic_pointer_cast<BoolExpr>(mapped)) {
                        if (predicate->value) results.push_back(item->clone());
                    } else {
                        results.push_back(mapped);
                    }
                } else {
                    results.push_back(mapped);
                }
            }
        }
        out = std::make_shared<ArrayExpr>(std::move(results));
        return true;
    }
    if (auto term = std::dynamic_pointer_cast<TermExpr>(resolved)) {
        return evalBuiltinTerm(*term, env, out);
    }
    if (auto array = std::dynamic_pointer_cast<ArrayExpr>(resolved)) {
        std::vector<std::shared_ptr<Expr>> items;
        items.reserve(array->items.size());
        for (const auto& item : array->items) {
            std::shared_ptr<Expr> value;
            if (!evalExprValue(item, env, value)) return false;
            items.push_back(value);
        }
        out = std::make_shared<ArrayExpr>(std::move(items));
        return true;
    }
    if (auto map = std::dynamic_pointer_cast<MapExpr>(resolved)) {
        std::vector<MapEntry> entries;
        entries.reserve(map->entries.size());
        for (const auto& entry : map->entries) {
            std::shared_ptr<Expr> value;
            if (!evalExprValue(entry.value, env, value)) return false;
            entries.push_back(MapEntry{entry.key, value});
        }
        auto evaluated = std::make_shared<MapExpr>(std::move(entries));
        // Evaluating a stored fact resolves field expressions but must not
        // discard its runtime-only identity.  This keeps aliases and values
        // retrieved through Fact.all/select attached to the same fact record.
        evaluated->factIdentity = map->factIdentity;
        evaluated->factType = map->factType;
        out = std::move(evaluated);
        return true;
    }
    if (auto access = std::dynamic_pointer_cast<AccessExpr>(resolved)) {
        std::shared_ptr<Expr> target;
        if (!evalExprValue(access->target, env, target)) return false;
        auto value = findMapValue(target, access->key);
        if (!value) return false;
        return evalExprValue(value, env, out);
    }
    if (auto var = std::dynamic_pointer_cast<VarExpr>(resolved)) {
        auto globalIt = globals_.find(var->name);
        if (globalIt != globals_.end()) return evalExprValue(globalIt->second, env, out);
        const SymbolId designationId = symbolIdForName(var->name);
        if (memory_.hasDesignation(designationId)) {
            out = std::make_shared<FactSelectionExpr>(
                "", memory_.captureSnapshot(), "", nullptr,
                std::vector<SymbolId>{designationId},
                std::vector<std::string>{var->name});
            return true;
        }
        return false;
    }
    out = resolved->clone();
    return true;
}

bool Interpreter::evalOperatorExpr(const OperatorExpression& expression,
                                   const Env& env,
                                   std::shared_ptr<Expr>& out) {
    if (expression.coreOperator != CoreOperator::Unknown &&
        operatorClauses_.count(expression.patternId) > 0) {
        bool matched = false;
        const bool evaluated = evalCustomOperatorExpr(expression, env, out, &matched);
        if (matched) return evaluated;
    }
    if (expression.coreOperator == CoreOperator::Unknown) {
        return evalCustomOperatorExpr(expression, env, out);
    }
    const auto selectionFieldFilter = [&](const std::shared_ptr<Expr>& candidate,
                                          const std::shared_ptr<Expr>& expected,
                                          TokenType op,
                                          std::shared_ptr<FactSelectionExpr>& selectionOut) {
        const auto access = std::dynamic_pointer_cast<AccessExpr>(candidate);
        if (!access) return false;
        std::shared_ptr<Expr> source;
        if (!evalExprValue(access->target, env, source)) return false;
        const auto selection = std::dynamic_pointer_cast<FactSelectionExpr>(source);
        if (!selection) return false;
        std::shared_ptr<Expr> value;
        if (!evalExprValue(expected, env, value)) return false;
        auto filtered = std::static_pointer_cast<FactSelectionExpr>(selection->clone());
        filtered->filters.push_back(FactSelectionFilter{
            access->key, access->keyId, op, std::move(value)});
        selectionOut = std::move(filtered);
        return true;
    };
    const auto comparisonToken = [&](CoreOperator op) {
        switch (op) {
            case CoreOperator::StrictEqual: return TokenType::EqEq;
            case CoreOperator::StrictNotEqual: return TokenType::NotEq;
            case CoreOperator::Less:
            case CoreOperator::LessEqual:
            case CoreOperator::Greater:
            case CoreOperator::GreaterEqual:
                return coreOperatorDefinition(op).token;
            default:
                return TokenType::End;
        }
    };
    if (expression.captureCount() == 2) {
        const TokenType filterOp = comparisonToken(expression.coreOperator);
        if (filterOp != TokenType::End) {
            std::shared_ptr<FactSelectionExpr> filtered;
            if (selectionFieldFilter(expression.capture(0), expression.capture(1), filterOp, filtered)) {
                out = std::move(filtered);
                return true;
            }
        }
    }
    const auto booleanValue = [](const std::shared_ptr<Expr>& value,
                                 const char* operatorName) {
        if (auto boolean = std::dynamic_pointer_cast<BoolExpr>(value)) return boolean->value;
        if (auto text = std::dynamic_pointer_cast<StringExpr>(value)) {
            if (text->value == "true") return true;
            if (text->value == "false") return false;
        }
        throw InterpreterError(std::string("Logical operator '") + operatorName +
                               "' expects boolean operands");
    };
    switch (expression.coreOperator) {
        case CoreOperator::Then: {
            if (expression.captureCount() != 2) throw InterpreterError("Invalid then operator shape");
            std::shared_ptr<Expr> previous;
            if (!evalExprValue(expression.capture(0), env, previous) ||
                std::dynamic_pointer_cast<NilExpr>(previous)) {
                out = std::make_shared<NilExpr>();
                return true;
            }
            PipelineResultValueScope pipelineResult(pipelineResults_, previous);
            std::shared_ptr<Expr> next;
            if (!evalExprValue(expression.capture(1), env, next) ||
                std::dynamic_pointer_cast<NilExpr>(next)) {
                out = std::make_shared<NilExpr>();
                return true;
            }
            out = std::move(next);
            return true;
        }
        case CoreOperator::UnaryMinus:
        case CoreOperator::UnaryPlus: {
            if (expression.captureCount() != 1) throw InterpreterError("Invalid unary operator shape");
            std::shared_ptr<Expr> operand;
            if (!evalExprValue(expression.capture(0), env, operand)) return false;
            const auto number = std::dynamic_pointer_cast<NumberExpr>(operand);
            if (!number) throw InterpreterError("Unary numeric operator expects a number");
            out = std::make_shared<NumberExpr>(
                expression.coreOperator == CoreOperator::UnaryMinus ? -number->value : number->value);
            return true;
        }
        case CoreOperator::LogicalNot: {
            if (expression.captureCount() != 1) throw InterpreterError("Invalid logical not shape");
            std::shared_ptr<Expr> operand;
            if (!evalExprValue(expression.capture(0), env, operand)) return false;
            out = std::make_shared<BoolExpr>(!booleanValue(operand, "not"));
            return true;
        }
        case CoreOperator::LogicalAnd:
        case CoreOperator::LogicalOr: {
            if (expression.captureCount() != 2) throw InterpreterError("Invalid logical operator shape");
            std::shared_ptr<Expr> left;
            if (!evalExprValue(expression.capture(0), env, left)) return false;
            if (expression.coreOperator == CoreOperator::LogicalAnd) {
                if (const auto selection = std::dynamic_pointer_cast<FactSelectionExpr>(left)) {
                    const auto right = std::dynamic_pointer_cast<OperatorExpression>(expression.capture(1));
                    const TokenType filterOp = right ? comparisonToken(right->coreOperator) : TokenType::End;
                    const auto field = right && right->captureCount() == 2
                        ? std::dynamic_pointer_cast<VarExpr>(right->capture(0)) : nullptr;
                    if (filterOp != TokenType::End && field) {
                        std::shared_ptr<Expr> value;
                        if (!evalExprValue(right->capture(1), env, value)) return false;
                        auto filtered = std::static_pointer_cast<FactSelectionExpr>(selection->clone());
                        filtered->filters.push_back(FactSelectionFilter{
                            field->name, field->nameId, filterOp, std::move(value)});
                        out = std::move(filtered);
                        return true;
                    }
                }
            }
            const bool leftTruth = booleanValue(
                left, expression.coreOperator == CoreOperator::LogicalAnd ? "and" : "or");
            if ((expression.coreOperator == CoreOperator::LogicalAnd && !leftTruth) ||
                (expression.coreOperator == CoreOperator::LogicalOr && leftTruth)) {
                out = std::make_shared<BoolExpr>(leftTruth);
                return true;
            }
            std::shared_ptr<Expr> right;
            if (!evalExprValue(expression.capture(1), env, right)) return false;
            out = std::make_shared<BoolExpr>(booleanValue(
                right, expression.coreOperator == CoreOperator::LogicalAnd ? "and" : "or"));
            return true;
        }
        case CoreOperator::Add:
        case CoreOperator::Subtract:
        case CoreOperator::Multiply:
        case CoreOperator::Divide:
        case CoreOperator::Modulo:
        case CoreOperator::StrictEqual:
        case CoreOperator::StrictNotEqual:
        case CoreOperator::Less:
        case CoreOperator::LessEqual:
        case CoreOperator::Greater:
        case CoreOperator::GreaterEqual:
            break;
        default:
            throw InterpreterError("Unsupported operator id " + std::to_string(expression.operatorId));
    }

    if (expression.captureCount() != 2) throw InterpreterError("Invalid binary operator shape");
    std::shared_ptr<Expr> left;
    std::shared_ptr<Expr> right;
    if (!evalExprValue(expression.capture(0), env, left) ||
        !evalExprValue(expression.capture(1), env, right)) {
        return false;
    }

    switch (expression.coreOperator) {
        case CoreOperator::StrictEqual:
        case CoreOperator::StrictNotEqual: {
            const bool equal = exprEqualsLiteral(left, right);
            out = std::make_shared<BoolExpr>(
                expression.coreOperator == CoreOperator::StrictEqual ? equal : !equal);
            return true;
        }
        case CoreOperator::Less:
        case CoreOperator::LessEqual:
        case CoreOperator::Greater:
        case CoreOperator::GreaterEqual:
            out = std::make_shared<BoolExpr>(
                compareResolved(left, coreOperatorDefinition(expression.coreOperator).token, right));
            return true;
        default:
            break;
    }

    const auto leftNumber = std::dynamic_pointer_cast<NumberExpr>(left);
    const auto rightNumber = std::dynamic_pointer_cast<NumberExpr>(right);
    if (!leftNumber || !rightNumber) {
        throw InterpreterError(
            "Operator '" + std::string(coreOperatorDefinition(expression.coreOperator).spelling) +
            "' expects numeric operands");
    }
    switch (expression.coreOperator) {
        case CoreOperator::Add:
            out = std::make_shared<NumberExpr>(leftNumber->value + rightNumber->value);
            return true;
        case CoreOperator::Subtract:
            out = std::make_shared<NumberExpr>(leftNumber->value - rightNumber->value);
            return true;
        case CoreOperator::Multiply:
            out = std::make_shared<NumberExpr>(leftNumber->value * rightNumber->value);
            return true;
        case CoreOperator::Divide:
        case CoreOperator::Modulo:
            if (std::fabs(rightNumber->value) < 1e-12) throw InterpreterError("DivisionByZero");
            out = std::make_shared<NumberExpr>(
                expression.coreOperator == CoreOperator::Divide
                    ? leftNumber->value / rightNumber->value
                    : std::fmod(leftNumber->value, rightNumber->value));
            return true;
        default:
            break;
    }
    throw InterpreterError("Invalid numeric operator id " + std::to_string(expression.operatorId));
}

bool Interpreter::evalCustomOperatorExpr(const OperatorExpression& expression,
                                         const Env& env,
                                         std::shared_ptr<Expr>& out,
                                         bool* matched) {
    if (matched) *matched = false;
    const auto clauses = operatorClauses_.find(expression.patternId);
    if (clauses == operatorClauses_.end()) {
        if (matched != nullptr) return false;
        throw InterpreterError("No overload implementation is registered for operator pattern " +
                               std::to_string(expression.patternId));
    }
    struct ResolvedOperatorType {
        LanguageTypeId languageType = LanguageTypeId::Unknown;
        SymbolId factTypeId = 0;
        std::string factType;

        bool known() const {
            return languageType != LanguageTypeId::Unknown || factTypeId != 0;
        }
    };
    const auto factRuntimeType = [](std::string name) {
        if (name.empty()) return ResolvedOperatorType{};
        return ResolvedOperatorType{
            LanguageTypeId::Unknown, symbolIdForName(name), std::move(name)};
    };
    const auto builtinRuntimeType = [](LanguageTypeId type) {
        return ResolvedOperatorType{type, 0, {}};
    };
    const auto sameResolvedType = [](const ResolvedOperatorType& left,
                                     const ResolvedOperatorType& right) {
        if (left.languageType != LanguageTypeId::Unknown ||
            right.languageType != LanguageTypeId::Unknown) {
            return left.languageType == right.languageType;
        }
        return left.factTypeId == right.factTypeId &&
            left.factType == right.factType;
    };
    const auto compatibleLanguageType = [](LanguageTypeId actual,
                                           LanguageTypeId expected) {
        if (actual == expected) return true;
        const auto numeric = [](LanguageTypeId type) {
            return type == LanguageTypeId::Number ||
                   type == LanguageTypeId::Decimal ||
                   type == LanguageTypeId::Double ||
                   type == LanguageTypeId::Float ||
                   type == LanguageTypeId::Int;
        };
        if (numeric(actual) && numeric(expected)) {
            // A general numeric result cannot promise integral output.
            return expected != LanguageTypeId::Int || actual == LanguageTypeId::Int;
        }
        return (actual == LanguageTypeId::Bool || actual == LanguageTypeId::Boolean) &&
               (expected == LanguageTypeId::Bool || expected == LanguageTypeId::Boolean);
    };
    const auto valueType = [&](const std::shared_ptr<Expr>& value) {
        if (auto ast = std::dynamic_pointer_cast<AstValueExpr>(value)) {
            return builtinRuntimeType(
                ast->valueKind == AstValueKind::Expression
                    ? LanguageTypeId::Expr
                    : ast->valueKind == AstValueKind::Statement
                        ? LanguageTypeId::Stmt
                        : LanguageTypeId::Statements);
        }
        if (std::dynamic_pointer_cast<NumberExpr>(value)) {
            return builtinRuntimeType(LanguageTypeId::Number);
        }
        if (std::dynamic_pointer_cast<StringExpr>(value)) {
            return builtinRuntimeType(LanguageTypeId::String);
        }
        if (std::dynamic_pointer_cast<BoolExpr>(value)) {
            return builtinRuntimeType(LanguageTypeId::Bool);
        }
        if (std::dynamic_pointer_cast<ArrayExpr>(value)) {
            return builtinRuntimeType(LanguageTypeId::Array);
        }
        if (const auto map = std::dynamic_pointer_cast<MapExpr>(value)) {
            if (!map->factType.empty()) return factRuntimeType(map->factType);
            const auto type = std::dynamic_pointer_cast<StringExpr>(
                findMapValue(value, internalSymbolString(InternalSymbolKind::Type)));
            if (type) return factRuntimeType(type->value);
        }
        return ResolvedOperatorType{};
    };

    // Overload selection must not execute captures. Resolve only immutable
    // values already present in the frame and direct structural metadata.
    const auto metadataValue = [&](const auto& self,
                                   const std::shared_ptr<Expr>& syntax,
                                   std::unordered_set<SymbolId>& resolving)
        -> std::shared_ptr<Expr> {
        if (auto ast = std::dynamic_pointer_cast<AstValueExpr>(syntax)) {
            if (ast->valueKind == AstValueKind::Expression && ast->nodes.size() == 1) {
                if (const auto nested = std::dynamic_pointer_cast<Expr>(ast->nodes.front())) {
                    return self(self, nested, resolving);
                }
            }
            return syntax;
        }
        if (auto variable = std::dynamic_pointer_cast<VarExpr>(syntax)) {
            if (variable->nameId == InternalSymbol::SystemResultId &&
                !pipelineResults_.empty()) {
                return pipelineResults_.back();
            }
            if (!resolving.insert(variable->nameId).second) return syntax;
            auto local = env.find(variable->name);
            if (local != env.end()) return self(self, local->second, resolving);
            auto global = globals_.find(variable->name);
            if (global != globals_.end()) return self(self, global->second, resolving);
            return syntax;
        }
        if (auto access = std::dynamic_pointer_cast<AccessExpr>(syntax)) {
            const auto targetVariable = std::dynamic_pointer_cast<VarExpr>(access->target);
            if (access->keyId == InternalSymbol::ResultId && targetVariable &&
                targetVariable->nameId == InternalSymbol::SystemId &&
                !pipelineResults_.empty()) {
                return pipelineResults_.back();
            }
            auto target = self(self, access->target, resolving);
            auto field = findMapValue(target, access->key);
            return field ? self(self, field, resolving) : syntax;
        }
        return syntax;
    };
    const auto isRegisteredMixfix = [&](const std::shared_ptr<Expr>& syntax) {
        const auto nested = std::dynamic_pointer_cast<OperatorExpression>(syntax);
        if (!nested || nested->coreOperator != CoreOperator::Unknown) return false;
        const auto* pattern = operators_->findPatternById(nested->patternId);
        return pattern != nullptr && pattern->isMixfixDeclaration;
    };
    const auto expressionType = [&](const std::shared_ptr<Expr>& syntax) {
        std::unordered_set<SymbolId> resolving;
        auto metadata = metadataValue(metadataValue, syntax, resolving);
        const ResolvedOperatorType direct = valueType(metadata);
        if (direct.known()) return direct;
        if (auto term = std::dynamic_pointer_cast<TermExpr>(metadata)) {
            if (!term->name.empty() &&
                std::isupper(static_cast<unsigned char>(term->name.front()))) {
                return factRuntimeType(term->name);
            }
            return ResolvedOperatorType{};
        }
        if (auto nested = std::dynamic_pointer_cast<OperatorExpression>(metadata)) {
            if (nested->coreOperator != CoreOperator::Unknown) {
                if (isComparisonOperator(nested->coreOperator)) {
                    return builtinRuntimeType(LanguageTypeId::Bool);
                }
                if (nested->coreOperator == CoreOperator::LogicalAnd ||
                    nested->coreOperator == CoreOperator::LogicalOr ||
                    nested->coreOperator == CoreOperator::LogicalNot) {
                    return builtinRuntimeType(LanguageTypeId::Bool);
                }
                if (nested->coreOperator == CoreOperator::Add ||
                    nested->coreOperator == CoreOperator::Subtract ||
                    nested->coreOperator == CoreOperator::Multiply ||
                    nested->coreOperator == CoreOperator::Divide ||
                    nested->coreOperator == CoreOperator::Modulo ||
                    nested->coreOperator == CoreOperator::UnaryPlus ||
                    nested->coreOperator == CoreOperator::UnaryMinus) {
                    return builtinRuntimeType(LanguageTypeId::Number);
                }
            }
            ResolvedOperatorType resultType;
            for (const auto* overload : operators_->overloadsForPattern(nested->patternId)) {
                if (overload->visibility == OperatorVisibility::Private &&
                    overload->module != nested->module) continue;
                const ResolvedOperatorType candidate =
                    overload->resultLanguageTypeId != LanguageTypeId::Unknown
                        ? builtinRuntimeType(overload->resultLanguageTypeId)
                        : factRuntimeType(overload->resultType);
                if (!resultType.known()) resultType = candidate;
                else if (!sameResolvedType(resultType, candidate)) {
                    return ResolvedOperatorType{};
                }
            }
            return resultType;
        }
        return ResolvedOperatorType{};
    };
    std::vector<ResolvedOperatorType> captureTypes;
    captureTypes.reserve(expression.captureCount());
    for (size_t i = 0; i < expression.captureCount(); ++i) {
        captureTypes.push_back(expressionType(expression.capture(i)));
    }

    struct Candidate {
        const OperatorOverloadDefinition* overload = nullptr;
        std::shared_ptr<ClauseStmt> clause;
        int score = 0;
        std::vector<std::shared_ptr<Expr>> factorPrototypes;
    };
    std::vector<Candidate> candidates;
    const auto hierarchyRank = [&](const ResolvedOperatorType& actual,
                                   const OperatorTypeBinding& expected) {
        if (actual.factTypeId == expected.typeId && actual.factType == expected.type) {
            return 100;
        }
        std::vector<std::pair<std::string, int>> pending{{actual.factType, 0}};
        std::unordered_set<std::string> visited;
        for (size_t index = 0; index < pending.size(); ++index) {
            const auto [current, distance] = pending[index];
            if (!visited.insert(current).second) continue;
            for (const auto& parent : memory_.parentsOf(current)) {
                if (symbolIdForName(parent) == expected.typeId &&
                    parent == expected.type) {
                    return std::max(20, 80 - distance - 1);
                }
                pending.emplace_back(parent, distance + 1);
            }
        }
        return -1;
    };
    const auto captureScore = [&](const std::vector<OperatorTypeBinding>& captures,
                                  int& score) {
        if (captures.size() != captureTypes.size()) return false;
        for (size_t i = 0; i < captureTypes.size(); ++i) {
            const auto& actual = captureTypes[i];
            const auto& expected = captures[i];
            if (expected.languageTypeId == LanguageTypeId::Mixfix) {
                std::unordered_set<SymbolId> resolving;
                const auto metadata = metadataValue(
                    metadataValue, expression.capture(i), resolving);
                if (!isRegisteredMixfix(metadata)) return false;
                // mixfix is a structural refinement of expr, so it always
                // outranks the broad code-as-data fallback.
                score += 120;
                continue;
            }
            if (expected.languageTypeId == LanguageTypeId::Expr) {
                // expr is the generic code-as-data fallback. A concrete fact
                // type must win when the nested expression has a known result.
                score += 5;
                continue;
            }
            if (expected.languageTypeId == LanguageTypeId::Any) {
                score += 10;
                continue;
            }
            // Unknown expression type cannot satisfy a concrete overload.
            // Previously this path skipped validation, allowing a typed
            // mixfix overload to win before runtime invocation failed.
            if (!actual.known()) return false;
            if (expected.languageTypeId == LanguageTypeId::Fact) {
                if (actual.factTypeId == 0) return false;
                score += 10;
                continue;
            }
            if (expected.languageTypeId != LanguageTypeId::Unknown) {
                std::unordered_set<SymbolId> resolving;
                auto metadata = metadataValue(
                    metadataValue, expression.capture(i), resolving);
                if (!valueMatchesBuiltinType(metadata, expected.languageTypeId) &&
                    !compatibleLanguageType(actual.languageType,
                                            expected.languageTypeId)) {
                    return false;
                }
                score += actual.languageType == expected.languageTypeId ? 100 : 80;
            } else if (actual.factTypeId == expected.typeId &&
                       actual.factType == expected.type) {
                score += 100;
            } else {
                const int rank = hierarchyRank(actual, expected);
                if (rank < 0) return false;
                score += rank;
            }
        }
        return true;
    };
    const auto matchingClause = [&](SymbolId methodId,
                                    const std::string& methodName) -> std::shared_ptr<ClauseStmt> {
        for (const auto& clause : clauses->second) {
            if (clause->head.nameId == methodId && clause->head.name == methodName) return clause;
        }
        return {};
    };
    for (const auto* overloadPtr : operators_->overloadsForPattern(expression.patternId)) {
        const auto& overload = *overloadPtr;
        if (overload.visibility == OperatorVisibility::Private && overload.module != expression.module) continue;
        int score = 0;
        if (!captureScore(overload.captures, score)) continue;
        if (overload.visibility == OperatorVisibility::Private &&
            overload.module == expression.module) ++score;
        const auto overloadClause = matchingClause(overload.methodId, overload.methodName);
        if (!overloadClause) continue;
        if (overload.factors.empty()) {
            candidates.push_back(Candidate{&overload, overloadClause, score, {}});
            continue;
        }
        for (const auto* matcherPtr : operators_->matchersForPattern(
                 expression.patternId, overload.factors.size())) {
            const auto& matcher = *matcherPtr;
            if (matcher.visibility == OperatorVisibility::Private && matcher.module != expression.module) continue;
            int matcherScore = score;
            if (!captureScore(matcher.captures, matcherScore)) continue;

            std::vector<std::size_t> producedForFactor(overload.factors.size(), matcher.produces.size());
            std::vector<bool> used(matcher.produces.size(), false);
            bool signatureMatches = true;
            for (size_t factorIndex = 0; factorIndex < overload.factors.size(); ++factorIndex) {
                const auto& required = overload.factors[factorIndex];
                int best = -1;
                std::size_t bestIndex = matcher.produces.size();
                for (size_t producedIndex = 0; producedIndex < matcher.produces.size(); ++producedIndex) {
                    if (used[producedIndex]) continue;
                    const auto& produced = matcher.produces[producedIndex];
                    int rank = -1;
                    if (produced.typeId == required.typeId &&
                        produced.type == required.type) {
                        rank = 100;
                    } else {
                        rank = hierarchyRank(
                            factRuntimeType(produced.type), required);
                    }
                    if (rank > best) {
                        best = rank;
                        bestIndex = producedIndex;
                    }
                }
                if (bestIndex == matcher.produces.size()) {
                    signatureMatches = false;
                    break;
                }
                used[bestIndex] = true;
                producedForFactor[factorIndex] = bestIndex;
                matcherScore += 1000 + best;
            }
            if (!signatureMatches) continue;
            const auto matcherClause = matchingClause(matcher.methodId, matcher.methodName);
            if (!matcherClause) continue;
            const auto nodeKindFor = [](const std::shared_ptr<Expr>& syntax) {
                if (std::dynamic_pointer_cast<StringExpr>(syntax)) return std::string("string");
                if (std::dynamic_pointer_cast<NumberExpr>(syntax)) return std::string("number");
                if (std::dynamic_pointer_cast<BoolExpr>(syntax)) return std::string("bool");
                if (std::dynamic_pointer_cast<NilExpr>(syntax)) return std::string("nil");
                if (std::dynamic_pointer_cast<VarExpr>(syntax)) return std::string("variable");
                if (std::dynamic_pointer_cast<TermExpr>(syntax)) return std::string("call");
                if (std::dynamic_pointer_cast<OperatorExpression>(syntax)) return std::string("operator");
                if (std::dynamic_pointer_cast<ArrayExpr>(syntax)) return std::string("array");
                if (std::dynamic_pointer_cast<MapExpr>(syntax)) return std::string("map");
                if (std::dynamic_pointer_cast<AccessExpr>(syntax)) return std::string("access");
                return std::string("expression");
            };
            const auto spanValue = [](const SourceSpan& span) {
                std::vector<MapEntry> fields;
                fields.emplace_back("startLine", std::make_shared<NumberExpr>(span.startLine));
                fields.emplace_back("startColumn", std::make_shared<NumberExpr>(span.startColumn));
                fields.emplace_back("endLine", std::make_shared<NumberExpr>(span.endLine));
                fields.emplace_back("endColumn", std::make_shared<NumberExpr>(span.endColumn));
                return std::make_shared<MapExpr>(std::move(fields));
            };
            const auto syntaxShape = [&](const auto& self,
                                         const std::shared_ptr<Expr>& syntax) -> std::shared_ptr<Expr> {
                std::vector<MapEntry> fields;
                fields.emplace_back("nodeKind", std::make_shared<StringExpr>(nodeKindFor(syntax)));
                fields.emplace_back("sourceSpan", spanValue(syntax->sourceSpan));
                std::vector<std::shared_ptr<Expr>> children;
                if (auto nested = std::dynamic_pointer_cast<OperatorExpression>(syntax)) {
                    fields.emplace_back("operatorId", std::make_shared<NumberExpr>(nested->operatorId));
                    fields.emplace_back("patternId", std::make_shared<NumberExpr>(nested->patternId));
                    for (size_t child = 0; child < nested->captureCount(); ++child) {
                        children.push_back(self(self, nested->capture(child)));
                    }
                } else if (auto term = std::dynamic_pointer_cast<TermExpr>(syntax)) {
                    for (const auto& argument : term->args) children.push_back(self(self, argument.value));
                } else if (auto access = std::dynamic_pointer_cast<AccessExpr>(syntax)) {
                    children.push_back(self(self, access->target));
                } else if (auto array = std::dynamic_pointer_cast<ArrayExpr>(syntax)) {
                    for (const auto& item : array->items) children.push_back(self(self, item));
                } else if (auto map = std::dynamic_pointer_cast<MapExpr>(syntax)) {
                    for (const auto& field : map->entries) children.push_back(self(self, field.value));
                }
                fields.emplace_back("children", std::make_shared<ArrayExpr>(std::move(children)));
                return std::make_shared<MapExpr>(std::move(fields));
            };
            const auto expressionReference = [&](size_t index) {
                const auto& syntax = expression.capture(index);
                std::string nodeKind = nodeKindFor(syntax);
                std::string literalKind;
                std::shared_ptr<Expr> literalValue = std::make_shared<NilExpr>();
                if (auto stringLiteral = std::dynamic_pointer_cast<StringExpr>(syntax)) {
                    nodeKind = literalKind = "string";
                    literalValue = stringLiteral->clone();
                } else if (auto numberLiteral = std::dynamic_pointer_cast<NumberExpr>(syntax)) {
                    nodeKind = literalKind = "number";
                    literalValue = numberLiteral->clone();
                } else if (auto boolLiteral = std::dynamic_pointer_cast<BoolExpr>(syntax)) {
                    nodeKind = literalKind = "bool";
                    literalValue = boolLiteral->clone();
                } else if (std::dynamic_pointer_cast<NilExpr>(syntax)) {
                    nodeKind = literalKind = "nil";
                } else if (auto identifier = std::dynamic_pointer_cast<VarExpr>(syntax)) {
                    literalKind = "identifier";
                    literalValue = std::make_shared<StringExpr>(identifier->name);
                }
                std::vector<MapEntry> fields;
                fields.emplace_back(internalSymbolString(InternalSymbolKind::Type),
                                    std::make_shared<StringExpr>("ExpressionRef"));
                fields.emplace_back("nodeKind", std::make_shared<StringExpr>(nodeKind));
                fields.emplace_back("captureName", std::make_shared<StringExpr>(
                    std::string(expression.captureName(index))));
                const auto& inferred = captureTypes[index];
                fields.emplace_back("inferredType", std::make_shared<StringExpr>(
                    inferred.languageType != LanguageTypeId::Unknown
                        ? std::string(languageTypeName(inferred.languageType))
                        : inferred.factType));
                fields.emplace_back("literalKind", std::make_shared<StringExpr>(literalKind));
                fields.emplace_back("literalValue", std::move(literalValue));
                fields.emplace_back("explicitlyGrouped", std::make_shared<BoolExpr>(
                    std::dynamic_pointer_cast<OperatorExpression>(syntax)
                        ? std::dynamic_pointer_cast<OperatorExpression>(syntax)->explicitlyGrouped
                        : false));
                fields.emplace_back("sourceSpan", spanValue(syntax->sourceSpan));
                auto shape = std::dynamic_pointer_cast<MapExpr>(syntaxShape(syntaxShape, syntax));
                fields.emplace_back("children", findMapValue(shape, "children")->clone());
                return std::make_shared<MapExpr>(std::move(fields));
            };
            const OperatorPatternDefinition* resolvedPattern =
                operators_->findPatternById(expression.patternId);
            const std::string operatorName =
                resolvedPattern ? resolvedPattern->operatorName : std::string{};
            std::vector<std::shared_ptr<Expr>> referenceValues;
            referenceValues.reserve(captureTypes.size());
            std::vector<std::shared_ptr<Expr>> contextCaptures;
            contextCaptures.reserve(captureTypes.size());
            for (size_t i = 0; i < captureTypes.size(); ++i) {
                auto reference = expressionReference(i);
                referenceValues.push_back(reference);
                contextCaptures.push_back(reference->clone());
            }
            std::vector<MapEntry> contextFields;
            contextFields.emplace_back(internalSymbolString(InternalSymbolKind::Type),
                                       std::make_shared<StringExpr>("OperatorContext"));
            contextFields.emplace_back("operator", std::make_shared<StringExpr>(operatorName));
            contextFields.emplace_back("operatorId", std::make_shared<NumberExpr>(expression.operatorId));
            contextFields.emplace_back("patternId", std::make_shared<NumberExpr>(expression.patternId));
            contextFields.emplace_back("pattern", std::make_shared<StringExpr>(
                resolvedPattern ? resolvedPattern->pattern : std::string{}));
            contextFields.emplace_back("precedence", std::make_shared<NumberExpr>(
                resolvedPattern ? static_cast<int>(resolvedPattern->precedence) : 0));
            contextFields.emplace_back("associativity", std::make_shared<StringExpr>(
                !resolvedPattern ? "none" :
                resolvedPattern->associativity == OperatorAssociativity::Left ? "left" :
                resolvedPattern->associativity == OperatorAssociativity::Right ? "right" : "none"));
            contextFields.emplace_back("explicitlyGrouped",
                                       std::make_shared<BoolExpr>(expression.explicitlyGrouped));
            contextFields.emplace_back("sourceSpan", spanValue(expression.sourceSpan));
            contextFields.emplace_back("captures", std::make_shared<ArrayExpr>(std::move(contextCaptures)));
            Env guardEnv;
            guardEnv["context"] = std::make_shared<MapExpr>(std::move(contextFields));
            for (size_t i = 0; i < matcher.captures.size(); ++i) {
                guardEnv[matcher.captures[i].name] = referenceValues[i];
            }
            bool guardsMatch = true;
            for (const auto& goal : matcherClause->body) {
                if (!std::dynamic_pointer_cast<WhereGoal>(goal)) continue;
                std::vector<Solution> guardSolutions;
                solveRecursive({goal}, guardEnv, guardSolutions, 1, 0);
                if (guardSolutions.empty()) {
                    guardsMatch = false;
                    break;
                }
            }
            if (!guardsMatch) continue;
            const ReturnGoal* returned = nullptr;
            for (const auto& goal : matcherClause->body) {
                if (auto result = std::dynamic_pointer_cast<ReturnGoal>(goal)) returned = result.get();
            }
            if (!returned || returned->fields.size() != 1) continue;
            const auto wrapper = std::dynamic_pointer_cast<TermExpr>(returned->fields.front().value);
            if (!wrapper) continue;

            std::vector<std::shared_ptr<Expr>> factorPrototypes;
            factorPrototypes.reserve(overload.factors.size());
            for (size_t factorIndex = 0; factorIndex < overload.factors.size(); ++factorIndex) {
                const auto& produced = matcher.produces[producedForFactor[factorIndex]];
                const Arg* prototype = nullptr;
                for (const auto& field : wrapper->args) {
                    if (field.nameId == produced.nameId && field.name == produced.name) {
                        prototype = &field;
                        break;
                    }
                }
                if (!prototype) {
                    signatureMatches = false;
                    break;
                }
                factorPrototypes.push_back(prototype->value);
            }
            if (signatureMatches) {
                candidates.push_back(Candidate{
                    &overload, overloadClause, matcherScore, std::move(factorPrototypes)});
            }
        }
    }
    if (candidates.empty()) {
        if (matched != nullptr) return false;
        throw InterpreterError("No typed overload matches the operator captures");
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) {
        return left.score > right.score;
    });
    if (candidates.size() > 1 && candidates[0].score == candidates[1].score) {
        throw InterpreterError("Ambiguous operator overload: multiple implementations have equal specificity");
    }
    const Candidate& selected = candidates.front();
    if (matched) *matched = true;
    std::vector<std::shared_ptr<Expr>> values;
    values.reserve(expression.captureCount());
    for (size_t i = 0; i < expression.captureCount(); ++i) {
        std::shared_ptr<Expr> value;
        const auto captureType = selected.overload->captures[i].languageTypeId;
        std::unordered_set<SymbolId> resolving;
        const auto metadata = metadataValue(
            metadataValue, expression.capture(i), resolving);
        const bool evaluateNestedMixfix = isRegisteredMixfix(metadata);
        if (captureType == LanguageTypeId::Expr && !evaluateNestedMixfix) {
            value = std::make_shared<AstValueExpr>(
                AstValueKind::Expression,
                std::vector<std::shared_ptr<AstNode>>{expression.capture(i)},
                astNodeKind(expression.capture(i)));
        } else if (!evalExprValue(
                       evaluateNestedMixfix ? metadata : expression.capture(i), env, value)) {
            return false;
        }
        values.push_back(std::move(value));
    }
    Env methodEnv;
    for (size_t i = 0; i < values.size(); ++i) {
        methodEnv[selected.overload->captures[i].name] = values[i];
        if (!selected.clause->head.args.empty()) {
            const auto parameter = makeMethodParamPlan(selected.clause->head.args[i]);
            methodEnv[parameter.localName] = values[i];
        }
    }
    Env prototypeEnv;
    for (size_t i = 0; i < values.size(); ++i) {
        prototypeEnv[selected.overload->captures[i].name] = values[i];
    }
    for (size_t i = 0; i < selected.factorPrototypes.size(); ++i) {
        std::shared_ptr<Expr> materialized;
        if (!evalExprValue(selected.factorPrototypes[i], prototypeEnv, materialized)) {
            throw InterpreterError("Requirement prototype could not be materialized after overload selection");
        }
        methodEnv[selected.overload->factors[i].name] = std::move(materialized);
    }
    std::vector<Solution> solutions;
    constexpr std::size_t kMaxOperatorManyResults = 1000;
    const std::size_t solutionLimit = selected.overload->cardinality == OperatorCardinality::Many
        ? kMaxOperatorManyResults + 1 : 2;
    solveRecursive(selected.clause->body, methodEnv, solutions, solutionLimit, 0);
    if (solutions.empty()) {
        for (const auto& branch : selected.clause->fallbackBranches) {
            solveRecursive(branch, methodEnv, solutions, solutionLimit, 0);
            if (!solutions.empty()) break;
        }
    }
    if (selected.overload->cardinality == OperatorCardinality::One && solutions.size() != 1) {
        throw InterpreterError("Operator overload cardinality 'one' requires exactly one result");
    }
    if (selected.overload->cardinality == OperatorCardinality::Optional && solutions.size() > 1) {
        throw InterpreterError("Operator overload cardinality 'optional' permits at most one result");
    }
    if (selected.overload->cardinality == OperatorCardinality::Many &&
        solutions.size() > kMaxOperatorManyResults) {
        throw InterpreterError("Operator overload cardinality 'many' exceeded the 1000-result safety limit");
    }
    if (solutions.empty()) {
        if (selected.overload->cardinality == OperatorCardinality::Many) {
            out = std::make_shared<ArrayExpr>(std::vector<std::shared_ptr<Expr>>{});
            return true;
        }
        out = std::make_shared<NilExpr>();
        return selected.overload->cardinality == OperatorCardinality::Optional;
    }
    std::vector<std::shared_ptr<Expr>> returnedValues;
    returnedValues.reserve(solutions.size());
    for (const auto& solution : solutions) {
        auto returned = solution.env.find(internalSymbolString(InternalSymbolKind::Return));
        if (returned == solution.env.end()) {
            throw InterpreterError("Operator overload method did not return a value");
        }
        auto value = resolveExpr(returned->second, solution.env)->clone();
        const ResolvedOperatorType actual = valueType(value);
        bool validResult = true;
        if (!selected.overload->resultType.empty()) {
            if (selected.overload->resultLanguageTypeId != LanguageTypeId::Unknown) {
                validResult = valueMatchesBuiltinType(
                    value, selected.overload->resultLanguageTypeId);
            } else if (actual.factTypeId != 0) {
                validResult =
                    (actual.factTypeId == selected.overload->resultTypeId &&
                     actual.factType == selected.overload->resultType) ||
                    memory_.isCompatibleType(
                        actual.factType, selected.overload->resultType);
            } else {
                validResult = false;
            }
        }
        if (!validResult) {
            const std::string actualName =
                actual.languageType != LanguageTypeId::Unknown
                    ? std::string(languageTypeName(actual.languageType))
                    : actual.factType.empty() ? "unknown" : actual.factType;
            throw InterpreterError("Operator overload returned '" + actualName +
                "', expected '" + selected.overload->resultType + "'");
        }
        returnedValues.push_back(std::move(value));
    }
    if (selected.overload->cardinality == OperatorCardinality::Many) {
        out = std::make_shared<ArrayExpr>(std::move(returnedValues));
    } else {
        out = std::move(returnedValues.front());
    }
    return true;
}

bool Interpreter::compareResolved(const std::shared_ptr<Expr>& left,
                                  TokenType op,
                                  const std::shared_ptr<Expr>& right) const {
    auto ln = std::dynamic_pointer_cast<NumberExpr>(left);
    auto rn = std::dynamic_pointer_cast<NumberExpr>(right);
    if (ln && rn) {
        switch (op) {
            case TokenType::LT: return ln->value < rn->value;
            case TokenType::LTE: return ln->value <= rn->value;
            case TokenType::GT: return ln->value > rn->value;
            case TokenType::GTE: return ln->value >= rn->value;
            default: return false;
        }
    }

    auto ls = std::dynamic_pointer_cast<StringExpr>(left);
    auto rs = std::dynamic_pointer_cast<StringExpr>(right);
    if (ls && rs) {
        switch (op) {
            case TokenType::LT: return ls->value < rs->value;
            case TokenType::LTE: return ls->value <= rs->value;
            case TokenType::GT: return ls->value > rs->value;
            case TokenType::GTE: return ls->value >= rs->value;
            default: return false;
        }
    }

    return false;
}

bool Interpreter::unifyCall(const Call& goal, const Call& head, Env& env) {
    if (goal.nameId != head.nameId) return false;

    bool headHasNamed = false;
    for (const auto& a : head.args) if (!a.name.empty()) headHasNamed = true;

    if (goal.args.size() == 1 && goal.args[0].name.empty() && headHasNamed) return false;

    // This supports partial named matching. A goal can specify only the fields it cares about:
    // Employee(name: X) can match Employee(name: "Alice", role: "Engineer").
    for (size_t i = 0; i < goal.args.size(); ++i) {
        const Arg& goalArg = goal.args[i];
        const Arg* headArg = findArg(head, goalArg, i);
        if (!headArg) return false;
        if (!unifyExpr(goalArg.value, headArg->value, env)) return false;
    }

    // For positional predicates, arity should match. For named predicates, partial matching is allowed.
    bool goalHasNamed = false;
    for (const auto& a : goal.args) if (!a.name.empty()) goalHasNamed = true;
    if (!goalHasNamed && !headHasNamed && goal.args.size() != head.args.size()) return false;

    return true;
}

std::vector<Env> Interpreter::unifyCallAlternatives(const Call& goal, const Call& head, const Env& env) {
    if (goal.nameId != head.nameId) return {};

    bool headHasNamed = false;
    bool goalHasNamed = false;
    for (const auto& arg : head.args) if (!arg.name.empty()) headHasNamed = true;
    for (const auto& arg : goal.args) if (!arg.name.empty()) goalHasNamed = true;

    if (goal.args.size() == 1 && goal.args[0].name.empty() && headHasNamed) return {};
    if (!goalHasNamed && !headHasNamed && goal.args.size() != head.args.size()) return {};

    std::vector<std::vector<const Arg*>> candidatesByArg;
    candidatesByArg.reserve(goal.args.size());
    bool hasAlternatives = false;
    for (size_t i = 0; i < goal.args.size(); ++i) {
        const Arg& goalArg = goal.args[i];
        std::vector<const Arg*> candidates;
        if (!goalArg.name.empty()) {
            for (const auto& headArg : head.args) {
                if (headArg.nameId == goalArg.nameId) {
                    candidates.push_back(&headArg);
                }
            }
            if (candidates.empty() && i < head.args.size() && head.args[i].name.empty()) {
                candidates.push_back(&head.args[i]);
            }
        } else if (i < head.args.size()) {
            candidates.push_back(&head.args[i]);
        }
        if (candidates.empty()) return {};
        hasAlternatives = hasAlternatives || candidates.size() > 1;
        candidatesByArg.push_back(std::move(candidates));
    }

    if (!hasAlternatives) {
        Env attempt = env;
        for (size_t i = 0; i < goal.args.size(); ++i) {
            if (!unifyExpr(goal.args[i].value, candidatesByArg[i].front()->value, attempt)) return {};
        }
        std::vector<Env> result;
        result.reserve(1);
        result.push_back(std::move(attempt));
        return result;
    }

    std::vector<Env> results;
    Env working = env;
    BindingTrail trail;
    BindingTrail* previousTrail = activeBindingTrail_;
    activeBindingTrail_ = &trail;
    const auto restoreTrail = [&]() {
        trail.rollback(0);
        activeBindingTrail_ = previousTrail;
    };
    const auto enumerate = [&](const auto& self, size_t argumentIndex) -> void {
        if (argumentIndex == goal.args.size()) {
            results.push_back(working);
            return;
        }
        const auto checkpoint = trail.checkpoint();
        const Arg& goalArg = goal.args[argumentIndex];
        for (const Arg* candidate : candidatesByArg[argumentIndex]) {
            trail.rollback(checkpoint);
            if (unifyExpr(goalArg.value, candidate->value, working)) {
                self(self, argumentIndex + 1);
            }
        }
        trail.rollback(checkpoint);
    };
    try {
        enumerate(enumerate, 0);
    } catch (...) {
        restoreTrail();
        throw;
    }
    restoreTrail();
    return results;
}

const Arg* Interpreter::findArg(const Call& call, const Arg& wanted, size_t index) const {
    if (!wanted.name.empty()) {
        for (const auto& a : call.args) {
            if (a.nameId == wanted.nameId) return &a;
        }
        if (index < call.args.size() && call.args[index].name.empty()) return &call.args[index];
        return nullptr;
    }
    if (index < call.args.size()) return &call.args[index];
    return nullptr;
}

bool Interpreter::unifyExpr(const std::shared_ptr<Expr>& a,
                            const std::shared_ptr<Expr>& b,
                            Env& env) {
    ++unificationAttempts_;
    auto ra = resolveExpr(a, env);
    auto rb = resolveExpr(b, env);

    if (isSameVariable(ra, rb)) return true;

    if (auto va = std::dynamic_pointer_cast<VarExpr>(ra)) {
        if (isAnonymousSymbolName(va->name)) return true;
    }
    if (auto vb = std::dynamic_pointer_cast<VarExpr>(rb)) {
        if (isAnonymousSymbolName(vb->name)) return true;
    }

    if (auto va = std::dynamic_pointer_cast<VarExpr>(ra)) {
        std::shared_ptr<Expr> value;
        value = evalExprValue(rb, env, value) ? value : rb->clone();
        if (activeBindingTrail_) activeBindingTrail_->assign(env, va->nameId, std::move(value));
        else env[va->name] = std::move(value);
        return true;
    }
    if (auto vb = std::dynamic_pointer_cast<VarExpr>(rb)) {
        std::shared_ptr<Expr> value;
        value = evalExprValue(ra, env, value) ? value : ra->clone();
        if (activeBindingTrail_) activeBindingTrail_->assign(env, vb->nameId, std::move(value));
        else env[vb->name] = std::move(value);
        return true;
    }

    if (auto sa = std::dynamic_pointer_cast<StringExpr>(ra)) {
        auto sb = std::dynamic_pointer_cast<StringExpr>(rb);
        return sb && sa->value == sb->value;
    }
    if (auto na = std::dynamic_pointer_cast<NumberExpr>(ra)) {
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
        if (!tb || ta->name != tb->name || ta->args.size() != tb->args.size()) return false;
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

    auto it = env.find(var->name);
    if (it == env.end()) {
        auto globalIt = globals_.find(var->name);
        if (globalIt != globals_.end()) return resolveExpr(globalIt->second, env);
        return expr;
    }

    // Follow variable aliases: X -> __r1_name -> "Alice".
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
    std::string prefix = makeRenameSymbolPrefix(++renameCounter_);
    Call head;
    head.name = clause.head.name;
    head.nameId = clause.head.nameId;
    head.builtinId = clause.head.builtinId;
    bool methodHead = isMethodClause(clause);
    for (const auto& arg : clause.head.args) {
        auto typeExpr = std::dynamic_pointer_cast<VarExpr>(arg.value);
        if (methodHead && typeExpr && isFelidaeTypeAnnotationName(typeExpr->name)) {
            head.args.push_back(Arg{arg.name, arg.value->clone()});
        } else {
            head.args.push_back(Arg{arg.name, renameExpr(arg.value, prefix)});
        }
    }
    std::vector<std::shared_ptr<Goal>> body;
    body.reserve(clause.body.size());
    for (const auto& g : clause.body) body.push_back(renameGoal(g, prefix));
    std::vector<std::vector<std::shared_ptr<Goal>>> fallbackBranches;
    fallbackBranches.reserve(clause.fallbackBranches.size());
    for (const auto& branch : clause.fallbackBranches) {
        std::vector<std::shared_ptr<Goal>> renamedBranch;
        renamedBranch.reserve(branch.size());
        for (const auto& goal : branch) renamedBranch.push_back(renameGoal(goal, prefix));
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

Call Interpreter::renameCall(const Call& call, const std::string& prefix) {
    Call out;
    out.name = call.name;
    out.nameId = call.nameId;
    out.builtinId = call.builtinId;
    for (const auto& a : call.args) {
        if ((call.builtinId == BuiltinId::Throw && a.name == "target") ||
            (call.builtinId == BuiltinId::Instanceof &&
             (a.name == "type" || a.name == "parent" || a.name == "of"))) {
            out.args.push_back(Arg{a.name, a.value->clone()});
            continue;
        }
        out.args.push_back(Arg{a.name, renameExpr(a.value, prefix)});
    }
    return out;
}

std::shared_ptr<Goal> Interpreter::renameGoal(const std::shared_ptr<Goal>& goal, const std::string& prefix) {
    switch (goal->kind()) {
        case GoalKind::Call: {
            auto cg = std::static_pointer_cast<CallGoal>(goal);
            return std::make_shared<CallGoal>(renameCall(cg->call, prefix));
        }
        case GoalKind::Not: {
            auto ng = std::static_pointer_cast<NotGoal>(goal);
            return std::make_shared<NotGoal>(renameCall(ng->call, prefix));
        }
        case GoalKind::Assign: {
            auto ag = std::static_pointer_cast<AssignGoal>(goal);
            return std::make_shared<AssignGoal>(prefix + ag->name, renameExpr(ag->expr, prefix));
        }
        case GoalKind::MultiAssign: {
            auto mag = std::static_pointer_cast<MultiAssignGoal>(goal);
            std::vector<AssignmentTarget> targets;
            targets.reserve(mag->targets.size());
            for (const auto& target : mag->targets) {
                targets.push_back(AssignmentTarget{prefix + target.name, target.type});
            }
            return std::make_shared<MultiAssignGoal>(std::move(targets), renameExpr(mag->expr, prefix));
        }
        case GoalKind::Binary: {
            auto bg = std::static_pointer_cast<BinaryGoal>(goal);
            return std::make_shared<BinaryGoal>(renameExpr(bg->left, prefix), bg->op, renameExpr(bg->right, prefix));
        }
        case GoalKind::Where: {
            auto wg = std::static_pointer_cast<WhereGoal>(goal);
            return std::make_shared<WhereGoal>(renameGoal(wg->condition, prefix));
        }
        case GoalKind::If: {
            auto ifGoal = std::static_pointer_cast<IfGoal>(goal);
            std::vector<std::shared_ptr<Goal>> thenBranch;
            thenBranch.reserve(ifGoal->thenBranch.size());
            for (const auto& branchGoal : ifGoal->thenBranch) thenBranch.push_back(renameGoal(branchGoal, prefix));
            std::vector<std::shared_ptr<Goal>> elseBranch;
            elseBranch.reserve(ifGoal->elseBranch.size());
            for (const auto& branchGoal : ifGoal->elseBranch) elseBranch.push_back(renameGoal(branchGoal, prefix));
            return std::make_shared<IfGoal>(
                renameGoal(ifGoal->condition, prefix),
                std::move(thenBranch),
                std::move(elseBranch));
        }
        case GoalKind::Return: {
            auto rg = std::static_pointer_cast<ReturnGoal>(goal);
            std::vector<Arg> fields;
            fields.reserve(rg->fields.size());
            for (const auto& field : rg->fields) fields.push_back(Arg{field.name, renameExpr(field.value, prefix)});
            return std::make_shared<ReturnGoal>(std::move(fields));
        }
        case GoalKind::Group: {
            auto gg = std::static_pointer_cast<GroupGoal>(goal);
            std::vector<std::shared_ptr<Goal>> goals;
            goals.reserve(gg->goals.size());
            for (const auto& groupedGoal : gg->goals) goals.push_back(renameGoal(groupedGoal, prefix));
            return std::make_shared<GroupGoal>(std::move(goals));
        }
        case GoalKind::Or: {
            auto og = std::static_pointer_cast<OrGoal>(goal);
            std::vector<std::vector<std::shared_ptr<Goal>>> branches;
            branches.reserve(og->branches.size());
            for (const auto& branch : og->branches) {
                std::vector<std::shared_ptr<Goal>> renamedBranch;
                renamedBranch.reserve(branch.size());
                for (const auto& branchGoal : branch) renamedBranch.push_back(renameGoal(branchGoal, prefix));
                branches.push_back(std::move(renamedBranch));
            }
            return std::make_shared<OrGoal>(std::move(branches));
        }
    }
    throw InterpreterError("Unknown goal while renaming");
}

std::shared_ptr<Expr> Interpreter::renameExpr(const std::shared_ptr<Expr>& expr, const std::string& prefix) {
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
        if (globals_.count(v->name) > 0) return v->clone();
        return std::make_shared<VarExpr>(prefix + v->name);
    }
    if (auto term = std::dynamic_pointer_cast<TermExpr>(expr)) {
        std::vector<Arg> args;
        args.reserve(term->args.size());
        for (const auto& arg : term->args) args.push_back(Arg{arg.name, renameExpr(arg.value, prefix)});
        return std::make_shared<TermExpr>(term->name, std::move(args), term->builtinId);
    }
    if (auto array = std::dynamic_pointer_cast<ArrayExpr>(expr)) {
        std::vector<std::shared_ptr<Expr>> items;
        items.reserve(array->items.size());
        for (const auto& item : array->items) items.push_back(renameExpr(item, prefix));
        return std::make_shared<ArrayExpr>(std::move(items));
    }
    if (auto map = std::dynamic_pointer_cast<MapExpr>(expr)) {
        std::vector<MapEntry> entries;
        entries.reserve(map->entries.size());
        for (const auto& entry : map->entries) {
            entries.push_back(MapEntry{entry.key, renameExpr(entry.value, prefix)});
        }
        return std::make_shared<MapExpr>(std::move(entries));
    }
    if (auto access = std::dynamic_pointer_cast<AccessExpr>(expr)) {
        auto targetVar = std::dynamic_pointer_cast<VarExpr>(access->target);
        if (access->keyId == InternalSymbol::ResultId && targetVar && targetVar->nameId == InternalSymbol::SystemId) {
            return access->clone();
        }
        return std::make_shared<AccessExpr>(renameExpr(access->target, prefix), access->key);
    }
    if (auto lambda = std::dynamic_pointer_cast<LambdaExpr>(expr)) {
        return std::make_shared<LambdaExpr>(
            renameExpr(lambda->source, prefix),
            prefix + lambda->variable,
            renameExpr(lambda->body, prefix),
            lambda->op,
            lambda->right ? renameExpr(lambda->right, prefix) : nullptr);
    }
    if (auto op = std::dynamic_pointer_cast<OperatorExpression>(expr)) {
        std::shared_ptr<OperatorExpression> renamed;
        if (op->coreOperator != CoreOperator::Unknown) {
            renamed = op->captureCount() == 1
                ? std::make_shared<OperatorExpression>(op->coreOperator, renameExpr(op->capture(0), prefix))
                : std::make_shared<OperatorExpression>(
                      op->coreOperator,
                      renameExpr(op->capture(0), prefix),
                      renameExpr(op->capture(1), prefix));
        } else {
            std::vector<OperatorCapture> captures;
            captures.reserve(op->captureCount());
            for (size_t i = 0; i < op->captureCount(); ++i) {
                captures.emplace_back(std::string(op->captureName(i)), renameExpr(op->capture(i), prefix));
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
        return variable->nameId != InternalSymbol::SystemResultId && globals_.count(variable->name) == 0;
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
    return va && vb && va->nameId == vb->nameId && va->name == vb->name;
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
    if (typeExpr && !plan.typedParam && !typeExpr->name.empty() && !isAnonymousSymbolName(typeExpr->name)) {
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
        upsertEntry(entries, arg.name, value->clone());
    }
    auto fact = std::make_shared<MapExpr>(std::move(entries));
    fact->factType = clause.head.name;
    return FactMaterialization{std::move(fact), std::move(parentFactIds)};
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
        for (size_t factIndex : memory_.compatibleFactIndexes(typeName->value)) {
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
            for (const size_t factIndex : memory_.designationIndexes({designationId})) {
                if (const auto value = memory_.factValue(factIndex)) values.push_back(value);
            }
            return values;
        }
    }
    if (var && !var->name.empty() && std::isupper(static_cast<unsigned char>(var->name.front()))) {
        ensurePredicateLoaded(var->name);
        std::vector<std::shared_ptr<Expr>> values;
        for (size_t factIndex : memory_.compatibleFactIndexes(var->name)) {
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
        for (const auto& arg : call.args) {
            if (arg.name == name) return &arg;
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
        for (const auto index : indexes) {
            const auto& record = memory_.snapshotFact(lazy->snapshotGeneration, index);
            if (!record.active || (!lazy->factType.empty() &&
                !memory_.isCompatibleType(record.type, lazy->factType))) {
                continue;
            }
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
                if (filter.op == TokenType::EqEq) {
                    matches = exprContainsLiteral(actual, filter.value);
                } else if (filter.op == TokenType::NotEq) {
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
                std::move(row.designationIds));
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
        << "\"parserVirtualTokenSynchronizations\":" << parserMetrics_.virtualTokenSynchronizations << ","
        << "\"parserVirtualTokensRegistered\":" << parserMetrics_.virtualTokensRegistered << ","
        << "\"parserVirtualTokensRetagged\":" << parserMetrics_.virtualTokensRetagged << ","
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
