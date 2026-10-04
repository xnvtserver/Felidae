#pragma once

#include "Operator.h"
#include "FelidaeGrammar.h"
#include <cstdint>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Felidae {

enum class ExprKind {
    String,
    Atom,
    ClassRef,
    FunctionRef,
    Number,
    Bool,
    Nil,
    Var,
    Array,
    Map,
    Access,
    Operator,
    Sequence,
    Conditional,
    Term,
    Lambda,
    FactSelection,
    GraphSelection,
    Super,
    AstValue
};

enum class GoalKind {
    Call,
    Binary,
    Assign,
    MultiAssign,
    Where,
    If,
    For,
    While,
    Switch,
    Break,
    Continue,
    Expression,
    Not,
    Group,
    Or,
    Try
};

// Recursive source type used by both class fields and typed immutable local
// bindings. Keeping one representation makes list/Pair/optional validation
// identical at both language boundaries.
struct TypeRef {
    std::string name;
    std::vector<TypeRef> arguments;

    std::string canonical() const {
        std::ostringstream out;
        out << name;
        if (!arguments.empty()) {
            out << '<';
            for (std::size_t index = 0; index < arguments.size(); ++index) {
                if (index) out << (name == "optional" ? " | " : ", ");
                out << arguments[index].canonical();
            }
            out << '>';
        }
        return out.str();
    }

    bool isOptional() const { return name == "optional"; }
};

enum class StatementKind {
    Import,
    Clause,
    GlobalBinding,
    Class
};

enum class ClauseKind {
    Fact,
    Rule,
    Method,
    NativeDeclaration,
    EntryCall
};

struct SourceSpan {
    int startLine = 1;
    int startColumn = 1;
    int endLine = 1;
    int endColumn = 1;

    bool valid() const {
        return startLine > 0 && startColumn > 0 &&
               endLine >= startLine &&
               (endLine != startLine || endColumn >= startColumn);
    }
};

class AstNode {
public:
    virtual ~AstNode() = default;
    virtual std::string debug() const = 0;

    // The parser assigns compact positional metadata as soon as a node is
    // completed. Source text remains owned by the input/runtime layer, so
    // retaining an AST node never pins a complete input buffer.
    SourceSpan sourceSpan;
    std::uint64_t nodeId = 0;
};

class Expr : public AstNode {
public:
    virtual ExprKind kind() const = 0;
    virtual std::shared_ptr<Expr> clone() const = 0;
};

class StringExpr final : public Expr {
public:
    explicit StringExpr(std::string value) : value(std::move(value)) {}
    std::string value;

    static constexpr ExprKind kKind = ExprKind::String;
    ExprKind kind() const override { return kKind; }
    std::shared_ptr<Expr> clone() const override { return std::make_shared<StringExpr>(value); }
    std::string debug() const override {
        std::ostringstream oss;
        oss << '"';
        for (char c : value) {
            if (c == '"') oss << "\\\"";
            else if (c == '\n') oss << "\\n";
            else oss << c;
        }
        oss << '"';
        return oss.str();
    }
};

// First-class symbolic data. The spelling is the durable identity; symbolId
// is process-local acceleration rebuilt whenever an atom is parsed or decoded.
class AtomExpr final : public Expr {
public:
    explicit AtomExpr(std::string spelling)
        : spelling(std::move(spelling)), symbolId(symbolIdForName(this->spelling)) {}

    std::string spelling;
    SymbolId symbolId = 0;

    static constexpr ExprKind kKind = ExprKind::Atom;
    ExprKind kind() const override { return kKind; }
    std::shared_ptr<Expr> clone() const override {
        return std::make_shared<AtomExpr>(spelling);
    }
    std::string debug() const override { return spelling; }
};

class ClassRefExpr final : public Expr {
public:
    explicit ClassRefExpr(std::string name)
        : name(std::move(name)), nameId(symbolIdForName(this->name)) {}
    std::string name;
    SymbolId nameId = 0;
    static constexpr ExprKind kKind = ExprKind::ClassRef;
    ExprKind kind() const override { return kKind; }
    std::shared_ptr<Expr> clone() const override {
        return std::make_shared<ClassRefExpr>(name);
    }
    std::string debug() const override { return name + ".class"; }
};

class FunctionRefExpr final : public Expr {
public:
    explicit FunctionRefExpr(std::string name)
        : name(std::move(name)), nameId(symbolIdForName(this->name)) {}
    std::string name;
    SymbolId nameId = 0;
    static constexpr ExprKind kKind = ExprKind::FunctionRef;
    ExprKind kind() const override { return kKind; }
    std::shared_ptr<Expr> clone() const override {
        return std::make_shared<FunctionRefExpr>(name);
    }
    std::string debug() const override { return name + ".function"; }
};

class NumberExpr final : public Expr {
public:
    explicit NumberExpr(double value) : value(value) {}
    double value;

    static constexpr ExprKind kKind = ExprKind::Number;
    ExprKind kind() const override { return kKind; }
    std::shared_ptr<Expr> clone() const override { return std::make_shared<NumberExpr>(value); }
    std::string debug() const override {
        std::ostringstream oss;
        oss << std::setprecision(15) << value;
        return oss.str();
    }
};

class BoolExpr final : public Expr {
public:
    explicit BoolExpr(bool value) : value(value) {}
    bool value;

    static constexpr ExprKind kKind = ExprKind::Bool;
    ExprKind kind() const override { return kKind; }
    std::shared_ptr<Expr> clone() const override { return std::make_shared<BoolExpr>(value); }
    std::string debug() const override { return value ? "true" : "false"; }
};

class NilExpr final : public Expr {
public:
    static constexpr ExprKind kKind = ExprKind::Nil;
    ExprKind kind() const override { return kKind; }
    std::shared_ptr<Expr> clone() const override { return std::make_shared<NilExpr>(); }
    std::string debug() const override { return "nil"; }
};

class VarExpr final : public Expr {
public:
    explicit VarExpr(std::string name, LanguageTypeId languageTypeId = LanguageTypeId::Unknown)
        : name(std::move(name)), nameId(symbolIdForName(this->name)),
          languageTypeId(languageTypeId) {}
    VarExpr(std::string displayName, SymbolId directId,
            LanguageTypeId languageTypeId = LanguageTypeId::Unknown)
        : name(std::move(displayName)), nameId(directId), languageTypeId(languageTypeId) {}
    std::string name;
    SymbolId nameId = 0;
    LanguageTypeId languageTypeId = LanguageTypeId::Unknown;

    static constexpr ExprKind kKind = ExprKind::Var;
    ExprKind kind() const override { return kKind; }
    std::shared_ptr<Expr> clone() const override {
        return std::make_shared<VarExpr>(name, nameId, languageTypeId);
    }
    std::string debug() const override { return name; }
};

class ArrayExpr final : public Expr {
public:
    explicit ArrayExpr(std::vector<std::shared_ptr<Expr>> items)
        : items(std::move(items)) {}

    std::vector<std::shared_ptr<Expr>> items;

    static constexpr ExprKind kKind = ExprKind::Array;
    ExprKind kind() const override { return kKind; }
    std::shared_ptr<Expr> clone() const override {
        std::vector<std::shared_ptr<Expr>> copied;
        copied.reserve(items.size());
        for (const auto& item : items) copied.push_back(item->clone());
        return std::make_shared<ArrayExpr>(std::move(copied));
    }

    std::string debug() const override {
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < items.size(); ++i) {
            if (i) oss << ", ";
            oss << items[i]->debug();
        }
        oss << "]";
        return oss.str();
    }
};

struct MapEntry {
    MapEntry() = default;
    MapEntry(std::string key, std::shared_ptr<Expr> value)
        : key(std::move(key)), keyId(symbolIdForName(this->key)), value(std::move(value)) {}
    MapEntry(std::string displayKey, SymbolId directId, std::shared_ptr<Expr> value)
        : key(std::move(displayKey)), keyId(directId), value(std::move(value)) {}

    std::string key;
    SymbolId keyId = 0;
    std::shared_ptr<Expr> value;
};

class MapExpr final : public Expr {
public:
    explicit MapExpr(std::vector<MapEntry> entries)
        : entries(std::move(entries)) {}

    std::vector<MapEntry> entries;
    // Set only for values created by Felidae fact declarations or
    // constructors. It is intentionally separate from the visible __type
    // field so an ordinary map cannot masquerade as a fact at the language
    // boundary.
    std::string factType;
    // Runtime-only identity for a value materialized from a fact store. It is
    // deliberately absent from debug(), serialization and structural
    // equality: two facts may have equal visible fields while retaining
    // distinct identities for dependencies and relationships.
    std::uint64_t factIdentity = 0;

    static constexpr ExprKind kKind = ExprKind::Map;
    ExprKind kind() const override { return kKind; }
    std::shared_ptr<Expr> clone() const override {
        std::vector<MapEntry> copied;
        copied.reserve(entries.size());
        for (const auto& entry : entries) {
            copied.push_back(MapEntry{entry.key, entry.value->clone()});
        }
        auto result = std::make_shared<MapExpr>(std::move(copied));
        result->factIdentity = factIdentity;
        result->factType = factType;
        return result;
    }

    std::string debug() const override {
        std::ostringstream oss;
        oss << "{";
        for (size_t i = 0; i < entries.size(); ++i) {
            if (i) oss << ", ";
            oss << entries[i].key << ": " << entries[i].value->debug();
        }
        oss << "}";
        return oss.str();
    }
};

enum class AstValueKind : std::uint8_t { Expression, Statement, Statements };

class AstValueExpr final : public Expr {
public:
    AstValueExpr(AstValueKind valueKind,
                 std::vector<std::shared_ptr<AstNode>> nodes,
                 std::string nodeKind)
        : valueKind(valueKind), nodes(std::move(nodes)), nodeKind(std::move(nodeKind)) {}

    AstValueKind valueKind = AstValueKind::Expression;
    std::vector<std::shared_ptr<AstNode>> nodes;
    std::string nodeKind;

    static constexpr ExprKind kKind = ExprKind::AstValue;
    ExprKind kind() const override { return kKind; }
    std::shared_ptr<Expr> clone() const override {
        return std::make_shared<AstValueExpr>(valueKind, nodes, nodeKind);
    }
    std::string sourceText() const {
        if (nodes.size() != 1 || !nodes.front()) return {};
        if (const auto variable = std::dynamic_pointer_cast<VarExpr>(nodes.front())) {
            return variable->name;
        }
        if (const auto text = std::dynamic_pointer_cast<StringExpr>(nodes.front())) {
            return text->value;
        }
        return nodes.front()->debug();
    }
    std::string debug() const override {
        const char* type = valueKind == AstValueKind::Expression ? "expr" :
                           valueKind == AstValueKind::Statement ? "stmt" : "stmts";
        return std::string("<") + type + ":" + nodeKind + ">";
    }
};

struct FactSelectionFilter {
    std::string field;
    SymbolId fieldId = 0;
    TokenId::Id op = TokenId::EQUAL;
    std::shared_ptr<Expr> value;
};

// Runtime-only lazy fact query. It deliberately is not a MapExpr: user map
// operations cannot mutate or counterfeit its query description. Facts are read
// from RocksDB only when a selection is materialized.
class FactSelectionExpr final : public Expr {
public:
    FactSelectionExpr(std::string factType,
                      std::string field = {},
                      std::shared_ptr<Expr> equals = nullptr,
                      std::vector<SymbolId> designationIds = {},
                      std::vector<std::string> designations = {},
                      std::vector<FactSelectionFilter> filters = {})
        : factType(std::move(factType)),
          factTypeId(this->factType.empty() ? 0 : symbolIdForName(this->factType)),
          field(std::move(field)),
          fieldId(this->field.empty() ? 0 : symbolIdForName(this->field)),
          equals(std::move(equals)),
          designationIds(std::move(designationIds)),
          designations(std::move(designations)),
          filters(std::move(filters)) {}

    std::string factType;
    SymbolId factTypeId = 0;
    std::string field;
    SymbolId fieldId = 0;
    std::shared_ptr<Expr> equals;
    std::vector<SymbolId> designationIds;
    std::vector<std::string> designations;
    std::vector<FactSelectionFilter> filters;

    static constexpr ExprKind kKind = ExprKind::FactSelection;
    ExprKind kind() const override { return kKind; }
    std::shared_ptr<Expr> clone() const override {
        std::vector<FactSelectionFilter> copiedFilters;
        copiedFilters.reserve(filters.size());
        for (const auto& filter : filters) {
            copiedFilters.push_back(FactSelectionFilter{
                filter.field,
                filter.fieldId,
                filter.op,
                filter.value ? filter.value->clone() : nullptr});
        }
        return std::make_shared<FactSelectionExpr>(
            factType,
            field,
            equals ? equals->clone() : nullptr,
            designationIds,
            designations,
            std::move(copiedFilters));
    }
    std::string debug() const override {
        std::ostringstream out;
        out << "{__type: \"FactSelection\", fact_type: \""
            << factType << "\", source: \"store\"";
        if (!designations.empty()) {
            out << ", designations: [";
            for (size_t i = 0; i < designations.size(); ++i) {
                if (i) out << ", ";
                out << "\"" << designations[i] << "\"";
            }
            out << "]";
        }
        if (!field.empty()) {
            out << ", field: \"" << field << "\"";
                if (equals) out << ", equals: " << equals->debug();
        }
        if (!filters.empty()) out << ", filters: " << filters.size();
        out << "}";
        return out.str();
    }
};

class AccessExpr final : public Expr {
public:
    AccessExpr(std::shared_ptr<Expr> target, std::string key)
        : target(std::move(target)), key(std::move(key)), keyId(symbolIdForName(this->key)) {}

    std::shared_ptr<Expr> target;
    std::string key;
    SymbolId keyId = 0;

    static constexpr ExprKind kKind = ExprKind::Access;
    ExprKind kind() const override { return kKind; }
    std::shared_ptr<Expr> clone() const override {
        return std::make_shared<AccessExpr>(target->clone(), key);
    }

    std::string debug() const override {
        return target->debug() + ":" + key;
    }
};

struct OperatorCapture {
    OperatorCapture() = default;
    OperatorCapture(std::string name, std::shared_ptr<Expr> expression)
        : name(std::move(name)), nameId(symbolIdForName(this->name)), expression(std::move(expression)) {}

    std::string name;
    SymbolId nameId = 0;
    std::shared_ptr<Expr> expression;
};

class OperatorExpression final : public Expr {
public:
    OperatorExpression(CoreOperator coreOperator,
                       std::shared_ptr<Expr> left,
                       std::shared_ptr<Expr> right)
        : operatorId(Felidae::operatorId(coreOperator)),
          patternId(corePatternId(coreOperator)),
          coreOperator(coreOperator), first_(std::move(left)), second_(std::move(right)),
          inlineCaptureCount_(2) {}

    OperatorExpression(CoreOperator coreOperator, std::shared_ptr<Expr> operand)
        : operatorId(Felidae::operatorId(coreOperator)),
          patternId(corePatternId(coreOperator)),
          coreOperator(coreOperator), first_(std::move(operand)), inlineCaptureCount_(1) {}

    OperatorExpression(OperatorId operatorId,
                       PatternId patternId,
                       std::vector<OperatorCapture> captures,
                       bool explicitlyGrouped = false)
        : operatorId(operatorId), patternId(patternId), coreOperator(CoreOperator::Unknown),
          explicitlyGrouped(explicitlyGrouped), namedCaptures_(std::move(captures)) {}

    OperatorId operatorId = 0;
    PatternId patternId = 0;
    CoreOperator coreOperator = CoreOperator::Unknown;
    bool explicitlyGrouped = false;
    std::string module;

    size_t captureCount() const {
        return coreOperator == CoreOperator::Unknown ? namedCaptures_.size() : inlineCaptureCount_;
    }
    const std::shared_ptr<Expr>& capture(size_t index) const {
        if (coreOperator == CoreOperator::Unknown) return namedCaptures_.at(index).expression;
        if (index == 0 && inlineCaptureCount_ > 0) return first_;
        if (index == 1 && inlineCaptureCount_ > 1) return second_;
        throw std::out_of_range("Operator capture index");
    }
    std::string_view captureName(size_t index) const {
        if (coreOperator == CoreOperator::Unknown) return namedCaptures_.at(index).name;
        if (inlineCaptureCount_ == 1 && index == 0) return "operand";
        if (index == 0) return "left";
        if (index == 1 && inlineCaptureCount_ > 1) return "right";
        return {};
    }

    static constexpr ExprKind kKind = ExprKind::Operator;
    ExprKind kind() const override { return kKind; }
    std::shared_ptr<Expr> clone() const override {
        std::shared_ptr<OperatorExpression> result;
        if (coreOperator != CoreOperator::Unknown) {
            result = captureCount() == 1
                ? std::make_shared<OperatorExpression>(coreOperator, capture(0)->clone())
                : std::make_shared<OperatorExpression>(coreOperator, capture(0)->clone(), capture(1)->clone());
        } else {
            std::vector<OperatorCapture> copied;
            copied.reserve(namedCaptures_.size());
            for (const auto& capture : namedCaptures_) {
                copied.emplace_back(capture.name, capture.expression->clone());
            }
            result = std::make_shared<OperatorExpression>(operatorId, patternId, std::move(copied), explicitlyGrouped);
        }
        result->operatorId = operatorId;
        result->patternId = patternId;
        result->explicitlyGrouped = explicitlyGrouped;
        result->module = module;
        result->sourceSpan = sourceSpan;
        return result;
    }

    std::string debug() const override {
        const auto definition = coreOperatorDefinition(coreOperator);
        if (definition.fixity == OperatorFixity::Prefix && captureCount() == 1) {
            return std::string(definition.spelling) + capture(0)->debug();
        }
        if (captureCount() == 2) {
            return capture(0)->debug() + " " + std::string(definition.spelling) + " " + capture(1)->debug();
        }
        return "operator(" + std::to_string(operatorId) + ")";
    }

private:
    std::shared_ptr<Expr> first_;
    std::shared_ptr<Expr> second_;
    std::vector<OperatorCapture> namedCaptures_;
    std::uint8_t inlineCaptureCount_ = 0;
};

class SequenceExpr final : public Expr {
public:
    explicit SequenceExpr(std::vector<std::shared_ptr<Expr>> expressions)
        : expressions(std::move(expressions)) {}

    std::vector<std::shared_ptr<Expr>> expressions;
    static constexpr ExprKind kKind = ExprKind::Sequence;
    ExprKind kind() const override { return kKind; }
    std::shared_ptr<Expr> clone() const override {
        std::vector<std::shared_ptr<Expr>> copied;
        copied.reserve(expressions.size());
        for (const auto& expression : expressions) copied.push_back(expression->clone());
        return std::make_shared<SequenceExpr>(std::move(copied));
    }
    std::string debug() const override {
        std::ostringstream out;
        for (std::size_t index = 0; index < expressions.size(); ++index) {
            if (index) out << ", ";
            out << expressions[index]->debug();
        }
        return out.str();
    }
};

struct ConditionalBranch {
    std::shared_ptr<Expr> condition;
    std::shared_ptr<Expr> result;
};

class ConditionalExpr final : public Expr {
public:
    ConditionalExpr(std::vector<ConditionalBranch> branches,
                    std::shared_ptr<Expr> fallback = {})
        : branches(std::move(branches)), fallback(std::move(fallback)) {}

    std::vector<ConditionalBranch> branches;
    std::shared_ptr<Expr> fallback;
    static constexpr ExprKind kKind = ExprKind::Conditional;
    ExprKind kind() const override { return kKind; }
    std::shared_ptr<Expr> clone() const override {
        std::vector<ConditionalBranch> copied;
        copied.reserve(branches.size());
        for (const auto& branch : branches) {
            copied.push_back({branch.condition->clone(), branch.result->clone()});
        }
        return std::make_shared<ConditionalExpr>(
            std::move(copied), fallback ? fallback->clone() : nullptr);
    }
    std::string debug() const override {
        std::ostringstream out;
        for (std::size_t index = 0; index < branches.size(); ++index) {
            if (index) out << " else ";
            out << branches[index].condition->debug() << " then "
                << branches[index].result->debug();
        }
        if (fallback) out << " else " << fallback->debug();
        return out.str();
    }
};

struct Arg {
    Arg() = default;
    Arg(std::string name, std::shared_ptr<Expr> value)
        : name(std::move(name)), nameId(this->name.empty() ? 0 : symbolIdForName(this->name)),
          value(std::move(value)) {}
    Arg(std::string displayName, SymbolId directId, std::shared_ptr<Expr> value)
        : name(std::move(displayName)), nameId(directId), value(std::move(value)) {}

    std::string name; // empty means positional argument
    SymbolId nameId = 0;
    std::shared_ptr<Expr> value;

    std::string debug() const {
        if (name.empty()) return value->debug();
        return name + ": " + value->debug();
    }
};

class TermExpr final : public Expr {
public:
    TermExpr(std::string name, std::vector<Arg> args, BuiltinId builtinId = BuiltinId::Unknown)
        : name(std::move(name)), nameId(symbolIdForName(this->name)),
          builtinId(builtinId), args(std::move(args)) {}
    TermExpr(std::string displayName, SymbolId directId, std::vector<Arg> args,
             BuiltinId builtinId = BuiltinId::Unknown)
        : name(std::move(displayName)), nameId(directId), builtinId(builtinId), args(std::move(args)) {}

    std::string name;
    SymbolId nameId = 0;
    BuiltinId builtinId = BuiltinId::Unknown;
    std::vector<Arg> args;
    static constexpr ExprKind kKind = ExprKind::Term;
    ExprKind kind() const override { return kKind; }
    std::shared_ptr<Expr> clone() const override {
        std::vector<Arg> copied;
        copied.reserve(args.size());
        for (const auto& arg : args) copied.emplace_back(arg.name, arg.nameId, arg.value->clone());
        return std::make_shared<TermExpr>(name, nameId, std::move(copied), builtinId);
    }

    std::string debug() const override {
        std::ostringstream oss;
        oss << name << "(";
        for (size_t i = 0; i < args.size(); ++i) {
            if (i) oss << ", ";
            oss << args[i].debug();
        }
        oss << ")";
        return oss.str();
    }
};

class LambdaExpr final : public Expr {
public:
    LambdaExpr(std::shared_ptr<Expr> source,
               std::string variable,
               std::shared_ptr<Expr> body,
               TokenId::Id op = TokenId::UNKNOWN,
               std::shared_ptr<Expr> right = {})
        : source(std::move(source)), variable(std::move(variable)),
          variableId(symbolIdForName(this->variable)), body(std::move(body)),
          op(std::move(op)), right(std::move(right)) {}
    LambdaExpr(std::shared_ptr<Expr> source,
               std::string displayVariable,
               SymbolId directVariableId,
               std::shared_ptr<Expr> body,
               TokenId::Id op = TokenId::UNKNOWN,
               std::shared_ptr<Expr> right = {})
        : source(std::move(source)), variable(std::move(displayVariable)),
          variableId(directVariableId), body(std::move(body)), op(std::move(op)),
          right(std::move(right)) {}

    std::shared_ptr<Expr> source;
    std::string variable;
    SymbolId variableId = 0;
    std::shared_ptr<Expr> body;
    TokenId::Id op;
    std::shared_ptr<Expr> right;

    static constexpr ExprKind kKind = ExprKind::Lambda;
    ExprKind kind() const override { return kKind; }
    std::shared_ptr<Expr> clone() const override {
        return std::make_shared<LambdaExpr>(
            source->clone(), variable, variableId, body->clone(), op,
            right ? right->clone() : nullptr);
    }

    std::string debug() const override {
        std::ostringstream oss;
        oss << "lambda(" << source->debug() << ", " << variable << " => " << body->debug();
        if (op != TokenId::UNKNOWN) oss << " " << builtinTokenSpelling(op) << " " << right->debug();
        oss << ")";
        return oss.str();
    }
};

class Call : public AstNode {
public:
    std::string name;
    SymbolId nameId = 0;
    BuiltinId builtinId = BuiltinId::Unknown;
    std::vector<Arg> args;
    // Only explicit fact-pattern/query contexts may introduce bindings from
    // unresolved identifiers. Ordinary calls interpret them as data atoms.
    bool patternBindings = false;
    // Query-only semantic designations select existing fact rows. They never
    // become serialized fields, types, or inheritance relationships.
    std::vector<std::string> designations;
    std::vector<SymbolId> designationIds;

    Call() = default;
    Call(std::string name, std::vector<Arg> args, BuiltinId builtinId = BuiltinId::Unknown)
        : name(std::move(name)), nameId(symbolIdForName(this->name)),
          builtinId(builtinId), args(std::move(args)) {}
    Call(std::string displayName, SymbolId directId, std::vector<Arg> args,
         BuiltinId builtinId = BuiltinId::Unknown)
        : name(std::move(displayName)), nameId(directId), builtinId(builtinId), args(std::move(args)) {}

    std::string debug() const override {
        std::ostringstream oss;
        oss << name << "(";
        for (size_t i = 0; i < args.size(); ++i) {
            if (i) oss << ", ";
            oss << args[i].debug();
        }
        oss << ")";
        if (!designations.empty()) {
            oss << " as ";
            for (size_t i = 0; i < designations.size(); ++i) {
                if (i) oss << ", ";
                oss << designations[i];
            }
        }
        return oss.str();
    }
};

class Goal : public AstNode {
public:
    virtual GoalKind kind() const = 0;
    virtual std::shared_ptr<Goal> clone() const = 0;
};

class CallGoal final : public Goal {
public:
    explicit CallGoal(Call call) : call(std::move(call)) {}
    Call call;

    static constexpr GoalKind kKind = GoalKind::Call;
    GoalKind kind() const override { return kKind; }
    std::shared_ptr<Goal> clone() const override {
        Call copy;
        copy.name = call.name;
        copy.nameId = call.nameId;
        copy.builtinId = call.builtinId;
        copy.patternBindings = call.patternBindings;
        copy.designations = call.designations;
        copy.designationIds = call.designationIds;
        for (const auto& a : call.args) {
            copy.args.emplace_back(a.name, a.nameId, a.value->clone());
        }
        return std::make_shared<CallGoal>(std::move(copy));
    }

    std::string debug() const override { return call.debug(); }
};

class BinaryGoal final : public Goal {
public:
    BinaryGoal(std::shared_ptr<Expr> left, TokenId::Id op, std::shared_ptr<Expr> right,
               bool strictBoolean = false)
        : left(std::move(left)), op(std::move(op)), right(std::move(right)),
          strictBoolean(strictBoolean) {}

    std::shared_ptr<Expr> left;
    TokenId::Id op;
    std::shared_ptr<Expr> right;
    bool strictBoolean = false;

    static constexpr GoalKind kKind = GoalKind::Binary;
    GoalKind kind() const override { return kKind; }
    std::shared_ptr<Goal> clone() const override {
        return std::make_shared<BinaryGoal>(left->clone(), op, right->clone(), strictBoolean);
    }

    std::string debug() const override {
        return left->debug() + " " + std::string(builtinTokenSpelling(op)) + " " + right->debug();
    }
};

// Negation-as-failure is intentionally a goal, not an expression operator.
// It is restricted to a predicate call so the runtime can enforce its pure,
// bound-variable and stratification rules without introducing implicit
// boolean coercions.
class NotGoal final : public Goal {
public:
    explicit NotGoal(Call call) : call(std::move(call)) {}
    Call call;

    static constexpr GoalKind kKind = GoalKind::Not;
    GoalKind kind() const override { return kKind; }
    std::shared_ptr<Goal> clone() const override {
        Call copy;
        copy.name = call.name;
        copy.nameId = call.nameId;
        copy.builtinId = call.builtinId;
        for (const auto& arg : call.args) copy.args.emplace_back(arg.name, arg.nameId, arg.value->clone());
        return std::make_shared<NotGoal>(std::move(copy));
    }
    std::string debug() const override { return "not " + call.debug(); }
};

class AssignGoal final : public Goal {
public:
    AssignGoal(std::string name, std::shared_ptr<Expr> expr)
        : name(std::move(name)), nameId(symbolIdForName(this->name)), expr(std::move(expr)) {}
    AssignGoal(std::string displayName, SymbolId directId, std::shared_ptr<Expr> expr)
        : name(std::move(displayName)), nameId(directId), expr(std::move(expr)) {}

    std::string name;
    SymbolId nameId = 0;
    std::shared_ptr<Expr> expr;

    static constexpr GoalKind kKind = GoalKind::Assign;
    GoalKind kind() const override { return kKind; }
    std::shared_ptr<Goal> clone() const override {
        if (!expr) return std::make_shared<AssignGoal>(name, nameId, std::shared_ptr<Expr>{});
        return std::make_shared<AssignGoal>(name, nameId, expr->clone());
    }

    std::string debug() const override {
        return name + " := " + (expr ? expr->debug() : "<missing expression>");
    }
};

struct AssignmentTarget {
    std::string name;
    SymbolId nameId = 0;
    TypeRef type;

    explicit AssignmentTarget(std::string name)
        : name(std::move(name)), nameId(symbolIdForName(this->name)) {}
    AssignmentTarget(std::string name, TypeRef type)
        : name(std::move(name)), nameId(symbolIdForName(this->name)), type(std::move(type)) {}
    AssignmentTarget(std::string displayName, SymbolId directId, TypeRef type = {})
        : name(std::move(displayName)), nameId(directId), type(std::move(type)) {}

    std::string debug() const {
        return type.name.empty() ? name : name + ": " + type.canonical();
    }
};

class MultiAssignGoal final : public Goal {
public:
    MultiAssignGoal(std::vector<AssignmentTarget> targets, std::shared_ptr<Expr> expr)
        : targets(std::move(targets)), expr(std::move(expr)) {}

    std::vector<AssignmentTarget> targets;
    std::shared_ptr<Expr> expr;

    static constexpr GoalKind kKind = GoalKind::MultiAssign;
    GoalKind kind() const override { return kKind; }
    std::shared_ptr<Goal> clone() const override {
        return std::make_shared<MultiAssignGoal>(targets, expr->clone());
    }

    std::string debug() const override {
        std::ostringstream oss;
        for (size_t i = 0; i < targets.size(); ++i) {
            if (i) oss << ", ";
            oss << targets[i].debug();
        }
        oss << " := " << expr->debug();
        return oss.str();
    }
};

class WhereGoal final : public Goal {
public:
    explicit WhereGoal(std::shared_ptr<Goal> condition)
        : condition(std::move(condition)) {}

    std::shared_ptr<Goal> condition;

    static constexpr GoalKind kKind = GoalKind::Where;
    GoalKind kind() const override { return kKind; }
    std::shared_ptr<Goal> clone() const override {
        return std::make_shared<WhereGoal>(condition->clone());
    }

    std::string debug() const override { return "where " + condition->debug(); }
};

class IfGoal final : public Goal {
public:
    IfGoal(std::shared_ptr<Goal> condition,
           std::vector<std::shared_ptr<Goal>> thenBranch,
           std::vector<std::shared_ptr<Goal>> elseBranch)
        : condition(std::move(condition)), thenBranch(std::move(thenBranch)), elseBranch(std::move(elseBranch)) {}

    std::shared_ptr<Goal> condition;
    std::vector<std::shared_ptr<Goal>> thenBranch;
    std::vector<std::shared_ptr<Goal>> elseBranch;

    static constexpr GoalKind kKind = GoalKind::If;
    GoalKind kind() const override { return kKind; }
    std::shared_ptr<Goal> clone() const override {
        std::vector<std::shared_ptr<Goal>> copiedThen;
        copiedThen.reserve(thenBranch.size());
        for (const auto& goal : thenBranch) copiedThen.push_back(goal->clone());
        std::vector<std::shared_ptr<Goal>> copiedElse;
        copiedElse.reserve(elseBranch.size());
        for (const auto& goal : elseBranch) copiedElse.push_back(goal->clone());
        return std::make_shared<IfGoal>(condition->clone(), std::move(copiedThen), std::move(copiedElse));
    }

    std::string debug() const override {
        std::ostringstream oss;
        oss << "if " << condition->debug() << ", ";
        for (size_t i = 0; i < thenBranch.size(); ++i) {
            if (i) oss << ", ";
            oss << thenBranch[i]->debug();
        }
        if (!elseBranch.empty()) {
            oss << " else ";
            for (size_t i = 0; i < elseBranch.size(); ++i) {
                if (i) oss << ", ";
                oss << elseBranch[i]->debug();
            }
        }
        return oss.str();
    }
};

// Runtime-only projection of one object node into its immediate parent class
// graph. The underlying object remains shared so `this` retains node identity.
class SuperExpr final : public Expr {
public:
    SuperExpr(std::shared_ptr<MapExpr> object, std::string viewType)
        : object(std::move(object)), viewType(std::move(viewType)) {}

    std::shared_ptr<MapExpr> object;
    std::string viewType;

    static constexpr ExprKind kKind = ExprKind::Super;
    ExprKind kind() const override { return kKind; }
    std::shared_ptr<Expr> clone() const override {
        return std::make_shared<SuperExpr>(object, viewType);
    }
    std::string debug() const override { return "<super:" + viewType + ">"; }
};

class ForGoal final : public Goal {
public:
    ForGoal(std::string variable, std::shared_ptr<Expr> iterable,
            std::vector<std::shared_ptr<Goal>> body)
        : variable(std::move(variable)), variableId(symbolIdForName(this->variable)),
          iterable(std::move(iterable)), body(std::move(body)) {}

    std::string variable;
    SymbolId variableId = 0;
    std::shared_ptr<Expr> iterable;
    std::vector<std::shared_ptr<Goal>> body;

    static constexpr GoalKind kKind = GoalKind::For;
    GoalKind kind() const override { return kKind; }
    std::shared_ptr<Goal> clone() const override {
        std::vector<std::shared_ptr<Goal>> copied;
        copied.reserve(body.size());
        for (const auto& goal : body) copied.push_back(goal->clone());
        return std::make_shared<ForGoal>(variable, iterable->clone(), std::move(copied));
    }
    std::string debug() const override { return "for " + variable + " in " + iterable->debug(); }
};

class WhileGoal final : public Goal {
public:
    WhileGoal(std::shared_ptr<Expr> condition,
              std::vector<std::shared_ptr<Goal>> body)
        : condition(std::move(condition)), body(std::move(body)) {}

    std::shared_ptr<Expr> condition;
    std::vector<std::shared_ptr<Goal>> body;

    static constexpr GoalKind kKind = GoalKind::While;
    GoalKind kind() const override { return kKind; }
    std::shared_ptr<Goal> clone() const override {
        std::vector<std::shared_ptr<Goal>> copied;
        copied.reserve(body.size());
        for (const auto& goal : body) copied.push_back(goal->clone());
        return std::make_shared<WhileGoal>(condition->clone(), std::move(copied));
    }
    std::string debug() const override { return "while " + condition->debug(); }
};

// One `catch name then ...` branch of a TryGoal.
struct CatchClause {
    CatchClause(std::string variable, SymbolId variableId,
                std::vector<std::shared_ptr<Goal>> body)
        : variable(std::move(variable)), variableId(variableId), body(std::move(body)) {}
    CatchClause(std::string name, std::vector<std::shared_ptr<Goal>> body)
        : CatchClause(name, symbolIdForName(name), std::move(body)) {}

    std::string variable;
    SymbolId variableId = 0;
    std::vector<std::shared_ptr<Goal>> body;
};

// try ... catch e then ... catch k then ... end. The try body runs to
// completion in its own solve. A native error or a thrown exception object
// binds {kind, message} to the first catch variable and runs that body. If that
// body raises, the next catch receives the new exception, and so on; an error
// raised by the last catch propagates out. A body that merely fails (no error)
// fails the goal like any other conjunction and is not caught.
class TryGoal final : public Goal {
public:
    TryGoal(std::vector<std::shared_ptr<Goal>> tryBody, std::vector<CatchClause> catches)
        : tryBody(std::move(tryBody)), catches(std::move(catches)) {}

    std::vector<std::shared_ptr<Goal>> tryBody;
    std::vector<CatchClause> catches;

    static constexpr GoalKind kKind = GoalKind::Try;
    GoalKind kind() const override { return kKind; }
    std::shared_ptr<Goal> clone() const override {
        std::vector<std::shared_ptr<Goal>> copiedTry;
        copiedTry.reserve(tryBody.size());
        for (const auto& goal : tryBody) copiedTry.push_back(goal->clone());
        std::vector<CatchClause> copiedCatches;
        copiedCatches.reserve(catches.size());
        for (const auto& clause : catches) {
            std::vector<std::shared_ptr<Goal>> body;
            body.reserve(clause.body.size());
            for (const auto& goal : clause.body) body.push_back(goal->clone());
            copiedCatches.emplace_back(clause.variable, clause.variableId, std::move(body));
        }
        return std::make_shared<TryGoal>(std::move(copiedTry), std::move(copiedCatches));
    }
    std::string debug() const override {
        std::string text = "try";
        for (const auto& clause : catches) text += " catch " + clause.variable;
        return text;
    }
};

struct SwitchCase {
    std::shared_ptr<Expr> value;
    std::vector<std::shared_ptr<Goal>> body;
};

class SwitchGoal final : public Goal {
public:
    SwitchGoal(std::shared_ptr<Expr> value, std::vector<SwitchCase> cases)
        : value(std::move(value)), cases(std::move(cases)) {}

    std::shared_ptr<Expr> value;
    std::vector<SwitchCase> cases;

    static constexpr GoalKind kKind = GoalKind::Switch;
    GoalKind kind() const override { return kKind; }
    std::shared_ptr<Goal> clone() const override {
        std::vector<SwitchCase> copied;
        copied.reserve(cases.size());
        for (const auto& branch : cases) {
            SwitchCase next;
            next.value = branch.value ? branch.value->clone() : nullptr;
            next.body.reserve(branch.body.size());
            for (const auto& goal : branch.body) next.body.push_back(goal->clone());
            copied.push_back(std::move(next));
        }
        return std::make_shared<SwitchGoal>(value->clone(), std::move(copied));
    }
    std::string debug() const override { return "switch " + value->debug(); }
};

class BreakGoal final : public Goal {
public:
    static constexpr GoalKind kKind = GoalKind::Break;
    GoalKind kind() const override { return kKind; }
    std::shared_ptr<Goal> clone() const override { return std::make_shared<BreakGoal>(); }
    std::string debug() const override { return "break"; }
};

class ContinueGoal final : public Goal {
public:
    static constexpr GoalKind kKind = GoalKind::Continue;
    GoalKind kind() const override { return kKind; }
    std::shared_ptr<Goal> clone() const override { return std::make_shared<ContinueGoal>(); }
    std::string debug() const override { return "continue"; }
};
// One value expression in a period-terminated callable sequence. Evaluation
// stores its value as the current implicit result without controlling flow;
// later expressions in the sequence still execute.
class ExpressionGoal final : public Goal {
public:
    explicit ExpressionGoal(std::shared_ptr<Expr> expression)
        : expression(std::move(expression)) {}

    std::shared_ptr<Expr> expression;

    static constexpr GoalKind kKind = GoalKind::Expression;
    GoalKind kind() const override { return kKind; }
    std::shared_ptr<Goal> clone() const override {
        return std::make_shared<ExpressionGoal>(expression->clone());
    }
    std::string debug() const override { return expression->debug(); }
};

class GroupGoal final : public Goal {
public:
    explicit GroupGoal(std::vector<std::shared_ptr<Goal>> goals)
        : goals(std::move(goals)) {}

    std::vector<std::shared_ptr<Goal>> goals;

    static constexpr GoalKind kKind = GoalKind::Group;
    GoalKind kind() const override { return kKind; }
    std::shared_ptr<Goal> clone() const override {
        std::vector<std::shared_ptr<Goal>> copied;
        copied.reserve(goals.size());
        for (const auto& goal : goals) copied.push_back(goal->clone());
        return std::make_shared<GroupGoal>(std::move(copied));
    }

    std::string debug() const override {
        std::ostringstream oss;
        oss << "(";
        for (size_t i = 0; i < goals.size(); ++i) {
            if (i) oss << ", ";
            oss << goals[i]->debug();
        }
        oss << ")";
        return oss.str();
    }
};

class OrGoal final : public Goal {
public:
    explicit OrGoal(std::vector<std::vector<std::shared_ptr<Goal>>> branches)
        : branches(std::move(branches)) {}

    std::vector<std::vector<std::shared_ptr<Goal>>> branches;

    static constexpr GoalKind kKind = GoalKind::Or;
    GoalKind kind() const override { return kKind; }
    std::shared_ptr<Goal> clone() const override {
        std::vector<std::vector<std::shared_ptr<Goal>>> copied;
        copied.reserve(branches.size());
        for (const auto& branch : branches) {
            std::vector<std::shared_ptr<Goal>> copiedBranch;
            copiedBranch.reserve(branch.size());
            for (const auto& goal : branch) copiedBranch.push_back(goal->clone());
            copied.push_back(std::move(copiedBranch));
        }
        return std::make_shared<OrGoal>(std::move(copied));
    }

    std::string debug() const override {
        std::ostringstream oss;
        for (size_t i = 0; i < branches.size(); ++i) {
            if (i) oss << " | ";
            for (size_t j = 0; j < branches[i].size(); ++j) {
                if (j) oss << ", ";
                oss << branches[i][j]->debug();
            }
        }
        return oss.str();
    }
};

class Statement : public AstNode {
public:
    virtual ~Statement() = default;
    virtual StatementKind kind() const = 0;
};

class ImportStmt final : public Statement {
public:
    explicit ImportStmt(std::string path) { paths.push_back(std::move(path)); }
    explicit ImportStmt(std::vector<std::string> paths) : paths(std::move(paths)) {}
    std::vector<std::string> paths;

    static constexpr StatementKind kKind = StatementKind::Import;
    StatementKind kind() const override { return kKind; }
    std::string debug() const override {
        std::ostringstream oss;
        oss << "import ";
        if (paths.size() == 1) {
            oss << '"' << paths[0] << '"';
        } else {
            oss << "(";
            for (size_t i = 0; i < paths.size(); ++i) {
                if (i) oss << " ";
                oss << '"' << paths[i] << '"';
            }
            oss << ")";
        }
        oss << ".";
        return oss.str();
    }
};

class ClauseStmt final : public Statement {
public:
    ClauseStmt(Call head, std::vector<std::shared_ptr<Goal>> body)
        : head(std::move(head)), body(std::move(body)),
          clauseKind(this->body.empty() ? ClauseKind::Fact : ClauseKind::Rule) {}
    ClauseStmt(Call head, std::string parentName, std::vector<std::shared_ptr<Goal>> body)
        : head(std::move(head)), parentName(std::move(parentName)), body(std::move(body)),
          clauseKind(this->body.empty() ? ClauseKind::Fact : ClauseKind::Rule) {
        if (!this->parentName.empty()) parentNames.push_back(this->parentName);
    }
    ClauseStmt(Call head,
               std::string parentName,
               std::vector<std::shared_ptr<Goal>> body,
               std::vector<std::vector<std::shared_ptr<Goal>>> fallbackBranches,
               bool emptyDeclaration = false,
               ClauseKind clauseKind = ClauseKind::Rule)
        : head(std::move(head)), parentName(std::move(parentName)), body(std::move(body)),
          fallbackBranches(std::move(fallbackBranches)), emptyDeclaration(emptyDeclaration),
          clauseKind(clauseKind) {
        if (!this->parentName.empty()) parentNames.push_back(this->parentName);
    }
    ClauseStmt(Call head,
               std::vector<std::string> parentNames,
               std::vector<std::shared_ptr<Goal>> body,
               std::vector<std::vector<std::shared_ptr<Goal>>> fallbackBranches,
               bool emptyDeclaration = false,
               ClauseKind clauseKind = ClauseKind::Rule)
        : head(std::move(head)), parentNames(std::move(parentNames)), body(std::move(body)),
          fallbackBranches(std::move(fallbackBranches)), emptyDeclaration(emptyDeclaration),
          clauseKind(clauseKind) {
        if (!this->parentNames.empty()) parentName = this->parentNames.front();
    }

    Call head;
    // parentName is retained as the primary-parent compatibility view. All
    // hierarchy traversal uses parentNames, which preserves every declared
    // direct parent in source order.
    std::string parentName;
    std::vector<std::string> parentNames;
    // Semantic designations are fact-instance metadata. They are neither
    // schema fields nor inheritance edges.
    std::vector<std::string> designations;
    std::vector<SymbolId> designationIds;
    std::vector<std::shared_ptr<Goal>> body; // empty body without => () means fact
    std::vector<std::vector<std::shared_ptr<Goal>>> fallbackBranches;
    bool emptyDeclaration = false;
    ClauseKind clauseKind = ClauseKind::Rule;
    std::string module;
    // An annotation is a normal method application evaluated against this
    // declaration. Built-in annotations and user annotations share this AST.
    std::vector<Call> annotations;

    static constexpr StatementKind kKind = StatementKind::Clause;
    StatementKind kind() const override { return kKind; }
    bool isFact() const { return clauseKind == ClauseKind::Fact; }

    std::string debug() const override {
        std::ostringstream oss;
        for (const auto& annotation : annotations) {
            oss << "@" << annotation.debug() << "\n";
        }
        if (clauseKind != ClauseKind::EntryCall) oss << "def ";
        oss << head.name;
        if (!parentNames.empty()) {
            oss << " extend ";
            for (size_t i = 0; i < parentNames.size(); ++i) {
                if (i) oss << ", ";
                oss << parentNames[i];
            }
        } else if (!parentName.empty()) oss << " extend " << parentName;
        oss << "(";
        for (size_t i = 0; i < head.args.size(); ++i) {
            if (i) oss << ", ";
            oss << head.args[i].debug();
        }
        oss << ")";
        if (!designations.empty()) {
            oss << " as ";
            for (size_t i = 0; i < designations.size(); ++i) {
                if (i) oss << ", ";
                oss << designations[i];
            }
        }
        if (emptyDeclaration) {
            oss << " => ()";
        } else if (!body.empty()) {
            oss << " => ";
            for (size_t i = 0; i < body.size(); ++i) {
                if (i) oss << ", ";
                oss << body[i]->debug();
            }
            for (const auto& branch : fallbackBranches) {
                oss << " else ";
                for (size_t i = 0; i < branch.size(); ++i) {
                    if (i) oss << ", ";
                    oss << branch[i]->debug();
                }
            }
        }
        oss << ".";
        return oss.str();
    }
};

class GlobalBindingStmt final : public Statement {
public:
    GlobalBindingStmt(std::string name, std::shared_ptr<Expr> expr,
                      TypeRef declaredType = {})
        : name(std::move(name)), expr(std::move(expr)),
          declaredType(std::move(declaredType)) {}

    std::string name;
    std::shared_ptr<Expr> expr;
    TypeRef declaredType;

    static constexpr StatementKind kKind = StatementKind::GlobalBinding;
    StatementKind kind() const override { return kKind; }
    std::string debug() const override {
        return "def " + name + (declaredType.name.empty()
            ? std::string{}
            : ": " + declaredType.canonical()) +
            " := " + expr->debug() + ".";
    }
};

struct ClassFieldDecl {
    using TypeRef = Felidae::TypeRef;

    ClassFieldDecl(std::string name,
                   SymbolId nameId,
                   TypeRef type,
                   std::shared_ptr<Expr> defaultValue,
                   SourceSpan sourceSpan)
        : name(std::move(name)), nameId(nameId), type(std::move(type)),
          defaultValue(std::move(defaultValue)), sourceSpan(sourceSpan) {}

    std::string name;
    SymbolId nameId = 0;
    TypeRef type;
    std::shared_ptr<Expr> defaultValue;
    SourceSpan sourceSpan;
};

struct ClassIndexDecl {
    std::vector<std::string> fields;
    std::vector<SymbolId> fieldIds;
    SourceSpan sourceSpan;
};

enum class GraphTraversalKind : std::uint8_t {
    Join,
    RecursiveJoin,
    ShortestPath
};

// Runtime-only lazy graph query. The source fact cursor and traversal bounds
// are immutable; RocksDB/in-memory adjacency is touched only by a terminal
// array operation or when the value crosses the program output boundary.
class GraphSelectionExpr final : public Expr {
public:
    GraphSelectionExpr(GraphTraversalKind traversal,
                       std::shared_ptr<FactSelectionExpr> source,
                       std::string direction,
                       std::shared_ptr<MapExpr> propertyMatch = {},
                       std::size_t minDepth = 1,
                       std::size_t maxDepth = 1,
                       std::uint64_t targetId = 0)
        : traversal(traversal), source(std::move(source)),
          direction(std::move(direction)), propertyMatch(std::move(propertyMatch)),
          minDepth(minDepth), maxDepth(maxDepth), targetId(targetId) {}

    GraphTraversalKind traversal = GraphTraversalKind::Join;
    std::shared_ptr<FactSelectionExpr> source;
    std::string direction;
    std::shared_ptr<MapExpr> propertyMatch;
    std::size_t minDepth = 1;
    std::size_t maxDepth = 1;
    std::uint64_t targetId = 0;

    static constexpr ExprKind kKind = ExprKind::GraphSelection;
    ExprKind kind() const override { return kKind; }
    std::shared_ptr<Expr> clone() const override {
        return std::make_shared<GraphSelectionExpr>(
            traversal,
            source ? std::static_pointer_cast<FactSelectionExpr>(source->clone()) : nullptr,
            direction,
            propertyMatch ? std::static_pointer_cast<MapExpr>(propertyMatch->clone()) : nullptr,
            minDepth, maxDepth, targetId);
    }
    std::string debug() const override {
        const char* operation = traversal == GraphTraversalKind::Join ? "join" :
            traversal == GraphTraversalKind::RecursiveJoin ? "recursive_join" : "shortest_path";
        return std::string("<graph:") + operation + ">";
    }
};

struct ClassKeyDecl {
    std::vector<std::string> fields;
    std::vector<SymbolId> fieldIds;
    SourceSpan sourceSpan;
};

// A schema remains source-level AST.  The direct interpreter registers its
// inheritance and the contained methods without an IR or binary conversion.
class ClassStmt final : public Statement {
public:
    ClassStmt(std::string name, SymbolId nameId,
              std::vector<std::string> parentNames,
              std::vector<ClassFieldDecl> fields,
              ClassKeyDecl key = {},
              std::vector<ClassIndexDecl> indexes = {},
              std::vector<std::shared_ptr<ClauseStmt>> methods = {})
        : name(std::move(name)), nameId(nameId),
          parentNames(std::move(parentNames)), fields(std::move(fields)),
          key(std::move(key)), indexes(std::move(indexes)), methods(std::move(methods)) {}

    std::string name;
    SymbolId nameId = 0;
    std::vector<std::string> parentNames;
    std::vector<ClassFieldDecl> fields;
    ClassKeyDecl key;
    std::vector<ClassIndexDecl> indexes;
    std::vector<std::shared_ptr<ClauseStmt>> methods;

    static constexpr StatementKind kKind = StatementKind::Class;
    StatementKind kind() const override { return kKind; }
    std::string debug() const override { return renderDeclaration(true); }
    std::string schemaFingerprint() const { return renderDeclaration(false); }

private:
    std::string renderDeclaration(bool includeMethods) const {
        std::ostringstream oss;
        oss << "class " << name;
        if (!parentNames.empty()) {
            oss << " extend ";
            for (std::size_t i = 0; i < parentNames.size(); ++i) {
                if (i) oss << ", ";
                oss << parentNames[i];
            }
        }
        oss << '\n';
        for (const auto& field : fields) {
            oss << "  def " << field.name << ": " << field.type.canonical();
            if (field.defaultValue) oss << " := " << field.defaultValue->debug();
            oss << ".\n";
        }
        if (!key.fields.empty()) {
            oss << "  key(";
            for (std::size_t i = 0; i < key.fields.size(); ++i) {
                if (i) oss << ", ";
                oss << key.fields[i];
            }
            oss << ").\n";
        }
        for (const auto& index : indexes) {
            oss << "  index(";
            for (std::size_t i = 0; i < index.fields.size(); ++i) {
                if (i) oss << ", ";
                oss << index.fields[i];
            }
            oss << ").\n";
        }
        if (includeMethods) {
            for (const auto& method : methods) oss << "  " << method->debug() << '\n';
        }
        return oss.str() + "end";
    }
};

class Program final : public AstNode {
public:
    std::vector<std::shared_ptr<Statement>> statements;
    std::vector<std::shared_ptr<ImportStmt>> imports;
    std::vector<std::shared_ptr<ClauseStmt>> clauses;
    std::vector<std::shared_ptr<GlobalBindingStmt>> globals;
    std::vector<std::shared_ptr<ClassStmt>> classes;

    void addStatement(std::shared_ptr<Statement> statement) {
        switch (statement->kind()) {
            case StatementKind::Import:
                imports.push_back(std::static_pointer_cast<ImportStmt>(statement));
                break;
            case StatementKind::Clause:
                clauses.push_back(std::static_pointer_cast<ClauseStmt>(statement));
                break;
            case StatementKind::GlobalBinding:
                globals.push_back(std::static_pointer_cast<GlobalBindingStmt>(statement));
                break;
            case StatementKind::Class:
                classes.push_back(std::static_pointer_cast<ClassStmt>(statement));
                for (const auto& method : classes.back()->methods) clauses.push_back(method);
                break;
        }
        statements.push_back(std::move(statement));
    }

    std::string debug() const override {
        std::ostringstream oss;
        for (const auto& s : statements) oss << s->debug() << "\n";
        return oss.str();
    }
};

// Checked downcast by integer kind: the replacement for dynamic_pointer_cast on
// AST nodes. Every concrete node class is final and owns exactly one kKind, so
// comparing one integer and then static-casting is equivalent to the RTTI cast.
// RTTI compares type-name strings, and profiling showed that (strcmp,
// __dynamic_cast, __do_dyncast) was 35-40% of interpretation time.
// Returns null when `node` is null or of a different kind, like dynamic_pointer_cast.
template <typename Node, typename Base>
inline std::shared_ptr<Node> nodeAs(const std::shared_ptr<Base>& node) noexcept {
    if (!node || node->kind() != Node::kKind) return nullptr;
    return std::static_pointer_cast<Node>(node);
}

} // namespace Felidae
