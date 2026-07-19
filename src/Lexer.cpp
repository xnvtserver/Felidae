#include "Lexer.h"
#include <cctype>
#include <sstream>

namespace Felidae {

bool Lexer::isAtEnd() const {
    return pos_ >= source_.size();
}

char Lexer::peek() const {
    return isAtEnd() ? '\0' : source_[pos_];
}

char Lexer::peekNext() const {
    return (pos_ + 1 >= source_.size()) ? '\0' : source_[pos_ + 1];
}

char Lexer::advance() {
    char c = source_[pos_++];
    if (c == '\n') {
        line_++;
        col_ = 1;
    } else {
        col_++;
    }
    return c;
}

bool Lexer::match(char expected) {
    if (isAtEnd() || source_[pos_] != expected) return false;
    advance();
    return true;
}

void Lexer::add(TokenType type, std::string text, std::vector<Token>& out, int line, int col) {
    out.push_back(Token{type, std::move(text), line, col});
}

void Lexer::skipWhitespaceAndComments() {
    while (!isAtEnd()) {
        char c = peek();
        if (std::isspace(static_cast<unsigned char>(c))) {
            advance();
            continue;
        }
        if (c == '#') {
            while (!isAtEnd() && peek() != '\n') advance();
            continue;
        }
        break;
    }
}

Token Lexer::readIdentifier() {
    int startLine = line_;
    int startCol = col_;
    std::string text;
    while (!isAtEnd()) {
        char c = peek();
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            text.push_back(advance());
        } else {
            break;
        }
    }
    if (text == "import") return Token{TokenType::Import, text, startLine, startCol};
    if (text == "then") return Token{TokenType::Then, text, startLine, startCol};
    return Token{TokenType::Ident, text, startLine, startCol};
}

Token Lexer::readNumber() {
    int startLine = line_;
    int startCol = col_;
    std::string text;
    while (!isAtEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
        text.push_back(advance());
    }
    if (!isAtEnd() && peek() == '.' && std::isdigit(static_cast<unsigned char>(peekNext()))) {
        text.push_back(advance());
        while (!isAtEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
            text.push_back(advance());
        }
    }
    return Token{TokenType::Number, text, startLine, startCol};
}

Token Lexer::readString() {
    int startLine = line_;
    int startCol = col_;
    advance(); // opening quote

    std::string text;
    while (!isAtEnd() && peek() != '"') {
        char c = advance();
        if (c == '\\') {
            if (isAtEnd()) break;
            char esc = advance();
            switch (esc) {
                case 'n': text.push_back('\n'); break;
                case 't': text.push_back('\t'); break;
                case 'r': text.push_back('\r'); break;
                case '"': text.push_back('"'); break;
                case '\\': text.push_back('\\'); break;
                default: text.push_back(esc); break;
            }
        } else {
            text.push_back(c);
        }
    }

    if (isAtEnd()) {
        std::ostringstream oss;
        oss << "Unterminated string at " << startLine << ":" << startCol;
        throw LexerError(oss.str());
    }
    advance(); // closing quote
    return Token{TokenType::String, text, startLine, startCol};
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> out;
    while (!isAtEnd()) {
        skipWhitespaceAndComments();
        if (isAtEnd()) break;

        int startLine = line_;
        int startCol = col_;
        char c = peek();

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            out.push_back(readIdentifier());
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            out.push_back(readNumber());
            continue;
        }
        if (c == '"') {
            out.push_back(readString());
            continue;
        }

        advance();
        switch (c) {
            case '(': add(TokenType::LParen, "(", out, startLine, startCol); break;
            case ')': add(TokenType::RParen, ")", out, startLine, startCol); break;
            case '{': add(TokenType::LBrace, "{", out, startLine, startCol); break;
            case '}': add(TokenType::RBrace, "}", out, startLine, startCol); break;
            case '[': add(TokenType::LBracket, "[", out, startLine, startCol); break;
            case ']': add(TokenType::RBracket, "]", out, startLine, startCol); break;
            case ',': add(TokenType::Comma, ",", out, startLine, startCol); break;
            case ':':
                if (match('=')) add(TokenType::Bind, ":=", out, startLine, startCol);
                else if (match(':')) add(TokenType::DoubleColon, "::", out, startLine, startCol);
                else add(TokenType::Colon, ":", out, startLine, startCol);
                break;
            case '+': add(TokenType::Plus, "+", out, startLine, startCol); break;
            case '-': add(TokenType::Minus, "-", out, startLine, startCol); break;
            case '*': add(TokenType::Star, "*", out, startLine, startCol); break;
            case '/': add(TokenType::Slash, "/", out, startLine, startCol); break;
            case '.': add(TokenType::Dot, ".", out, startLine, startCol); break;
            case '|': add(TokenType::Pipe, "|", out, startLine, startCol); break;
            case '?': add(TokenType::Question, "?", out, startLine, startCol); break;
            case '=':
                if (match('>')) add(TokenType::Arrow, "=>", out, startLine, startCol);
                else if (match('=')) add(TokenType::EqEq, "==", out, startLine, startCol);
                else throw LexerError("Unexpected '='. Did you mean '=>' or '=='?");
                break;
            case '!':
                if (match('=')) add(TokenType::NotEq, "!=", out, startLine, startCol);
                else throw LexerError("Unexpected '!'. Did you mean '!='?");
                break;
            case '<':
                if (match('=')) add(TokenType::LTE, "<=", out, startLine, startCol);
                else add(TokenType::LT, "<", out, startLine, startCol);
                break;
            case '>':
                if (match('=')) add(TokenType::GTE, ">=", out, startLine, startCol);
                else add(TokenType::GT, ">", out, startLine, startCol);
                break;
            default: {
                std::ostringstream oss;
                oss << "Unexpected character '" << c << "' at " << startLine << ":" << startCol;
                throw LexerError(oss.str());
            }
        }
    }
    out.push_back(Token{TokenType::End, "", line_, col_});
    return out;
}

} // namespace Felidae
