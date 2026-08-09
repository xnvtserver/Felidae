#pragma once

#include "Symbol.h"
#include "FelidaeSentencePieceIds.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Felidae {

inline constexpr bool isCustomOperatorCharacter(char value) {
    switch (value) {
        case '~':
        case '^':
        case '&':
        case '$':
            return true;
        default:
            return false;
    }
}

inline bool isCustomOperatorSpelling(std::string_view spelling) {
    return !spelling.empty() && std::all_of(
        spelling.begin(), spelling.end(), [](char value) {
            return isCustomOperatorCharacter(value);
        });
}

enum class BuiltinId {
    Unknown = 0,

    Throw,
    Type,
    Instanceof,

    Count,
    Sum,
    Average,
    Min,
    Max,
    Sort,
    Search,
    Contains,
    Lower,
    Upper,
    Length,
    ParseDoc,

    StrLen,
    StrContains,
    StrConcat,
    StrJoin,
    StrLower,
    StrUpper,
    StrTrim,
    StrSplit,
    StrReplace,
    StrStartsWith,
    StrEndsWith,

    ArrayGet,
    ArrayLen,
    ArrayPush,

    FnArray,
    FnPair,
    FnTuple,

    PairFirst,
    PairSecond,

    ConsoleReadLine,
    ConsoleInput,
    ConsoleInputNumber,
    ConsoleWriteLine,
    ConsoleWrite,
    SystemPrint,
    SystemPrintf,
    SystemRun,

    FileReadFile,
    FileReadLines,
    FileReadLine,
    FileWriteFile,
    FileWriteLines,
    FileAppendFile,
    FileExists,
    FileDeleteFile,

    FactAll,
    FactFind,
    FactCount,
    FactFirst,
    FactTypes,
    FactFields,
    FactExists,
    FactSelect,
    FactMaterialize,
    FactRelease,
    FactTimeline,
    FactReferences,
    DbSync,

    // Fact reasoning is language-native.  These operate directly on typed
    // fact values; FactMemory remains an implementation detail.
    CommonAncestors,
    LowestCommonAncestor,
    HighestCommonAncestor,
    AncestorAnalysis,
    PropagateFact,

    RelationCompare,
    RelationFind,
    DependencySatisfied,

    JsonObject,
    JsonParse,
    JsonGet,
    JsonHas,
    JsonKeys,
    JsonSet,
    JsonRemove,
    JsonToText,

    VisualizeGraphJson,

    ThreadCreateThread,
    ThreadStart,
    ThreadPause,
    ThreadStop,
    ThreadStatus,
    ThreadResult,

    MathPi,
    MathE,
    MathRandom,
    MathPow,
    MathAtan2,
    MathSqrt,
    MathSin,
    MathCos,
    MathTan,
    MathAsin,
    MathAcos,
    MathAtan,
    MathLog,
    MathLog10,
    MathExp,
    MathAbs,
    MathFloor,
    MathCeil,
    MathRound,
    MathAdd,
    MathSub,
    MathMul,
    MathDiv,
    MathMod,

    ProbabilityMean,
    ProbabilityVariance,
    ProbabilityStddev,
    ProbabilityNormalize,
    ProbabilityEntropy,
    ProbabilityCovariance,
    ProbabilityCorrelation,
    ProbabilityBernoulli,
    ProbabilityBinomialPmf,
    ProbabilityBinomialCdf,
    ProbabilityPoissonPmf,
    ProbabilityPoissonCdf,
    ProbabilityNormalPdf,
    ProbabilityNormalCdf,
    ProbabilityUniformPdf,
    ProbabilityUniformCdf,
    ProbabilitySample,
    ProbabilityWeightedChoice,

    ReasoningContrary,
    ReasoningProve,
    ReasoningGrade,
    ReasoningDecide,

    MlSigmoid,
    MlRelu,
    MlDot,
    MlMeanSquaredError,

    OverloadAnnotation,
    MatcherAnnotation,
    MixfixAnnotation,

    Last = MixfixAnnotation
};

enum class LanguageTypeId {
    Unknown = 0,
    Any,
    Array,
    Bool,
    Boolean,
    Decimal,
    Double,
    Fact,
    Float,
    Int,
    Number,
    String,
    Expr,
    Mixfix,
    Stmt,
    Statements
};

inline constexpr std::string_view languageTypeName(LanguageTypeId type) {
    switch (type) {
        case LanguageTypeId::Any: return "any";
        case LanguageTypeId::Array: return "array";
        case LanguageTypeId::Bool: return "bool";
        case LanguageTypeId::Boolean: return "boolean";
        case LanguageTypeId::Decimal: return "decimal";
        case LanguageTypeId::Double: return "double";
        case LanguageTypeId::Fact: return "Fact";
        case LanguageTypeId::Float: return "float";
        case LanguageTypeId::Int: return "int";
        case LanguageTypeId::Number: return "number";
        case LanguageTypeId::String: return "string";
        case LanguageTypeId::Expr: return "expr";
        case LanguageTypeId::Mixfix: return "mixfix";
        case LanguageTypeId::Stmt: return "stmt";
        case LanguageTypeId::Statements: return "stmts";
        case LanguageTypeId::Unknown: return {};
    }
    return {};
}

inline LanguageTypeId languageTypeIdForName(const std::string& name) {
    if (name == "any") return LanguageTypeId::Any;
    if (name == "array") return LanguageTypeId::Array;
    if (name == "bool") return LanguageTypeId::Bool;
    if (name == "boolean") return LanguageTypeId::Boolean;
    if (name == "decimal") return LanguageTypeId::Decimal;
    if (name == "double") return LanguageTypeId::Double;
    if (name == "Fact") return LanguageTypeId::Fact;
    if (name == "float") return LanguageTypeId::Float;
    if (name == "int") return LanguageTypeId::Int;
    if (name == "number") return LanguageTypeId::Number;
    if (name == "string") return LanguageTypeId::String;
    if (name == "expr") return LanguageTypeId::Expr;
    if (name == "mixfix") return LanguageTypeId::Mixfix;
    if (name == "stmt") return LanguageTypeId::Stmt;
    if (name == "stmts") return LanguageTypeId::Statements;
    return LanguageTypeId::Unknown;
}

inline bool isFelidaeBuiltinTypeName(const std::string& name) {
    return languageTypeIdForName(name) != LanguageTypeId::Unknown;
}

inline bool isFelidaeTypeAnnotationName(const std::string& name) {
    return !name.empty() &&
           (std::isupper(static_cast<unsigned char>(name.front())) ||
            isFelidaeBuiltinTypeName(name));
}

inline bool isFelidaeLikelyTypeName(const std::string& name) {
    return isFelidaeBuiltinTypeName(name) ||
           (!name.empty() && std::isupper(static_cast<unsigned char>(name.front())));
}

enum class TokenType {
    End,
    Ident,
    String,
    Number,
    Newline,
    BuiltinFunction,
    CustomOperator,

    Import,
    Not,
    And,
    Or,
    Then,
    If,
    Else,
    Return,
    Where,
    Extend,
    Lambda,
    True,
    False,
    Nil,

    LParen,
    RParen,
    LBrace,
    RBrace,
    LBracket,
    RBracket,
    Comma,
    Colon,
    Dot,
    Pipe,
    Question,
    At,

    Bind,       // :=
    DoubleColon,// ::
    Arrow,      // =>
    Plus,       // +
    Minus,      // -
    Star,       // *
    Slash,      // /
    Percent,    // %
    EqEq,       // ==
    NotEq,      // !=
    LT,         // <
    LTE,        // <=
    GT,         // >
    GTE         // >=
};

// This is the single, authoritative spelling table for fixed Felidae syntax.
// SentencePiece registration, validation, the lexer and mixfix compilation
// deliberately all consume this table rather than maintaining local switches.
struct BuiltinTokenDefinition {
    std::string_view spelling;
    std::string_view idName;
    TokenType type;
};

inline constexpr BuiltinTokenDefinition kBuiltinTokens[] = {
    {"import", "IMPORT", TokenType::Import}, {"not", "NOT", TokenType::Not}, {"and", "AND", TokenType::And},
    {"or", "OR", TokenType::Or}, {"then", "THEN", TokenType::Then}, {"if", "IF", TokenType::If},
    {"else", "ELSE", TokenType::Else}, {"return", "RETURN", TokenType::Return}, {"where", "WHERE", TokenType::Where},
    {"extend", "EXTEND", TokenType::Extend}, {"lambda", "LAMBDA", TokenType::Lambda}, {"true", "TRUE", TokenType::True},
    {"false", "FALSE", TokenType::False}, {"nil", "NIL", TokenType::Nil},
    {"(", "LPAREN", TokenType::LParen}, {")", "RPAREN", TokenType::RParen}, {"{", "LBRACE", TokenType::LBrace},
    {"}", "RBRACE", TokenType::RBrace}, {"[", "LBRACKET", TokenType::LBracket}, {"]", "RBRACKET", TokenType::RBracket},
    {",", "COMMA", TokenType::Comma}, {":", "COLON", TokenType::Colon}, {".", "DOT", TokenType::Dot},
    {"|", "PIPE", TokenType::Pipe}, {"?", "QUESTION", TokenType::Question}, {"@", "AT", TokenType::At},
    {":=", "ASSIGN", TokenType::Bind}, {"::", "DOUBLE_COLON", TokenType::DoubleColon}, {"=>", "ARROW", TokenType::Arrow},
    {"+", "PLUS", TokenType::Plus}, {"-", "MINUS", TokenType::Minus}, {"*", "STAR", TokenType::Star},
    {"/", "SLASH", TokenType::Slash}, {"%", "PERCENT", TokenType::Percent}, {"==", "EQUAL", TokenType::EqEq},
    {"!=", "NOT_EQUAL", TokenType::NotEq}, {"<", "LESS", TokenType::LT}, {"<=", "LESS_EQUAL", TokenType::LTE},
    {">", "GREATER", TokenType::GT}, {">=", "GREATER_EQUAL", TokenType::GTE},
};

static_assert(std::size(kBuiltinTokens) == std::size(kFelidaeBuiltinSentencePieceIds),
              "Regenerate FelidaeSentencePieceIds.h after changing built-in syntax");

inline constexpr const BuiltinTokenDefinition* builtinTokenForSpelling(std::string_view spelling) {
    for (const auto& token : kBuiltinTokens) {
        if (token.spelling == spelling) return &token;
    }
    return nullptr;
}

struct Token {
    Token() = default;
    Token(TokenType type,
          std::string text,
          int line,
          int column,
          BuiltinId builtinId = BuiltinId::Unknown,
          LanguageTypeId languageTypeId = LanguageTypeId::Unknown)
        : type(type), text(std::move(text)),
          symbolId((type == TokenType::Ident || type == TokenType::CustomOperator)
                       ? symbolIdForName(this->text)
                       : 0),
          builtinId(builtinId), languageTypeId(languageTypeId), line(line), column(column) {}
    Token(TokenType type,
          std::string text,
          SymbolId symbolId,
          int line,
          int column,
          BuiltinId builtinId = BuiltinId::Unknown,
          LanguageTypeId languageTypeId = LanguageTypeId::Unknown)
        : type(type), text(std::move(text)), symbolId(symbolId), builtinId(builtinId),
          languageTypeId(languageTypeId), line(line), column(column) {}

    TokenType type;
    std::string text;
    SymbolId symbolId = 0;
    BuiltinId builtinId = BuiltinId::Unknown;
    LanguageTypeId languageTypeId = LanguageTypeId::Unknown;
    // A non-zero value identifies an annotation-defined anchor. The token
    // remains an identifier so declarations and field access stay unchanged.
    // Ephemeral lexical evidence. These IDs are model-specific and are never
    // used by the runtime; identifiers still receive SymbolIds above.
    std::vector<int> pieceIds;
    int line = 1;
    int column = 1;
};

inline std::string tokenTypeName(TokenType type) {
    switch (type) {
        case TokenType::End: return "End";
        case TokenType::Ident: return "Ident";
        case TokenType::String: return "String";
        case TokenType::Number: return "Number";
        case TokenType::Newline: return "Newline";
        case TokenType::BuiltinFunction: return "BuiltinFunction";
        case TokenType::CustomOperator: return "CustomOperator";
        case TokenType::Import: return "Import";
        case TokenType::Not: return "not";
        case TokenType::And: return "and";
        case TokenType::Or: return "or";
        case TokenType::Then: return "then";
        case TokenType::If: return "if";
        case TokenType::Else: return "else";
        case TokenType::Return: return "return";
        case TokenType::Where: return "where";
        case TokenType::Extend: return "extend";
        case TokenType::Lambda: return "lambda";
        case TokenType::True: return "true";
        case TokenType::False: return "false";
        case TokenType::Nil: return "nil";
        case TokenType::LParen: return "(";
        case TokenType::RParen: return ")";
        case TokenType::LBrace: return "{";
        case TokenType::RBrace: return "}";
        case TokenType::LBracket: return "[";
        case TokenType::RBracket: return "]";
        case TokenType::Comma: return ",";
        case TokenType::Colon: return ":";
        case TokenType::Dot: return ".";
        case TokenType::Pipe: return "|";
        case TokenType::Question: return "?";
        case TokenType::At: return "@";
        case TokenType::Bind: return ":=";
        case TokenType::DoubleColon: return "::";
        case TokenType::Arrow: return "=>";
        case TokenType::Plus: return "+";
        case TokenType::Minus: return "-";
        case TokenType::Star: return "*";
        case TokenType::Slash: return "/";
        case TokenType::Percent: return "%";
        case TokenType::EqEq: return "==";
        case TokenType::NotEq: return "!=";
        case TokenType::LT: return "<";
        case TokenType::LTE: return "<=";
        case TokenType::GT: return ">";
        case TokenType::GTE: return ">=";
    }
    return "Unknown";
}

} // namespace Felidae
