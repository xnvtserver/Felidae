#include "Lexer.h"
#include "BuiltinRegistry.h"
#include <cctype>
#include <sstream>

namespace Felidae {

Lexer::Lexer(std::string source)
    : ownedInput_(std::make_unique<std::istringstream>(std::move(source))),
      input_(ownedInput_.get()) {}

bool Lexer::ensureChar(size_t offset) {
    while (chars_.size() <= offset && !inputEnded_) {
        input_->read(readBuffer_.data(), static_cast<std::streamsize>(readBuffer_.size()));
        const std::streamsize count = input_->gcount();
        if (count <= 0) {
            inputEnded_ = true;
            break;
        }
        chars_.insert(chars_.end(), readBuffer_.data(), readBuffer_.data() + count);
        if (count < static_cast<std::streamsize>(readBuffer_.size())) inputEnded_ = true;
    }
    return chars_.size() > offset;
}

bool Lexer::isAtEnd() {
    return !ensureChar(0);
}

char Lexer::peek(size_t offset) {
    return ensureChar(offset) ? chars_[offset] : '\0';
}

char Lexer::peekNext() {
    return peek(1);
}

char Lexer::advance() {
    if (!ensureChar(0)) return '\0';
    char c = chars_.front();
    chars_.pop_front();
    if (c == '\n') {
        line_++;
        col_ = 1;
    } else {
        col_++;
    }
    return c;
}

bool Lexer::match(char expected) {
    if (isAtEnd() || peek() != expected) return false;
    advance();
    return true;
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
    const BuiltinId simpleBuiltinId = builtinIdForName(text);
    if (simpleBuiltinId != BuiltinId::Unknown) {
        return Token{TokenType::BuiltinFunction, text, startLine, startCol, simpleBuiltinId};
    }
    // Fixed language words are entirely represented by TokenType.  Retaining
    // their spelling in every token adds allocations/copies without helping
    // parsing or runtime dispatch; only user identifiers and literals keep
    // source text.
    if (text == "import") return Token{TokenType::Import, {}, startLine, startCol};
    if (text == "not") return Token{TokenType::Not, {}, startLine, startCol};
    if (text == "and") return Token{TokenType::And, {}, startLine, startCol};
    if (text == "or") return Token{TokenType::Or, {}, startLine, startCol};
    if (text == "then") return Token{TokenType::Then, {}, startLine, startCol};
    if (text == "if") return Token{TokenType::If, {}, startLine, startCol};
    if (text == "else") return Token{TokenType::Else, {}, startLine, startCol};
    if (text == "return") return Token{TokenType::Return, {}, startLine, startCol};
    if (text == "where") return Token{TokenType::Where, {}, startLine, startCol};
    if (text == "extend") return Token{TokenType::Extend, {}, startLine, startCol};
    if (text == "lambda") return Token{TokenType::Lambda, {}, startLine, startCol};
    if (text == "true") return Token{TokenType::True, {}, startLine, startCol};
    if (text == "false") return Token{TokenType::False, {}, startLine, startCol};
    if (text == "nil") return Token{TokenType::Nil, {}, startLine, startCol};
    Token token{TokenType::Ident, text, startLine, startCol,
                BuiltinId::Unknown, languageTypeIdForName(text)};
    const auto virtualToken = virtualTokens_.find(token.symbolId);
    if (virtualToken != virtualTokens_.end()) token.virtualTokenId = virtualToken->second;
    return token;
}

void Lexer::registerVirtualToken(VirtualTokenDefinition token) {
    if (token.symbolId != 0 && token.tokenId != 0) {
        virtualTokens_.emplace(token.symbolId, token.tokenId);
    }
}

void Lexer::registerVirtualTokens(const std::vector<VirtualTokenDefinition>& tokens) {
    for (const auto& token : tokens) registerVirtualToken(token);
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
                case 'b': text.push_back('\b'); break;
                case 'f': text.push_back('\f'); break;
                case 'v': text.push_back('\v'); break;
                case '0': text.push_back('\0'); break;
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

Token Lexer::nextToken() {
    if (endEmitted_) return Token{TokenType::End, "", line_, col_};
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
            if (nestingDepth_ == 0 && !emittedLogicalNewline_) {
                emittedLogicalNewline_ = true;
                return Token{TokenType::Newline, "\n", startLine, startCol};
            }
            continue;
        }

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            emittedLogicalNewline_ = false;
            return readIdentifier();
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            emittedLogicalNewline_ = false;
            return readNumber();
        }
        if (c == '"') {
            emittedLogicalNewline_ = false;
            return readString();
        }

        advance();
        TokenType type = TokenType::End;
        std::string text;
        switch (c) {
            case '(':
                type = TokenType::LParen;
                nestingDepth_++;
                break;
            case ')':
                type = TokenType::RParen;
                if (nestingDepth_ > 0) nestingDepth_--;
                break;
            case '{':
                type = TokenType::LBrace;
                nestingDepth_++;
                break;
            case '}':
                type = TokenType::RBrace;
                if (nestingDepth_ > 0) nestingDepth_--;
                break;
            case '[':
                type = TokenType::LBracket;
                nestingDepth_++;
                break;
            case ']':
                type = TokenType::RBracket;
                if (nestingDepth_ > 0) nestingDepth_--;
                break;
            case ',': type = TokenType::Comma; break;
            case ':':
                if (match('=')) { type = TokenType::Bind; }
                else if (match(':')) { type = TokenType::DoubleColon; }
                else type = TokenType::Colon;
                break;
            case '+': type = TokenType::Plus; break;
            case '-': type = TokenType::Minus; break;
            case '*': type = TokenType::Star; break;
            case '/': type = TokenType::Slash; break;
            case '%': type = TokenType::Percent; break;
            case '.': type = TokenType::Dot; break;
            case '|': type = TokenType::Pipe; break;
            case '@': type = TokenType::At; break;
            case '?': type = TokenType::Question; break;
            case '=':
                if (match('>')) { type = TokenType::Arrow; }
                else if (match('=')) { type = TokenType::EqEq; }
                else throw LexerError("Unexpected '='. Did you mean '=>' or '=='?");
                break;
            case '!':
                if (match('=')) { type = TokenType::NotEq; }
                else throw LexerError("Unexpected '!'. Did you mean '!='?");
                break;
            case '<':
                if (match('=')) { type = TokenType::LTE; }
                else type = TokenType::LT;
                break;
            case '>':
                if (match('=')) { type = TokenType::GTE; }
                else type = TokenType::GT;
                break;
            default: {
                if (isCustomOperatorCharacter(c)) {
                    type = TokenType::CustomOperator;
                    text.push_back(c);
                    while (!isAtEnd() && isCustomOperatorCharacter(peek())) {
                        text.push_back(advance());
                    }
                    break;
                }
                std::ostringstream oss;
                oss << "Unexpected character '" << c << "' at " << startLine << ":" << startCol;
                throw LexerError(oss.str());
            }
        }
        emittedLogicalNewline_ = false;
        return Token{type, std::move(text), startLine, startCol};
    }
    endEmitted_ = true;
    return Token{TokenType::End, "", line_, col_};
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> out;
    while (true) {
        Token token = nextToken();
        const bool end = token.type == TokenType::End;
        out.push_back(std::move(token));
        if (end) break;
    }
    return out;
}

} // namespace Felidae
