#pragma once

#include "Operator.h"
#include "Token.h"
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
    Number,
    Bool,
    Nil,
    Var,
    Array,
    Map,
    Access,
    Operator,
    Term,
    Lambda,
    FactSelection,
    AstValue
};

enum class GoalKind {
    Call,
    Binary,
    Assign,
    MultiAssign,
    Where,
    If,
    Return,
    Not,
    Group,
    Or
};

enum class StatementKind {
    Import,
    Clause,
    GlobalBinding
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
    // completed. Source text remains owned by the lexer/editor, so retaining
    // an AST node never pins a complete input buffer.
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

    ExprKind kind() const override { return ExprKind::String; }
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

class NumberExpr final : public Expr {
public:
    explicit NumberExpr(double value) : value(value) {}
    double value;

    ExprKind kind() const override { return ExprKind::Number; }
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

    ExprKind kind() const override { return ExprKind::Bool; }
    std::shared_ptr<Expr> clone() const override { return std::make_shared<BoolExpr>(value); }
    std::string debug() const override { return value ? "true" : "false"; }
};

class NilExpr final : public Expr {
public:
    ExprKind kind() const override { return ExprKind::Nil; }
    std::shared_ptr<Expr> clone() const override { return std::make_shared<NilExpr>(); }
    std::string debug() const override { return "nil"; }
};

class VarExpr final : public Expr {
public:
    explicit VarExpr(std::string name, LanguageTypeId languageTypeId = LanguageTypeId::Unknown)
        : name(std::move(name)), nameId(languageTypeId == LanguageTypeId::Unknown ? symbolIdForName(this->name) : 0),
          languageTypeId(languageTypeId) {}
    std::string name;
    SymbolId nameId = 0;
    LanguageTypeId languageTypeId = LanguageTypeId::Unknown;

    ExprKind kind() const override { return ExprKind::Var; }
    std::shared_ptr<Expr> clone() const override { return std::make_shared<VarExpr>(name, languageTypeId); }
    std::string debug() const override { return name; }
};

class ArrayExpr final : public Expr {
public:
    explicit ArrayExpr(std::vector<std::shared_ptr<Expr>> items)
        : items(std::move(items)) {}

    std::vector<std::shared_ptr<Expr>> items;

    ExprKind kind() const override { return ExprKind::Array; }
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
    // Runtime-only identity for a value materialized from FactMemory.  It is
    // deliberately absent from debug(), serialization and structural
    // equality: two facts may have equal visible fields while retaining
    // distinct identities for dependencies and relationships.
    std::uint64_t factIdentity = 0;

    ExprKind kind() const override { return ExprKind::Map; }
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

    ExprKind kind() const override { return ExprKind::AstValue; }
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

// Runtime-only lazy fact query. It deliberately is not a MapExpr: user map
// operations cannot mutate or counterfeit its snapshot/cursor metadata.
class FactSelectionExpr final : public Expr {
public:
    FactSelectionExpr(std::string factType,
                      std::uint64_t snapshotGeneration,
                      std::string field = {},
                      std::shared_ptr<Expr> equals = nullptr)
        : factType(std::move(factType)),
          factTypeId(symbolIdForName(this->factType)),
          snapshotGeneration(snapshotGeneration),
          field(std::move(field)),
          fieldId(this->field.empty() ? 0 : symbolIdForName(this->field)),
          equals(std::move(equals)) {}

    std::string factType;
    SymbolId factTypeId = 0;
    std::uint64_t snapshotGeneration = 0;
    std::string field;
    SymbolId fieldId = 0;
    std::shared_ptr<Expr> equals;

    ExprKind kind() const override { return ExprKind::FactSelection; }
    std::shared_ptr<Expr> clone() const override {
        return std::make_shared<FactSelectionExpr>(
            factType,
            snapshotGeneration,
            field,
            equals ? equals->clone() : nullptr);
    }
    std::string debug() const override {
        std::ostringstream out;
        out << "{__type: \"FactSelection\", fact_type: \""
            << factType << "\", source: \"memory\", snapshot_generation: "
            << snapshotGeneration;
        if (!field.empty()) {
            out << ", field: \"" << field << "\"";
            if (equals) out << ", equals: " << equals->debug();
        }
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

    ExprKind kind() const override { return ExprKind::Access; }
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

    ExprKind kind() const override { return ExprKind::Operator; }
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

struct Arg {
    Arg() = default;
    Arg(std::string name, std::shared_ptr<Expr> value)
        : name(std::move(name)), nameId(this->name.empty() ? 0 : symbolIdForName(this->name)),
          value(std::move(value)) {}

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
        : name(std::move(name)), nameId(builtinId == BuiltinId::Unknown ? symbolIdForName(this->name) : 0),
          builtinId(builtinId), args(std::move(args)) {}

    std::string name;
    SymbolId nameId = 0;
    BuiltinId builtinId = BuiltinId::Unknown;
    std::vector<Arg> args;

    ExprKind kind() const override { return ExprKind::Term; }
    std::shared_ptr<Expr> clone() const override {
        std::vector<Arg> copied;
        copied.reserve(args.size());
        for (const auto& arg : args) copied.push_back(Arg{arg.name, arg.value->clone()});
        return std::make_shared<TermExpr>(name, std::move(copied), builtinId);
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
               TokenType op = TokenType::End,
               std::shared_ptr<Expr> right = {})
        : source(std::move(source)), variable(std::move(variable)),
          body(std::move(body)), op(std::move(op)), right(std::move(right)) {}

    std::shared_ptr<Expr> source;
    std::string variable;
    std::shared_ptr<Expr> body;
    TokenType op;
    std::shared_ptr<Expr> right;

    ExprKind kind() const override { return ExprKind::Lambda; }
    std::shared_ptr<Expr> clone() const override {
        return std::make_shared<LambdaExpr>(
            source->clone(), variable, body->clone(), op, right ? right->clone() : nullptr);
    }

    std::string debug() const override {
        std::ostringstream oss;
        oss << "lambda(" << source->debug() << ", " << variable << " => " << body->debug();
        if (op != TokenType::End) oss << " " << tokenTypeName(op) << " " << right->debug();
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

    Call() = default;
    Call(std::string name, std::vector<Arg> args, BuiltinId builtinId = BuiltinId::Unknown)
        : name(std::move(name)), nameId(builtinId == BuiltinId::Unknown ? symbolIdForName(this->name) : 0),
          builtinId(builtinId), args(std::move(args)) {}

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

class Goal : public AstNode {
public:
    virtual GoalKind kind() const = 0;
    virtual std::shared_ptr<Goal> clone() const = 0;
};

class CallGoal final : public Goal {
public:
    explicit CallGoal(Call call) : call(std::move(call)) {}
    Call call;

    GoalKind kind() const override { return GoalKind::Call; }
    std::shared_ptr<Goal> clone() const override {
        Call copy;
        copy.name = call.name;
        copy.nameId = call.nameId;
        copy.builtinId = call.builtinId;
        for (const auto& a : call.args) {
            copy.args.push_back(Arg{a.name, a.value->clone()});
        }
        return std::make_shared<CallGoal>(std::move(copy));
    }

    std::string debug() const override { return call.debug(); }
};

class BinaryGoal final : public Goal {
public:
    BinaryGoal(std::shared_ptr<Expr> left, TokenType op, std::shared_ptr<Expr> right)
        : left(std::move(left)), op(std::move(op)), right(std::move(right)) {}

    std::shared_ptr<Expr> left;
    TokenType op;
    std::shared_ptr<Expr> right;

    GoalKind kind() const override { return GoalKind::Binary; }
    std::shared_ptr<Goal> clone() const override {
        return std::make_shared<BinaryGoal>(left->clone(), op, right->clone());
    }

    std::string debug() const override {
        return left->debug() + " " + tokenTypeName(op) + " " + right->debug();
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

    GoalKind kind() const override { return GoalKind::Not; }
    std::shared_ptr<Goal> clone() const override {
        Call copy;
        copy.name = call.name;
        copy.nameId = call.nameId;
        copy.builtinId = call.builtinId;
        for (const auto& arg : call.args) copy.args.push_back(Arg{arg.name, arg.value->clone()});
        return std::make_shared<NotGoal>(std::move(copy));
    }
    std::string debug() const override { return "not " + call.debug(); }
};

class AssignGoal final : public Goal {
public:
    AssignGoal(std::string name, std::shared_ptr<Expr> expr)
        : name(std::move(name)), expr(std::move(expr)) {}

    std::string name;
    std::shared_ptr<Expr> expr;

    GoalKind kind() const override { return GoalKind::Assign; }
    std::shared_ptr<Goal> clone() const override {
        if (!expr) return std::make_shared<AssignGoal>(name, std::shared_ptr<Expr>{});
        return std::make_shared<AssignGoal>(name, expr->clone());
    }

    std::string debug() const override {
        return name + " := " + (expr ? expr->debug() : "<missing expression>");
    }
};

struct AssignmentTarget {
    std::string name;
    std::string type;

    std::string debug() const {
        return type.empty() ? name : name + ": " + type;
    }
};

class MultiAssignGoal final : public Goal {
public:
    MultiAssignGoal(std::vector<AssignmentTarget> targets, std::shared_ptr<Expr> expr)
        : targets(std::move(targets)), expr(std::move(expr)) {}

    std::vector<AssignmentTarget> targets;
    std::shared_ptr<Expr> expr;

    GoalKind kind() const override { return GoalKind::MultiAssign; }
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

    GoalKind kind() const override { return GoalKind::Where; }
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

    GoalKind kind() const override { return GoalKind::If; }
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
class ReturnGoal final : public Goal {
public:
    explicit ReturnGoal(std::vector<Arg> fields) : fields(std::move(fields)) {}

    std::vector<Arg> fields;

    GoalKind kind() const override { return GoalKind::Return; }
    std::shared_ptr<Goal> clone() const override {
        std::vector<Arg> copied;
        copied.reserve(fields.size());
        for (const auto& field : fields) copied.push_back(Arg{field.name, field.value->clone()});
        return std::make_shared<ReturnGoal>(std::move(copied));
    }

    std::string debug() const override {
        std::ostringstream oss;
        oss << "return (";
        for (size_t i = 0; i < fields.size(); ++i) {
            if (i) oss << ", ";
            oss << fields[i].debug();
        }
        oss << ")";
        return oss.str();
    }
};

class GroupGoal final : public Goal {
public:
    explicit GroupGoal(std::vector<std::shared_ptr<Goal>> goals)
        : goals(std::move(goals)) {}

    std::vector<std::shared_ptr<Goal>> goals;

    GoalKind kind() const override { return GoalKind::Group; }
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

    GoalKind kind() const override { return GoalKind::Or; }
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

    StatementKind kind() const override { return StatementKind::Import; }
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
    std::vector<std::shared_ptr<Goal>> body; // empty body without => () means fact
    std::vector<std::vector<std::shared_ptr<Goal>>> fallbackBranches;
    bool emptyDeclaration = false;
    ClauseKind clauseKind = ClauseKind::Rule;
    std::string module;
    // An annotation is a normal method application evaluated against this
    // declaration. Built-in annotations and user annotations share this AST.
    std::vector<Call> annotations;

    StatementKind kind() const override { return StatementKind::Clause; }
    bool isFact() const { return clauseKind == ClauseKind::Fact; }

    std::string debug() const override {
        std::ostringstream oss;
        for (const auto& annotation : annotations) {
            oss << "@" << annotation.debug() << "\n";
        }
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
    GlobalBindingStmt(std::string name, std::shared_ptr<Expr> expr)
        : name(std::move(name)), expr(std::move(expr)) {}

    std::string name;
    std::shared_ptr<Expr> expr;

    StatementKind kind() const override { return StatementKind::GlobalBinding; }
    std::string debug() const override {
        return name + " := " + expr->debug() + ".";
    }
};

class Program final : public AstNode {
public:
    std::vector<std::shared_ptr<Statement>> statements;
    std::vector<std::shared_ptr<ImportStmt>> imports;
    std::vector<std::shared_ptr<ClauseStmt>> clauses;
    std::vector<std::shared_ptr<GlobalBindingStmt>> globals;

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
        }
        statements.push_back(std::move(statement));
    }

    std::string debug() const override {
        std::ostringstream oss;
        for (const auto& s : statements) oss << s->debug() << "\n";
        return oss.str();
    }
};

} // namespace Felidae
