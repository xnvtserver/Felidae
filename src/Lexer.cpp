#include "Lexer.h"
#include "BuiltinRegistry.h"
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
        if (c == ' ' || c == '\t' || c == '\f' || c == '\v') {
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

void Lexer::consumePhysicalNewline() {
    if (peek() == '\r') {
        advance();
        if (!isAtEnd() && peek() == '\n') advance();
        return;
    }
    if (peek() == '\n') advance();
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
    size_t scan = pos_;
    std::string qualified = text;
    while (scan + 1 < source_.size() && (source_[scan] == '.' || source_[scan] == ':') &&
           (std::isalpha(static_cast<unsigned char>(source_[scan + 1])) || source_[scan + 1] == '_')) {
        size_t partStart = scan + 1;
        size_t partEnd = partStart;
        while (partEnd < source_.size() &&
               (std::isalnum(static_cast<unsigned char>(source_[partEnd])) || source_[partEnd] == '_')) {
            partEnd++;
        }
        qualified += ":" + source_.substr(partStart, partEnd - partStart);
        scan = partEnd;
    }
    if (qualified != text) {
        const BuiltinId builtinId = builtinIdForName(qualified);
        if (builtinId != BuiltinId::Unknown) {
            while (pos_ < scan) advance();
            return Token{TokenType::BuiltinFunction, qualified, startLine, startCol, builtinId};
        }
    }
    const BuiltinId simpleBuiltinId = builtinIdForName(text);
    if (simpleBuiltinId != BuiltinId::Unknown) {
        return Token{TokenType::BuiltinFunction, text, startLine, startCol, simpleBuiltinId};
    }
    if (text == "import") return Token{TokenType::Import, text, startLine, startCol};
    if (text == "then") return Token{TokenType::Then, text, startLine, startCol};
    if (text == "if") return Token{TokenType::If, text, startLine, startCol};
    if (text == "else") return Token{TokenType::Else, text, startLine, startCol};
    if (text == "return") return Token{TokenType::Return, text, startLine, startCol};
    if (text == "where") return Token{TokenType::Where, text, startLine, startCol};
    if (text == "extend") return Token{TokenType::Extend, text, startLine, startCol};
    if (text == "lambda") return Token{TokenType::Lambda, text, startLine, startCol};
    if (text == "true") return Token{TokenType::True, text, startLine, startCol};
    if (text == "false") return Token{TokenType::False, text, startLine, startCol};
    if (text == "nil") return Token{TokenType::Nil, text, startLine, startCol};
    return Token{TokenType::Ident, text, startLine, startCol, BuiltinId::Unknown, languageTypeIdForName(text)};
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
    int nestingDepth = 0;
    bool emittedLogicalNewline = false;
    while (!isAtEnd()) {
        skipWhitespaceAndComments();
        if (isAtEnd()) break;

        int startLine = line_;
        int startCol = col_;
        char c = peek();

        if (c == '\\' && (peekNext() == '\n' || peekNext() == '\r')) {
            advance();
            consumePhysicalNewline();
            continue;
        }
        if (c == '\n' || c == '\r') {
            consumePhysicalNewline();
            if (nestingDepth == 0 && !emittedLogicalNewline) {
                add(TokenType::Newline, "\n", out, startLine, startCol);
                emittedLogicalNewline = true;
            }
            continue;
        }

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            out.push_back(readIdentifier());
            emittedLogicalNewline = false;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            out.push_back(readNumber());
            emittedLogicalNewline = false;
            continue;
        }
        if (c == '"') {
            out.push_back(readString());
            emittedLogicalNewline = false;
            continue;
        }

        advance();
        switch (c) {
            case '(':
                add(TokenType::LParen, "(", out, startLine, startCol);
                nestingDepth++;
                break;
            case ')':
                add(TokenType::RParen, ")", out, startLine, startCol);
                if (nestingDepth > 0) nestingDepth--;
                break;
            case '{':
                add(TokenType::LBrace, "{", out, startLine, startCol);
                nestingDepth++;
                break;
            case '}':
                add(TokenType::RBrace, "}", out, startLine, startCol);
                if (nestingDepth > 0) nestingDepth--;
                break;
            case '[':
                add(TokenType::LBracket, "[", out, startLine, startCol);
                nestingDepth++;
                break;
            case ']':
                add(TokenType::RBracket, "]", out, startLine, startCol);
                if (nestingDepth > 0) nestingDepth--;
                break;
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
        if (c != '\n' && c != '\r') emittedLogicalNewline = false;
    }
    out.push_back(Token{TokenType::End, "", line_, col_});
    return out;
}

} // namespace Felidae
