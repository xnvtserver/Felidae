#pragma once

#include "Token.h"
#include <deque>
#include <istream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace Felidae {

class LexerError : public std::runtime_error {
public:
    explicit LexerError(const std::string& msg) : std::runtime_error(msg) {}
};

class Lexer {
public:
    explicit Lexer(std::string source);
    explicit Lexer(std::istream& input) : input_(&input) {}

    Token nextToken();
    std::vector<Token> tokenize();

private:
    std::unique_ptr<std::istringstream> ownedInput_;
    std::istream* input_ = nullptr;
    std::deque<char> chars_;
    std::vector<char> readBuffer_ = std::vector<char>(64 * 1024);
    bool inputEnded_ = false;
    bool endEmitted_ = false;
    int nestingDepth_ = 0;
    bool emittedLogicalNewline_ = false;
    int line_ = 1;
    int col_ = 1;

    bool ensureChar(size_t offset);
    bool isAtEnd();
    char peek(size_t offset = 0);
    char peekNext();
    char advance();
    bool match(char expected);

    void skipWhitespaceAndComments();
    void consumePhysicalNewline();
    Token readIdentifier();
    Token readNumber();
    Token readString();
    void attachPieceIds(Token& token, std::string_view source) const;
};

} // namespace Felidae
