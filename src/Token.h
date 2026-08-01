#pragma once

#include "Symbol.h"
#include <cctype>
#include <string>
#include <utility>

namespace Felidae {

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
    FactReferences,
    DbSync,

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

    Last = MatcherAnnotation
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

struct Token {
    Token() = default;
    Token(TokenType type,
          std::string text,
          int line,
          int column,
          BuiltinId builtinId = BuiltinId::Unknown,
          LanguageTypeId languageTypeId = LanguageTypeId::Unknown)
        : type(type), text(std::move(text)), symbolId(type == TokenType::Ident ? symbolIdForName(this->text) : 0),
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
