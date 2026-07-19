#pragma once

#include <string>

namespace Felidae {

enum class TokenType {
    End,
    Ident,
    String,
    Number,

    Import,
    Then,

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

    Bind,       // :=
    DoubleColon,// ::
    Arrow,      // =>
    Plus,       // +
    Minus,      // -
    Star,       // *
    Slash,      // /
    EqEq,       // ==
    NotEq,      // !=
    LT,         // <
    LTE,        // <=
    GT,         // >
    GTE         // >=
};

struct Token {
    TokenType type;
    std::string text;
    int line = 1;
    int column = 1;
};

inline std::string tokenTypeName(TokenType type) {
    switch (type) {
        case TokenType::End: return "End";
        case TokenType::Ident: return "Ident";
        case TokenType::String: return "String";
        case TokenType::Number: return "Number";
        case TokenType::Import: return "Import";
        case TokenType::Then: return "then";
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
        case TokenType::Bind: return ":=";
        case TokenType::DoubleColon: return "::";
        case TokenType::Arrow: return "=>";
        case TokenType::Plus: return "+";
        case TokenType::Minus: return "-";
        case TokenType::Star: return "*";
        case TokenType::Slash: return "/";
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
