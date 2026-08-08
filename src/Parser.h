#pragma once

#include "AST.h"
#include "Lexer.h"
#include "Token.h"
#include <functional>
#include <map>
#include <memory>
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
    explicit Parser(std::vector<Token> tokens,
                    std::shared_ptr<OperatorRegistry> operators = std::make_shared<OperatorRegistry>(),
                    std::string module = {})
        : tokens_(std::move(tokens)), operators_(std::move(operators)), module_(std::move(module)) {
        virtualAnchorCount_ = operators_->virtualAnchorCount();
        markVirtualAnchorsInBufferedTokens();
    }
    explicit Parser(Lexer& lexer,
                    std::shared_ptr<OperatorRegistry> operators = std::make_shared<OperatorRegistry>(),
                    std::string module = {})
        : lexer_(&lexer), operators_(std::move(operators)), module_(std::move(module)) {
        lexer_->registerVirtualTokens(operators_->virtualTokens());
        virtualAnchorCount_ = operators_->virtualAnchorCount();
    }

    Program parseProgram();
    void parseProgram(const std::function<void(std::shared_ptr<Statement>)>& consume);
    void bootstrapOperatorPatterns();
    std::vector<std::shared_ptr<Goal>> parseQuery();
    std::shared_ptr<Expr> parseExpressionText();

private:
    mutable std::vector<Token> tokens_;
    Lexer* lexer_ = nullptr;
    std::set<std::string> globals_;
    std::map<std::string, std::set<std::string>> predicateFields_;
    std::set<std::string> knownTypes_;
    std::set<std::string> methodPredicates_;
    std::set<std::string> annotationBindings_;
    std::shared_ptr<OperatorRegistry> operators_;
    std::string module_;
    size_t pos_ = 0;
    size_t anonymousCounter_ = 0;
    bool parsingOperatorAnnotation_ = false;
    std::uint64_t nodeCounter_ = 0;
    mutable std::size_t virtualAnchorCount_ = 0;

    void ensureToken(size_t index) const;
    void syncLexerVirtualAnchors() const;
    const Token& tokenAt(size_t index) const;
    bool hasToken(size_t index) const;
    const Token& peek() const;
    const Token& previous() const;
    bool check(TokenType type) const;
    bool isAtEnd() const;
    const Token& advance();
    bool match(TokenType type);
    const Token& consume(TokenType type, const std::string& message);
    void consumeLogicalNewline();
    bool matchGoalSeparator();
    bool isGoalListTerminator() const;
    void rejectUnsupportedToken(const Token& token) const;
    bool checkElse() const;
    void stampNode(const std::shared_ptr<AstNode>& node,
                   int startLine,
                   int startColumn);

    std::shared_ptr<Statement> parseStatement();
    Call parseAnnotation();
    std::shared_ptr<ImportStmt> parseImport();
    std::shared_ptr<ClauseStmt> parseClause(std::vector<Call> annotations = {});
    void prepareOperatorAnnotation(const Call& annotation);
    bool parseEmptyDeclarationBody();
    void parseRuleBody(const Call& head,
                       std::vector<std::shared_ptr<Goal>>& body,
                       std::vector<std::vector<std::shared_ptr<Goal>>>& fallbackBranches);
    void validateClauseBody(const Call& head,
                            const std::vector<std::shared_ptr<Goal>>& body,
                            const std::vector<std::vector<std::shared_ptr<Goal>>>& fallbackBranches) const;
    std::shared_ptr<GlobalBindingStmt> parseGlobalBinding();

    Call parseCall();
    Call parseCall(bool allowPositional);
    Call parseCallFromName(std::string name, bool allowPositional);
    std::string parseQualifiedName();
    std::vector<Arg> parseArgList();
    Arg parseArg();

    std::shared_ptr<Goal> parseGoal();
    std::shared_ptr<Goal> parseIfGoal();
    std::shared_ptr<BinaryGoal> comparisonGoal(const std::shared_ptr<Expr>& expr,
                                               const std::string& message) const;
    bool isMultiAssignmentStart() const;
    std::vector<AssignmentTarget> parseAssignmentTargets();
    std::vector<std::shared_ptr<Goal>> parseGoalConjunction();
    std::vector<std::shared_ptr<Goal>> parseGoalList();
    void splitFallbackPrelude(std::vector<std::shared_ptr<Goal>>& primary,
                              std::vector<std::vector<std::shared_ptr<Goal>>>& fallbackBranches) const;
    std::shared_ptr<Expr> parseExpr();
    std::shared_ptr<Expr> parseOperatorExpr(int minimumPrecedence,
                                           bool stopAtThen = false,
                                           const PatternLexeme* stopAnchor = nullptr);
    bool patternLexemeMatches(size_t position, const PatternLexeme& lexeme) const;
    const OperatorPatternDefinition* selectScoredPattern(
        const std::vector<const OperatorPatternDefinition*>& candidates,
        size_t start) const;
    std::shared_ptr<Expr> tryParseDeferredTrailingCapturePattern(
        const std::shared_ptr<Expr>& firstCapture,
        int minimumPrecedence,
        bool stopAtThen);
    void consumePatternAnchor(const std::vector<PatternLexeme>& lexemes,
                              size_t offset = 0);
    const OperatorPatternDefinition& registerOperatorPattern(OperatorPatternDefinition pattern);
    void markVirtualAnchorsInBufferedTokens();
    std::shared_ptr<Expr> parseAccessExpr();
    std::shared_ptr<Expr> parseUnaryExpr();
    std::shared_ptr<Expr> parsePrimaryExpr();
    std::shared_ptr<Expr> parseLambdaExpr();
    std::shared_ptr<Expr> parseMapExpr();
    std::shared_ptr<Expr> parseArrayExpr();
    std::shared_ptr<Expr> makeOperatorExpr(CoreOperator op,
                                           std::shared_ptr<Expr> left,
                                           std::shared_ptr<Expr> right) const;

    bool isMethodStyleHead(const Call& head) const;
    std::string typeAnnotationName(const std::shared_ptr<Expr>& expression) const;
    bool isTypeNameKnown(const std::string& name) const;
    void validateCallFields(const Call& call) const;
    bool containsAccessExpr(const std::shared_ptr<Expr>& expr) const;
    void validateSystemResultUsage(const std::shared_ptr<Expr>& expr, bool allowed) const;
    void validateGoalSystemResultUsage(const std::shared_ptr<Goal>& goal) const;
    void validateExceptionFlow(const std::vector<std::shared_ptr<Goal>>& goals) const;
    bool operatorCaptureAcceptsExpressionData(const OperatorExpression& expression,
                                              size_t captureIndex) const;
    void collectExprVars(const std::shared_ptr<Expr>& expr, std::set<std::string>& vars) const;
    void validateGoalVars(const std::shared_ptr<Goal>& goal, std::set<std::string>& declared) const;
    bool isDeclaredName(const std::string& name, const std::set<std::string>& declared) const;
    void validateRuleVars(const Call& head,
                          const std::vector<std::shared_ptr<Goal>>& body,
                          const std::vector<std::vector<std::shared_ptr<Goal>>>& fallbackBranches = {}) const;
    bool isComparison(TokenType type) const;
};

} // namespace Felidae
