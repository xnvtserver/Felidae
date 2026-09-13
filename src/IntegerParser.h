#pragma once

#include "AST.h"
#include "IntegerTokenList.h"

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <unordered_set>
#include <vector>

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

// Direct word-vocabulary-token-ID grammar assembler. It has no secondary
// tokenizer, source-character syntax scanner, or spelling-to-token lookup
// table. IDs determine all syntax; original source is retained only to copy
// an already bounded identifier payload into the IR for SymbolId interning.
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
    bool lastClauseUsedBlockEnd_ = false;
    IntegerParserMetrics metrics_;
    // Byte offsets of every NEWLINE/CARRIAGE_RETURN token, built once (lazily,
    // on first use) and binary-searched by span()/sourceContainsLineBreak()/
    // sourceLineIndent() instead of each replaying the whole token stream
    // from position 0 on every call - see their definitions for why that
    // used to make parsing a long straight-line statement list quadratic.
    mutable std::vector<std::size_t> lineBreakOffsets_;
    mutable bool lineBreakOffsetsBuilt_ = false;
    const std::vector<std::size_t>& lineBreakOffsets() const;

    static constexpr std::size_t kMaximumRecursionDepth = 512;
    // A safety net against a genuinely non-terminating parse loop (a real
    // parser bug), not a cap on legitimate program size. Cost is linear -
    // confirmed by measurement, ~52 iterations per simple statement - so a
    // real 19k-statement program (unremarkable for a generated fact base or
    // a large rule set spread across several imports) used to trip this at
    // only 1,000,000 with an opaque "iteration budget exceeded" error. Wide
    // enough now for a ~1M-statement program (~50M iterations) while still
    // catching a runaway loop in a few seconds rather than hanging forever.
    static constexpr std::size_t kMaximumIterations = 50'000'000;

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
    bool atBlockEnd();
    bool matchBlockEnd();
    void require(TokenId::Id id, const char* message);
    bool atEnd();
    std::shared_ptr<Expr> parseExpression();
    std::shared_ptr<Expr> parseBinaryExpression(int minimumPrecedence,
                                                TokenId::Id stop = TokenId::UNKNOWN,
                                                const std::vector<PatternLexeme>* stopAnchor = nullptr);
    // The precedence-climbing continuation shared by parseBinaryExpression
    // (which parses `left` itself first) and any caller that already has a
    // fully-parsed left operand from elsewhere - see its definition.
    std::shared_ptr<Expr> continueBinaryExpression(std::shared_ptr<Expr> left,
                                                    int minimumPrecedence,
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
    // `allowCapitalizedDotted` exists for the one place `Capitalized.name`
    // is unambiguous: a `def` declaration head. Everywhere else, a
    // capitalized dotted name in source is deferred to parseUnary's postfix
    // loop, because there `Type.member` might be a fact-fluent method whose
    // receiver evaluates to a runtime value - a decision this function has
    // no way to make. A declaration head is never that: `def Logic.negate(`
    // always declares a method on `Logic`, the same shape parseClassStatement
    // already synthesizes for class methods.
    QualifiedName consumeQualifiedName(bool allowNamespaceSeparators = true,
                                       bool allowDottedName = true,
                                       bool allowCapitalizedDotted = false);
    Call parseCall();
    std::shared_ptr<Goal> parseGoal();
    std::vector<std::shared_ptr<Goal>> parseGoalList(TokenId::Id terminator);
    std::shared_ptr<Statement> parseStatement();
    std::shared_ptr<ClassStmt> parseClassStatement(std::size_t begin);
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
    bool startsOwnLine(std::size_t offset) const;
    void consumeStatementTerminator(std::size_t statementBegin);
    // Every parse error's location suffix, e.g. " at source byte 42 (found
    // 'end')". Quotes the user's own source text at the offending offset
    // rather than a raw token/piece ID - the only way to describe an ID that
    // is correct for every ID range at once (fixed grammar, reserved words,
    // and the byte-level pieces that make up an identifier) without a second
    // ID-to-text table that would only have to agree with the lexer's own.
    std::string describeLocation(std::size_t offset) const;
    SourceSpan span(std::size_t begin, std::size_t end) const;
    void stamp(const std::shared_ptr<AstNode>& node, std::size_t begin, std::size_t end) const;
};

} // namespace Felidae
