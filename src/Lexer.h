#pragma once

#include "Token.h"
#include <stdexcept>
#include <string>
#include <vector>

namespace Felidae {

class LexerError : public std::runtime_error {
public:
    explicit LexerError(const std::string& msg) : std::runtime_error(msg) {}
};

class Lexer {
public:
    explicit Lexer(std::string source) : source_(std::move(source)) {}

    std::vector<Token> tokenize();

private:
    std::string source_;
    size_t pos_ = 0;
    int line_ = 1;
    int col_ = 1;

    bool isAtEnd() const;
    char peek() const;
    char peekNext() const;
    char advance();
    bool match(char expected);

    void add(TokenType type, std::string text, std::vector<Token>& out, int line, int col);
    void skipWhitespaceAndComments();
    Token readIdentifier();
    Token readNumber();
    Token readString();
};

} // namespace Felidae
