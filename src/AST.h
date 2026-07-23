#pragma once

#include "Token.h"
#include <iomanip>
#include <memory>
#include <sstream>
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
    Binary,
    Pipeline,
    Term,
    Lambda
};

enum class GoalKind {
    Call,
    Binary,
    Assign,
    MultiAssign,
    Where,
    If,
    Return,
    Group,
    Or
};

enum class StatementKind {
    Import,
    Clause,
    GlobalBinding
};

class AstNode {
public:
    virtual ~AstNode() = default;
    virtual std::string debug() const = 0;
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

    ExprKind kind() const override { return ExprKind::Map; }
    std::shared_ptr<Expr> clone() const override {
        std::vector<MapEntry> copied;
        copied.reserve(entries.size());
        for (const auto& entry : entries) {
            copied.push_back(MapEntry{entry.key, entry.value->clone()});
        }
        return std::make_shared<MapExpr>(std::move(copied));
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

class BinaryExpr final : public Expr {
public:
    BinaryExpr(std::shared_ptr<Expr> left, TokenType op, std::shared_ptr<Expr> right)
        : left(std::move(left)), op(std::move(op)), right(std::move(right)) {}

    std::shared_ptr<Expr> left;
    TokenType op;
    std::shared_ptr<Expr> right;

    ExprKind kind() const override { return ExprKind::Binary; }
    std::shared_ptr<Expr> clone() const override {
        return std::make_shared<BinaryExpr>(left->clone(), op, right->clone());
    }

    std::string debug() const override {
        return left->debug() + " " + tokenTypeName(op) + " " + right->debug();
    }
};

class PipelineExpr final : public Expr {
public:
    PipelineExpr(std::shared_ptr<Expr> left, std::shared_ptr<Expr> right)
        : left(std::move(left)), right(std::move(right)) {}

    std::shared_ptr<Expr> left;
    std::shared_ptr<Expr> right;

    ExprKind kind() const override { return ExprKind::Pipeline; }
    std::shared_ptr<Expr> clone() const override {
        return std::make_shared<PipelineExpr>(left->clone(), right->clone());
    }

    std::string debug() const override {
        return left->debug() + " then " + right->debug();
    }
};

struct Arg {
    std::string name; // empty means positional argument
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

class AssignGoal final : public Goal {
public:
    AssignGoal(std::string name, std::shared_ptr<Goal> goal)
        : name(std::move(name)), goal(std::move(goal)) {}
    AssignGoal(std::string name, std::shared_ptr<Expr> expr)
        : name(std::move(name)), expr(std::move(expr)) {}

    std::string name;
    std::shared_ptr<Goal> goal;
    std::shared_ptr<Expr> expr;

    GoalKind kind() const override { return GoalKind::Assign; }
    std::shared_ptr<Goal> clone() const override {
        if (goal) return std::make_shared<AssignGoal>(name, goal->clone());
        if (!expr) return std::make_shared<AssignGoal>(name, std::shared_ptr<Expr>{});
        return std::make_shared<AssignGoal>(name, expr->clone());
    }

    std::string debug() const override {
        if (goal) return name + " := " + goal->debug();
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
        : head(std::move(head)), body(std::move(body)) {}
    ClauseStmt(Call head, std::string parentName, std::vector<std::shared_ptr<Goal>> body)
        : head(std::move(head)), parentName(std::move(parentName)), body(std::move(body)) {}
    ClauseStmt(Call head,
               std::string parentName,
               std::vector<std::shared_ptr<Goal>> body,
               std::vector<std::vector<std::shared_ptr<Goal>>> fallbackBranches,
               bool emptyDeclaration = false)
        : head(std::move(head)), parentName(std::move(parentName)), body(std::move(body)),
          fallbackBranches(std::move(fallbackBranches)), emptyDeclaration(emptyDeclaration) {}

    Call head;
    std::string parentName;
    std::vector<std::shared_ptr<Goal>> body; // empty body without => () means fact
    std::vector<std::vector<std::shared_ptr<Goal>>> fallbackBranches;
    bool emptyDeclaration = false;

    StatementKind kind() const override { return StatementKind::Clause; }
    bool isFact() const { return body.empty() && fallbackBranches.empty() && !emptyDeclaration; }

    std::string debug() const override {
        std::ostringstream oss;
        oss << head.name;
        if (!parentName.empty()) oss << " extend " << parentName;
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
