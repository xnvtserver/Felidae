#pragma once

#include "AST.h"
#include "FelidaeIr.h"
#include "IntegerTokenList.h"

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace Felidae {

class MixfixStateModel;
struct MixfixContext;

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

// Direct SentencePiece-ID parser. It has no secondary tokenizer,
// source-character syntax scanner, or spelling-to-token lookup table. IDs
// determine all syntax; original source is retained only to copy an already
// bounded identifier payload into short-lived compiler symbol interning.
class IntegerParser {
public:
    explicit IntegerParser(const IntegerTokenList& input,
                           std::shared_ptr<OperatorRegistry> operators = {},
                           MixfixStateModel* mixfixModel = nullptr);

    Program parseProgram();
    std::vector<std::shared_ptr<Goal>> parseQuery();
    std::shared_ptr<Expr> parseExpressionText();
    // Transitional direct lowering for the primitive expression subset.  It
    // consumes the existing SentencePiece stream once and returns executable
    // canonical IR without an interpreter invocation.
    FelidaeIr compileExpressionIr();
    // Compiler-front-end seam: AST is permitted before IR, but this method
    // always produces typed canonical IR and never carries an AST into VM
    // registers. Statement/module lowering builds on this same seam.
    static FelidaeIr compileAstExpressionIr(const std::shared_ptr<Expr>& expression,
                                            const std::unordered_set<SymbolId>& factTypes = {},
                                            const std::unordered_map<SymbolId, SymbolId>& factDesignations = {});
    // Initial statement compiler slice. It deliberately reuses expression
    // lowering and emits StoreSymbol; no statement AST survives in the IR.
    static FelidaeIr compileAstGlobalBindingIr(const GlobalBindingStmt& binding,
                                               const std::unordered_set<SymbolId>& factTypes = {},
                                               const std::unordered_map<SymbolId, SymbolId>& factDesignations = {});
    // Initial routine slice for a deterministic zero-argument entry method.
    // More complex goal lists are intentionally rejected until frame/local
    // lowering is available, rather than being handed to an AST executor.
    static FelidaeIr compileAstEntryMethodIr(const ClauseStmt& method,
                                             const std::unordered_set<SymbolId>& factTypes = {},
                                             const std::unordered_map<SymbolId, SymbolId>& factDesignations = {});
    // Compiler-SSM entry point. The caller selects an existing SentencePiece
    // span; this method never retokenizes source text and always verifies the
    // finite-vocabulary model output before returning IR.
    FelidaeIr compileVerifiedMixfixSpanIr(MixfixStateModel& model,
                                          const MixfixContext& context,
                                          FelidaeIr irShell,
                                          std::size_t firstPiece,
                                          std::size_t pastLastPiece) const;
    bool startsQuery();
    const IntegerParserMetrics& metrics() const noexcept { return metrics_; }

private:
    struct QualifiedName {
        std::string spelling;
        SymbolId nameId = 0;
        BuiltinId builtinId = BuiltinId::Unknown;
        bool isCapitalized = false;
    };
    struct StringLiteral {
        std::string value;
        std::vector<std::uint32_t> sentencePieceIds;
        bool containsEscape = false;
    };
    const IntegerTokenList& input_;
    std::shared_ptr<OperatorRegistry> operators_;
    // Optional compiler backend.  Normal grammar is always deterministic;
    // only an assembled custom mixfix expression can reach this model.
    MixfixStateModel* mixfixModel_ = nullptr;
    std::size_t piece_ = 0;
    std::size_t byte_ = 0;
    std::size_t recursionDepth_ = 0;
    IntegerParserMetrics metrics_;
    std::vector<std::size_t> lineStarts_;
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
    std::size_t builtinSequenceLength(TokenId::Id id) const;
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
    void registerOperatorImplementation(const Call& annotation, const ClauseStmt& method);
    const OperatorPatternDefinition& registerOperatorPattern(OperatorPatternDefinition pattern);
    SymbolId resolveMixfixMethod(const OperatorExpression& expression) const;
    SymbolId resolveModelMixfixMethod(const std::shared_ptr<OperatorExpression>& expression) const;
    std::string consumeNameRange();
    StringLiteral consumeString();
    double consumeNumber();
    bool atNameRange();
    bool sourceContainsLineBreak(std::size_t begin, std::size_t end) const;
    bool lineBreakBeforeNextSignificantPiece() const;
    std::size_t sourceLineIndent(std::size_t offset) const;
    void consumeStatementTerminator(std::size_t statementBegin);
    SourceSpan span(std::size_t begin, std::size_t end) const;
    std::pair<int, int> sourcePosition(std::size_t offset) const;
    std::size_t sourceOffset(int line, int column) const;
    void stamp(const std::shared_ptr<AstNode>& node, std::size_t begin, std::size_t end) const;
    FelidaeIr compileModelRoutedMixfixExpressionIr(const std::shared_ptr<Expr>& expression) const;
};

} // namespace Felidae
