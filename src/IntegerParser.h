#pragma once

#include "AST.h"
#include "IntegerTokenList.h"

#include <cstddef>
#include <memory>
#include <stdexcept>

namespace Felidae {

struct IntegerParserMetrics {
    std::size_t tokenCount = 0;
    std::size_t statementCount = 0;
    std::size_t iterations = 0;
    std::size_t peakRecursionDepth = 0;
    std::size_t backtrackingAttempts = 0;
    std::size_t sourceEncodeCount = 0;
};

class IntegerParserError : public std::runtime_error {
public:
    explicit IntegerParserError(const std::string& message) : std::runtime_error(message) {}
};

// Direct SentencePiece-ID grammar assembler. It has no secondary tokenizer,
// source-character syntax scanner, or spelling-to-token lookup table. IDs
// determine all syntax; original source is retained only to copy an already
// bounded identifier payload into the IR for SymbolId interning.
class IntegerParser {
public:
    explicit IntegerParser(const IntegerTokenList& input,
                           std::shared_ptr<OperatorRegistry> operators = {});

    Program parseProgram();
    std::vector<std::shared_ptr<Goal>> parseQuery();
    std::shared_ptr<Expr> parseExpressionText();
    bool startsQuery();
    const IntegerParserMetrics& metrics() const noexcept { return metrics_; }

private:
    struct QualifiedName {
        std::string spelling;
        SymbolId nameId = 0;
        BuiltinId builtinId = BuiltinId::Unknown;
        bool isCapitalized = false;
    };
    const IntegerTokenList& input_;
    std::shared_ptr<OperatorRegistry> operators_;
    std::size_t piece_ = 0;
    std::size_t byte_ = 0;
    std::size_t recursionDepth_ = 0;
    IntegerParserMetrics metrics_;

    static constexpr std::size_t kMaximumRecursionDepth = 512;
    static constexpr std::size_t kMaximumIterations = 1'000'000;

    class RecursionScope {
    public:
        explicit RecursionScope(IntegerParser& parser);
        ~RecursionScope();
    private:
        IntegerParser& parser_;
    };

    void step();
    void skipTrivia();
    void alignPiece();
    bool at(TokenId::Id id);
    bool match(TokenId::Id id);
    void require(TokenId::Id id, const char* message);
    bool atEnd();
    std::shared_ptr<Expr> parseExpression();
    std::shared_ptr<Expr> parseBinaryExpression(int minimumPrecedence,
                                                TokenId::Id stop = TokenId::UNKNOWN,
                                                const std::vector<PatternLexeme>* stopAnchor = nullptr);
    std::shared_ptr<Expr> parseUnary();
    std::shared_ptr<Expr> tryParseLeadingPattern();
    std::shared_ptr<Expr> tryParseTrailingPattern(std::shared_ptr<Expr> left,
                                                  int minimumPrecedence);
    bool atPatternLexeme(const PatternLexeme& lexeme);
    bool atPatternAnchor(const std::vector<PatternLexeme>& anchor);
    bool matchPatternLexeme(const PatternLexeme& lexeme);
    bool matchPatternAnchor(const std::vector<PatternLexeme>& anchor);
    std::shared_ptr<Expr> parsePrimary();
    std::shared_ptr<Expr> parseArray();
    std::shared_ptr<Expr> parseMap();
    std::vector<Arg> parseArguments(bool allowAnnotationBindings = false);
    QualifiedName consumeQualifiedName(bool allowNamespaceSeparators = true);
    Call parseCall();
    std::shared_ptr<Goal> parseGoal();
    std::vector<std::shared_ptr<Goal>> parseGoalList(TokenId::Id terminator);
    std::shared_ptr<Statement> parseStatement();
    Call parseAnnotation();
    void prepareOperatorAnnotation(const Call& annotation);
    const OperatorPatternDefinition& registerOperatorPattern(OperatorPatternDefinition pattern);
    std::string consumeNameRange();
    std::string consumeString();
    double consumeNumber();
    bool atNameRange();
    bool sourceContainsLineBreak(std::size_t begin, std::size_t end) const;
    bool lineBreakBeforeNextSignificantPiece() const;
    std::size_t sourceLineIndent(std::size_t offset) const;
    void consumeStatementTerminator(std::size_t statementBegin);
    SourceSpan span(std::size_t begin, std::size_t end) const;
    void stamp(const std::shared_ptr<AstNode>& node, std::size_t begin, std::size_t end) const;
};

} // namespace Felidae
