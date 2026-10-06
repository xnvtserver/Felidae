#pragma once

#include "AST.h"
#include "IntegerTokenList.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

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

class IntegerParserIncomplete : public IntegerParserError {
public:
    explicit IntegerParserIncomplete(const std::string& message)
        : IntegerParserError(message) {}
};

// Parser-owned callable metadata shared by every source in one import graph.
// A whitespace application is legal only when a name has exactly one fixed
// arity. Parenthesized calls do not depend on this registry.
class CallSignatureRegistry {
public:
    void registerFixed(std::string name, std::size_t arity);
    std::optional<std::size_t> uniqueFixedArity(const std::string& name) const;
    bool hasAmbiguousFixedArities(const std::string& name) const;

private:
    std::unordered_map<std::string, std::unordered_set<std::size_t>> fixedArities_;
};

// Direct word-vocabulary-token-ID grammar assembler. It has no secondary
// tokenizer, source-character syntax scanner, or spelling-to-token lookup
// table. IDs determine all syntax; original source is retained only to copy
// an already bounded identifier payload into the IR for SymbolId interning.
class IntegerParser {
public:
    explicit IntegerParser(const IntegerTokenList& input,
                           std::shared_ptr<OperatorRegistry> operators = {},
                           std::shared_ptr<CallSignatureRegistry> signatures = {});

    // A project manifest (init.fx) accepts configuration entry calls such as
    // db.location(...). It never changes identifier casing semantics.
    void setManifestMode(bool enabled) noexcept { manifestMode_ = enabled; }

    Program parseProgram();
    bool programComplete();
    std::shared_ptr<Statement> parseNextProgramStatement();
    std::shared_ptr<Expr> parseExpressionText();
    std::shared_ptr<Expr> parseTerminatedExpressionText();
    bool emptyInput();
    bool startsDeclaration();
    const IntegerParserMetrics& metrics() const noexcept { return metrics_; }

    // Something suspicious that is still valid Felidae. Collected while parsing
    // and reported only by --check-json; a run never prints them.
    struct Warning {
        std::string message;
        SourceSpan span;
    };
    const std::vector<Warning>& warnings() const noexcept { return warnings_; }

private:
    struct QualifiedName {
        std::string spelling;
        SymbolId nameId = 0;
        BuiltinId builtinId = BuiltinId::Unknown;
    };
    const IntegerTokenList& input_;
    bool manifestMode_ = false;
    std::size_t statementIterations_ = 0;
    std::shared_ptr<OperatorRegistry> operators_;
    std::shared_ptr<CallSignatureRegistry> signatures_;
    std::size_t piece_ = 0;
    std::size_t byte_ = 0;
    std::size_t recursionDepth_ = 0;
    bool lastClauseUsedBlockEnd_ = false;
    bool insideClassMethod_ = false;
    bool insideQuery_ = false;
    bool parsingDeclarationHead_ = false;
    bool parsingFactPattern_ = false;
    std::unordered_set<SymbolId> localBindings_;
    std::unordered_set<SymbolId> globalBindings_;

    // Singleton variables. A name a fact pattern introduces
    // (`def Person(name: who, age: a)` binds `a`) that is mentioned nowhere else
    // in its function is almost always a typo for a name that was meant to be
    // shared, and it silently matches every fact. Counted per function body.
    struct PatternVariable {
        std::string name;
        SymbolId id = 0;
        SourceSpan span;
    };
    bool insideMethodBody_ = false;
    std::unordered_map<SymbolId, std::size_t> variableMentions_;
    std::vector<PatternVariable> patternVariables_;
    std::vector<Warning> warnings_;
    IntegerParserMetrics metrics_;

    // Kept below native Windows stack exhaustion; malformed/deep source must
    // report a parser error rather than terminating the process.
    static constexpr std::size_t kMaximumRecursionDepth = 128;
    // Per top-level statement; the cumulative count is metrics_.iterations.
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
    bool atAdjacentDot();
    bool atBlockEnd();
    bool matchBlockEnd();
    void requireBlockEnd(const char* message, std::size_t blockBegin);
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
    std::vector<Arg> parseWhitespaceArguments(std::size_t arity);
    void indexCallableSignatures();
    TypeRef parseTypeReference();
    QualifiedName consumeQualifiedName(bool allowDottedName = true);
    Call parseCall();
    std::shared_ptr<Goal> parseGoal();
    std::vector<std::shared_ptr<Goal>> parseBlockBody();
    std::vector<std::shared_ptr<Goal>> parseGoalList(TokenId::Id terminator);
    std::shared_ptr<Statement> parseStatement();
    std::shared_ptr<ClassStmt> parseClassStatement(std::size_t begin);
    Call parseAnnotation();
    void prepareOperatorAnnotation(const Call& annotation);
    const OperatorPatternDefinition& registerOperatorPattern(OperatorPatternDefinition pattern);
    std::string consumeNameRange();
    std::string consumeString();
    std::string consumeAtom();
    double consumeNumber();
    bool atNameRange();
    void consumeStatementTerminator(const char* construct);
    std::string sourceLocation(std::size_t offset) const;
    SourceSpan span(std::size_t begin, std::size_t end) const;
    void stamp(const std::shared_ptr<AstNode>& node, std::size_t begin, std::size_t end) const;
};

} // namespace Felidae
