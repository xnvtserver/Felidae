#pragma once

#include "AST.h"
#include "Token.h"
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace Felidae {

class ParserError : public std::runtime_error {
public:
    explicit ParserError(const std::string& msg) : std::runtime_error(msg) {}
};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    Program parseProgram();
    std::vector<std::shared_ptr<Goal>> parseQuery();
    std::shared_ptr<Expr> parseExpressionText();

private:
    std::vector<Token> tokens_;
    std::set<std::string> globals_;
    std::map<std::string, std::set<std::string>> predicateFields_;
    std::set<std::string> knownTypes_;
    std::set<std::string> methodPredicates_;
    size_t pos_ = 0;
    size_t anonymousCounter_ = 0;

    const Token& peek() const;
    const Token& previous() const;
    bool check(TokenType type) const;
    bool isAtEnd() const;
    const Token& advance();
    bool match(TokenType type);
    const Token& consume(TokenType type, const std::string& message);
    void rejectUnsupportedTokens() const;
    bool checkElse() const;

    std::shared_ptr<Statement> parseStatement();
    std::shared_ptr<ImportStmt> parseImport();
    std::shared_ptr<ClauseStmt> parseClause();
    std::shared_ptr<GlobalBindingStmt> parseGlobalBinding();

    Call parseCall();
    Call parseCall(bool allowPositional);
    Call parseCallFromName(std::string name, bool allowPositional);
    std::string parseQualifiedName();
    std::vector<Arg> parseArgList();
    Arg parseArg();

    std::shared_ptr<Goal> parseGoal();
    bool isMultiAssignmentStart() const;
    std::vector<AssignmentTarget> parseAssignmentTargets();
    std::vector<std::shared_ptr<Goal>> parseGoalConjunction();
    std::vector<std::shared_ptr<Goal>> parseGoalList();
    void splitFallbackPrelude(std::vector<std::shared_ptr<Goal>>& primary,
                              std::vector<std::vector<std::shared_ptr<Goal>>>& fallbackBranches) const;
    std::shared_ptr<Expr> parseExpr();
    std::shared_ptr<Expr> parseAccessExpr();
    std::shared_ptr<Expr> parseAdditiveExpr();
    std::shared_ptr<Expr> parseMultiplicativeExpr();
    std::shared_ptr<Expr> parsePrimaryExpr();
    std::shared_ptr<Expr> parseLambdaExpr();
    std::shared_ptr<Expr> parseMapExpr();
    std::shared_ptr<Expr> parseArrayExpr();

    bool isMethodStyleHead(const Call& head) const;
    bool isTypeNameKnown(const std::string& name) const;
    bool isBuiltinCallName(const std::string& name) const;
    void validateCallFields(const Call& call) const;
    bool containsAccessExpr(const std::shared_ptr<Expr>& expr) const;
    void validateSystemResultUsage(const std::shared_ptr<Expr>& expr, bool allowed) const;
    void validateGoalSystemResultUsage(const std::shared_ptr<Goal>& goal) const;
    void collectExprVars(const std::shared_ptr<Expr>& expr, std::set<std::string>& vars) const;
    void validateGoalVars(const std::shared_ptr<Goal>& goal, std::set<std::string>& declared) const;
    bool isDeclaredName(const std::string& name, const std::set<std::string>& declared) const;
    void validateRuleVars(const Call& head,
                          const std::vector<std::shared_ptr<Goal>>& body,
                          const std::vector<std::vector<std::shared_ptr<Goal>>>& fallbackBranches = {}) const;
    bool isComparison(TokenType type) const;
    std::string comparisonText(TokenType type) const;
};

} // namespace Felidae
