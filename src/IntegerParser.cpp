#include "IntegerParser.h"

#include "BuiltinRegistry.h"
#include "Operator.h"
#include "OperatorAnnotation.h"
#include "Tokenizer.h"

#include <algorithm>
#include <cctype>

namespace Felidae {

namespace {
const SymbolId kMainSymbolId = symbolIdForName("main");

bool hasValueExpression(const std::vector<std::shared_ptr<Goal>>& goals) {
    for (const auto& goal : goals) {
        if (nodeAs<ExpressionGoal>(goal)) return true;
    }
    return false;
}

bool isMethodStyleHead(const Call& head) {
    if (head.args.empty()) return false;
    for (const auto& argument : head.args) {
        const auto type = nodeAs<VarExpr>(argument.value);
        if (argument.name.empty() || !type ||
            (type->languageTypeId == LanguageTypeId::Unknown &&
             !isFelidaeTypeAnnotationName(type->name))) {
            return false;
        }
    }
    return true;
}

ParsedOperatorAnnotation decodeParserAnnotation(const Call& annotation) {
    try {
        return decodeOperatorAnnotation(annotation);
    } catch (const std::runtime_error& error) {
        throw IntegerParserError(error.what());
    }
}

std::optional<std::size_t> fixedBuiltinArity(BuiltinId id) {
    switch (id) {
        case BuiltinId::Type:
        case BuiltinId::IsAtom:
        case BuiltinId::Count:
        case BuiltinId::Sum:
        case BuiltinId::Average:
        case BuiltinId::Min:
        case BuiltinId::Max:
        case BuiltinId::Sort:
        case BuiltinId::Lower:
        case BuiltinId::Upper:
        case BuiltinId::Length:
        case BuiltinId::ParseDoc:
        case BuiltinId::ConsoleWriteLine:
        case BuiltinId::ConsoleWrite:
        case BuiltinId::SystemPrint:
        case BuiltinId::SystemPrintf:
            return 1;
        case BuiltinId::Instanceof:
        case BuiltinId::Contains:
        case BuiltinId::Search:
            return 2;
        case BuiltinId::Throw:
            return 2;
        default:
            return std::nullopt;
    }
}
} // namespace

IntegerParser::IntegerParser(const IntegerTokenList& input,
                             std::shared_ptr<OperatorRegistry> operators,
                             std::shared_ptr<CallSignatureRegistry> signatures)
    : input_(input), operators_(std::move(operators)),
      signatures_(signatures ? std::move(signatures)
                             : std::make_shared<CallSignatureRegistry>()) {
    if (!operators_) operators_ = std::make_shared<OperatorRegistry>();
    metrics_.sourceEncodeCount = input.encodeCount();
    metrics_.tokenCount = input.entries().size();
    indexCallableSignatures();
}

void CallSignatureRegistry::registerFixed(std::string name, std::size_t arity) {
    fixedArities_[std::move(name)].insert(arity);
}

std::optional<std::size_t> CallSignatureRegistry::uniqueFixedArity(
    const std::string& name) const {
    const auto found = fixedArities_.find(name);
    if (found == fixedArities_.end() || found->second.size() != 1) return std::nullopt;
    return *found->second.begin();
}

bool CallSignatureRegistry::hasAmbiguousFixedArities(
    const std::string& name) const {
    const auto found = fixedArities_.find(name);
    return found != fixedArities_.end() && found->second.size() > 1;
}

void IntegerParser::indexCallableSignatures() {
    // This is a token-ID header pass owned by the parser. It does not parse or
    // validate bodies, and therefore cannot become a parallel source grammar.
    // Full validation remains the responsibility of parseStatement().
    const auto& entries = input_.entries();
    const auto trivia = [](TokenId::Id id) {
        return id == TokenId::SPACE || id == TokenId::TAB ||
               id == TokenId::NEWLINE || id == TokenId::CARRIAGE_RETURN ||
               id == TokenId::COMMENT;
    };
    const auto skipTriviaAt = [&](std::size_t& index) {
        while (index < entries.size()) {
            if (entries[index].id == TokenId::COMMENT) {
                ++index;
                while (index < entries.size() && entries[index].id != TokenId::NEWLINE &&
                       entries[index].id != TokenId::CARRIAGE_RETURN) ++index;
                continue;
            }
            if (!trivia(entries[index].id)) break;
            ++index;
        }
    };
    const auto readNameAt = [&](std::size_t& index) -> std::string {
        skipTriviaAt(index);
        if (index >= entries.size() || entries[index].id <= TokenId::UNKNOWN ||
            (isBuiltinTokenId(entries[index].id) && entries[index].id != TokenId::VAR)) return {};
        const std::size_t begin = entries[index].begin;
        std::size_t end = begin;
        const auto readSegment = [&]() {
            if (entries[index].id == TokenId::VAR) {
                end = entries[index++].end;
                return;
            }
            while (index < entries.size()) {
                const auto id = entries[index].id;
                if (id == TokenId::UNKNOWN || isIdentifierBoundaryId(id)) break;
                end = entries[index++].end;
            }
        };
        readSegment();
        while (index + 1 < entries.size() && entries[index].id == TokenId::DOT &&
               entries[index].end == entries[index + 1].begin &&
               entries[index + 1].id > TokenId::UNKNOWN &&
               (!isBuiltinTokenId(entries[index + 1].id) ||
                entries[index + 1].id == TokenId::VAR)) {
            end = entries[index++].end;
            readSegment();
        }
        return input_.source().substr(begin, end - begin);
    };

    enum class IndexedBlock { Class, Callable, Control };
    struct IndexedBlockFrame {
        IndexedBlock kind;
        std::string name;
    };
    std::vector<IndexedBlockFrame> blocks;

    for (std::size_t index = 0; index < entries.size(); ++index) {
        if (entries[index].id == TokenId::COMMENT) {
            while (index + 1 < entries.size() &&
                   entries[index + 1].id != TokenId::NEWLINE &&
                   entries[index + 1].id != TokenId::CARRIAGE_RETURN) ++index;
            continue;
        }
        if (entries[index].id == TokenId::QUOTE ||
            entries[index].id == TokenId::ATOM_QUOTE) {
            const auto quote = entries[index].id;
            while (index + 1 < entries.size() && entries[++index].id != quote) {}
            continue;
        }
        if (entries[index].id == TokenId::END) {
            if (!blocks.empty()) blocks.pop_back();
            continue;
        }
        if (entries[index].id == TokenId::CLASS) {
            std::size_t nameIndex = index + 1;
            std::string className = readNameAt(nameIndex);
            if (!className.empty()) {
                blocks.push_back({IndexedBlock::Class, std::move(className)});
                index = nameIndex - 1;
            }
            continue;
        }
        if (entries[index].id == TokenId::FOR || entries[index].id == TokenId::WHILE ||
            entries[index].id == TokenId::SWITCH || entries[index].id == TokenId::TRY) {
            blocks.push_back({IndexedBlock::Control, {}});
            continue;
        }
        if (entries[index].id != TokenId::DEF) continue;
        ++index;
        std::string name = readNameAt(index);
        if (name.empty()) continue;
        skipTriviaAt(index);
        if (index >= entries.size() || entries[index].id != TokenId::LPAREN) continue;
        ++index;
        std::size_t depth = 1;
        std::size_t typeDepth = 0;
        std::size_t arity = 0;
        bool hasArgument = false;
        while (index < entries.size() && depth != 0) {
            const auto id = entries[index].id;
            if (id == TokenId::COMMENT) {
                skipTriviaAt(index);
                continue;
            }
            if (id == TokenId::QUOTE || id == TokenId::ATOM_QUOTE) {
                const auto quote = id;
                ++index;
                while (index < entries.size() && entries[index].id != quote) ++index;
                if (index < entries.size()) ++index;
                hasArgument = true;
                continue;
            }
            if (id == TokenId::LPAREN || id == TokenId::LBRACKET || id == TokenId::LBRACE) {
                ++depth;
            } else if (id == TokenId::RPAREN || id == TokenId::RBRACKET || id == TokenId::RBRACE) {
                if (--depth == 0) {
                    ++index;
                    break;
                }
            } else if (depth == 1 && id == TokenId::LESS) {
                ++typeDepth;
            } else if (depth == 1 && id == TokenId::GREATER && typeDepth != 0) {
                --typeDepth;
            } else if (depth == 1 && typeDepth == 0 && id == TokenId::COMMA) {
                ++arity;
            } else if (depth == 1 && !trivia(id)) {
                hasArgument = true;
            }
            ++index;
        }
        if (depth != 0) continue;
        skipTriviaAt(index);
        if (index < entries.size() && entries[index].id == TokenId::ARROW) {
            if (!blocks.empty() && blocks.back().kind == IndexedBlock::Class) {
                name = blocks.back().name + "." + name;
            }
            signatures_->registerFixed(std::move(name), hasArgument ? arity + 1 : 0);
            blocks.push_back({IndexedBlock::Callable, {}});
        }
    }
}

IntegerParser::RecursionScope::RecursionScope(IntegerParser& parser) : parser_(parser) {
    ++parser_.recursionDepth_;
    if (parser_.recursionDepth_ > kMaximumRecursionDepth) {
        --parser_.recursionDepth_;
        throw IntegerParserError("Maximum integer parser recursion depth exceeded");
    }
    parser_.metrics_.peakRecursionDepth = std::max(
        parser_.metrics_.peakRecursionDepth, parser_.recursionDepth_);
}

IntegerParser::RecursionScope::~RecursionScope() {
    if (parser_.recursionDepth_ != 0) --parser_.recursionDepth_;
}

void IntegerParser::step() {
    ++metrics_.iterations;
    if (++statementIterations_ > kMaximumIterations) {
        throw IntegerParserError("Integer parser iteration budget exceeded");
    }
}

void IntegerParser::alignPiece() {
    const auto& pieces = input_.entries();
    while (piece_ < pieces.size() && pieces[piece_].end <= byte_) ++piece_;
}

void IntegerParser::skipTrivia() {
    const auto& pieces = input_.entries();
    while (piece_ < pieces.size()) {
        step();
        const auto id = pieces[piece_].id;
        if (id == TokenId::SPACE || id == TokenId::TAB || id == TokenId::NEWLINE ||
            id == TokenId::CARRIAGE_RETURN) {
            byte_ = pieces[piece_++].end;
            continue;
        }
        if (id == TokenId::COMMENT) {
            byte_ = pieces[piece_++].end;
            while (piece_ < pieces.size() && pieces[piece_].id != TokenId::NEWLINE &&
                   pieces[piece_].id != TokenId::CARRIAGE_RETURN) {
                byte_ = pieces[piece_++].end;
            }
            continue;
        }
        break;
    }
    alignPiece();
}

bool IntegerParser::at(TokenId::Id id) {
    skipTrivia();
    const auto& pieces = input_.entries();
    return piece_ < pieces.size() && pieces[piece_].id == id;
}

bool IntegerParser::match(TokenId::Id id) {
    if (!at(id)) return false;
    const auto& entry = input_.entries()[piece_++];
    byte_ = entry.end;
    return true;
}

bool IntegerParser::atAdjacentDot() {
    skipTrivia();
    const auto& pieces = input_.entries();
    if (piece_ + 1 >= pieces.size() || pieces[piece_].id != TokenId::DOT ||
        pieces[piece_].end != pieces[piece_ + 1].begin) {
        return false;
    }
    const auto next = pieces[piece_ + 1].id;
    if (next == TokenId::CLASS) return true;
    if (next > TokenId::UNKNOWN && !isBuiltinTokenId(next)) return true;
    const auto spelling = builtinTokenSpelling(next);
    return !spelling.empty() &&
           std::isalpha(static_cast<unsigned char>(spelling.front())) != 0;
}

bool IntegerParser::atBlockEnd() {
    skipTrivia();
    return piece_ < input_.entries().size() && input_.entries()[piece_].id == TokenId::END;
}

bool IntegerParser::matchBlockEnd() {
    if (!atBlockEnd()) return false;
    const auto end = input_.entries()[piece_++].end;
    byte_ = end;
    if (at(TokenId::DOT)) {
        throw IntegerParserError(
            "Unexpected '.' after 'end'; 'end' terminates the block at " +
            sourceLocation(byte_));
    }
    return true;
}

void IntegerParser::require(TokenId::Id id, const char* message) {
    if (!match(id)) {
        const std::string detail = std::string(message) + " at " +
                                   sourceLocation(byte_);
        if (atEnd()) throw IntegerParserIncomplete(detail);
        throw IntegerParserError(detail);
    }
}

void IntegerParser::requireBlockEnd(const char* message, std::size_t blockBegin) {
    if (matchBlockEnd()) return;
    const std::string detail = std::string(message) + " (block starts at " +
                               sourceLocation(blockBegin) + ")";
    if (atEnd()) throw IntegerParserIncomplete(detail);
    throw IntegerParserError(detail);
}

bool IntegerParser::atEnd() {
    skipTrivia();
    return piece_ >= input_.entries().size();
}

bool IntegerParser::atNameRange() {
    skipTrivia();
    const auto& pieces = input_.entries();
    if (piece_ >= pieces.size()) return false;
    const auto id = pieces[piece_].id;
    return id > TokenId::UNKNOWN &&
           (!isBuiltinTokenId(id) || id == TokenId::VAR);
}

std::string IntegerParser::sourceLocation(std::size_t offset) const {
    offset = std::min(offset, input_.source().size());
    const auto position = input_.lineColumn(offset);
    return "line " + std::to_string(position.line) + ", column " +
           std::to_string(position.column);
}

void IntegerParser::consumeStatementTerminator(const char* construct) {
    if (match(TokenId::DOT)) {
        if (at(TokenId::DOT)) {
            throw IntegerParserError(
                "Consecutive '..' is not valid Felidae syntax at " +
                sourceLocation(byte_));
        }
        return;
    }
    throw IntegerParserError(
        "Expected '.' after " + std::string(construct) + " at " +
        sourceLocation(byte_));
}

std::string IntegerParser::consumeNameRange() {
    skipTrivia();
    if (!atNameRange()) {
        if (atEnd()) throw IntegerParserIncomplete("Expected a token name range");
        const auto id = piece_ < input_.entries().size() ? input_.entries()[piece_].id : TokenId::UNKNOWN;
        throw IntegerParserError("Expected a token name at " +
                                 sourceLocation(byte_) + " (token ID " +
                                 std::to_string(id) + ")");
    }
    const std::size_t begin = byte_;
    const auto& pieces = input_.entries();
    if (pieces[piece_].id == TokenId::VAR) {
        byte_ = pieces[piece_++].end;
        return input_.source().substr(begin, byte_ - begin);
    }
    // A logical name is a contiguous run of non-grammar token IDs.
    // The byte tokenizer represents one source name as adjacent byte tokens.
    while (piece_ < pieces.size()) {
        const auto id = pieces[piece_].id;
        if (id == TokenId::UNKNOWN || isIdentifierBoundaryId(id)) break;
        byte_ = pieces[piece_++].end;
    }
    if (byte_ == begin) throw IntegerParserError("Empty token name range");
    return input_.source().substr(begin, byte_ - begin);
}

std::string IntegerParser::consumeString() {
    require(TokenId::QUOTE, "Expected a string literal");
    const std::size_t begin = byte_;
    while (piece_ < input_.entries().size()) {
        const auto id = input_.entries()[piece_].id;
        if (id == TokenId::QUOTE) {
            const std::size_t end = input_.entries()[piece_].begin;
            byte_ = input_.entries()[piece_++].end;
            // Literal content is a lexer-owned source span; the byte tokenizer supplies IDs
            // only for identifiers and mixfix anchors.
            const std::string value = input_.source().substr(begin, end - begin);
            std::string unescaped;
            unescaped.reserve(value.size());
            for (std::size_t index = 0; index < value.size(); ++index) {
                if (value[index] != '\\' || index + 1 == value.size()) {
                    unescaped.push_back(value[index]);
                    continue;
                }
                const char escaped = value[++index];
                switch (escaped) {
                    case 'n': unescaped.push_back('\n'); break;
                    case 'r': unescaped.push_back('\r'); break;
                    case 't': unescaped.push_back('\t'); break;
                    case '\\': unescaped.push_back('\\'); break;
                    case '"': unescaped.push_back('"'); break;
                    default:
                        unescaped.push_back('\\');
                        unescaped.push_back(escaped);
                        break;
                }
            }
            return unescaped;
        }
        byte_ = input_.entries()[piece_++].end;
    }
    throw IntegerParserIncomplete("Unterminated string literal");
}

std::string IntegerParser::consumeAtom() {
    require(TokenId::ATOM_QUOTE, "Expected an atom literal");
    const std::size_t begin = byte_;
    while (piece_ < input_.entries().size()) {
        if (input_.entries()[piece_].id == TokenId::ATOM_QUOTE) {
            const std::size_t end = input_.entries()[piece_].begin;
            byte_ = input_.entries()[piece_++].end;
            return input_.source().substr(begin, end - begin);
        }
        byte_ = input_.entries()[piece_++].end;
    }
    throw IntegerParserIncomplete("Unterminated atom literal");
}

double IntegerParser::consumeNumber() {
    skipTrivia();
    double value = 0.0;
    bool consumed = false;
    while (piece_ < input_.entries().size() && isDecimalDigitId(input_.entries()[piece_].id)) {
        value = value * 10.0 + static_cast<double>(input_.entries()[piece_].id - TokenId::DIGIT_0);
        byte_ = input_.entries()[piece_++].end;
        consumed = true;
    }
    if (at(TokenId::DOT) && piece_ + 1 < input_.entries().size() &&
        isDecimalDigitId(input_.entries()[piece_ + 1].id)) {
        match(TokenId::DOT);
        double scale = 0.1;
        while (piece_ < input_.entries().size() && isDecimalDigitId(input_.entries()[piece_].id)) {
            value += static_cast<double>(input_.entries()[piece_].id - TokenId::DIGIT_0) * scale;
            scale *= 0.1;
            byte_ = input_.entries()[piece_++].end;
        }
    }
    if (!consumed) throw IntegerParserError("Expected a number literal");
    return value;
}

std::shared_ptr<Expr> IntegerParser::parseArray() {
    const std::size_t begin = byte_;
    require(TokenId::LBRACKET, "Expected '['");
    std::vector<std::shared_ptr<Expr>> items;
    if (!at(TokenId::RBRACKET)) {
        do {
            items.push_back(parseExpression());
        } while (match(TokenId::COMMA));
    }
    require(TokenId::RBRACKET, "Expected ']' after array");
    auto result = std::make_shared<ArrayExpr>(std::move(items));
    stamp(result, begin, byte_);
    return result;
}

std::vector<Arg> IntegerParser::parseArguments(bool allowAnnotationBindings) {
    require(TokenId::LPAREN, "Expected '('");
    std::vector<Arg> arguments;
    bool sawNamed = false;
    std::unordered_set<SymbolId> namedIds;
    if (!at(TokenId::RPAREN)) {
        do {
            skipTrivia();
            const std::size_t before = byte_;
            QualifiedName name;
            bool named = false;
            if (at(TokenId::CLASS)) {
                const auto nameStart = byte_;
                const auto pieceStart = piece_;
                match(TokenId::CLASS);
                if (match(TokenId::COLON)) {
                    name.spelling = "class";
                    name.nameId = symbolIdForName(name.spelling);
                    named = true;
                } else {
                    byte_ = nameStart;
                    piece_ = pieceStart;
                }
            } else if (atNameRange()) {
                const auto nameStart = byte_;
                const auto pieceStart = piece_;
                const auto candidate = consumeQualifiedName();
                if (match(TokenId::COLON)) {
                    name = candidate;
                    named = true;
                }
                else {
                    byte_ = nameStart;
                    piece_ = pieceStart;
                }
            }
            if (!named && sawNamed) {
                throw IntegerParserError(
                    "Positional arguments cannot follow named arguments at " +
                    sourceLocation(before));
            }
            if (named) {
                sawNamed = true;
                if (!namedIds.insert(name.nameId).second) {
                    throw IntegerParserError("Duplicate named argument '" +
                                             name.spelling + "'");
                }
            }
            std::shared_ptr<Expr> value;
            if (allowAnnotationBindings && named && at(TokenId::LBRACKET)) {
                require(TokenId::LBRACKET, "Expected '[' for annotation bindings");
                std::vector<std::shared_ptr<Expr>> bindings;
                if (!at(TokenId::RBRACKET)) {
                    do {
                        const auto binding = consumeQualifiedName();
                        require(TokenId::COLON, "Expected ':' after annotation binding name");
                        const auto type = consumeQualifiedName();
                        auto typeExpr = std::make_shared<VarExpr>(
                            type.spelling, type.nameId, languageTypeIdForName(type.spelling));
                        bindings.push_back(std::make_shared<MapExpr>(std::vector<MapEntry>{
                            MapEntry{binding.spelling, binding.nameId, std::move(typeExpr)}}));
                    } while (match(TokenId::COMMA));
                }
                require(TokenId::RBRACKET, "Expected ']' after annotation bindings");
                value = std::make_shared<ArrayExpr>(std::move(bindings));
            } else if (allowAnnotationBindings && named && atNameRange()) {
                const auto valueByte = byte_;
                const auto valuePiece = piece_;
                const auto binding = consumeQualifiedName();
                if (match(TokenId::COLON)) {
                    const auto type = consumeQualifiedName();
                    auto typeExpr = std::make_shared<VarExpr>(
                        type.spelling, type.nameId, languageTypeIdForName(type.spelling));
                    value = std::make_shared<MapExpr>(std::vector<MapEntry>{
                        MapEntry{binding.spelling, binding.nameId, std::move(typeExpr)}});
                } else {
                    byte_ = valueByte;
                    piece_ = valuePiece;
                }
            }
            if (!value) value = parseExpression();
            if (byte_ == before) throw IntegerParserError("Integer parser made no progress in argument list");
            arguments.emplace_back(named ? std::move(name.spelling) : std::string{},
                                   named ? name.nameId : 0, std::move(value));
        } while (match(TokenId::COMMA));
    }
    require(TokenId::RPAREN, "Expected ')' after arguments");
    return arguments;
}

std::vector<Arg> IntegerParser::parseWhitespaceArguments(std::size_t arity) {
    std::vector<Arg> arguments;
    arguments.reserve(arity);
    bool sawNamed = false;
    std::unordered_set<SymbolId> namedIds;
    for (std::size_t index = 0; index < arity; ++index) {
        if (index != 0) (void)match(TokenId::COMMA);
        skipTrivia();
        if (atEnd() || at(TokenId::DOT) || at(TokenId::RPAREN) ||
            at(TokenId::RBRACKET) || at(TokenId::RBRACE) || at(TokenId::END) ||
            at(TokenId::THEN) || at(TokenId::ELSE)) {
            throw IntegerParserError(
                "Whitespace call requires " + std::to_string(arity) +
                " arguments; found " + std::to_string(index) + " at " +
                sourceLocation(byte_));
        }

        const auto savedByte = byte_;
        const auto savedPiece = piece_;
        QualifiedName argumentName;
        if (atNameRange()) {
            argumentName = consumeQualifiedName(false);
            if (!match(TokenId::COLON)) {
                byte_ = savedByte;
                piece_ = savedPiece;
                argumentName = {};
            }
        }
        const bool named = !argumentName.spelling.empty();
        if (!named && sawNamed) {
            throw IntegerParserError(
                "Positional arguments cannot follow named arguments at " +
                sourceLocation(byte_));
        }
        if (named) {
            sawNamed = true;
            if (!namedIds.insert(argumentName.nameId).second) {
                throw IntegerParserError("Duplicate named argument '" +
                                         argumentName.spelling + "'");
            }
        }
        auto value = parseBinaryExpression(
            static_cast<int>(OperatorPrecedence::Control), TokenId::ELSE);
        arguments.emplace_back(named ? std::move(argumentName.spelling) : std::string{},
                               named ? argumentName.nameId : 0, std::move(value));
    }
    return arguments;
}

IntegerParser::QualifiedName IntegerParser::consumeQualifiedName(
    bool allowDottedName) {
    skipTrivia();
    const auto firstPiece = piece_;
    QualifiedName name{consumeNameRange(), 0, BuiltinId::Unknown};
    while (allowDottedName && atAdjacentDot()) {
        const auto beforeByte = byte_;
        const auto beforePiece = piece_;
        const auto separator = input_.entries()[piece_].id;
        match(separator);
        if (!atNameRange()) {
            byte_ = beforeByte;
            piece_ = beforePiece;
            break;
        }
        name.spelling += ".";
        name.spelling += consumeNameRange();
    }
    std::vector<TokenId::Id> ids;
    ids.reserve(piece_ - firstPiece);
    for (std::size_t index = firstPiece; index < piece_; ++index) {
        const auto id = input_.entries()[index].id;
        if (id != TokenId::SPACE && id != TokenId::TAB &&
            id != TokenId::NEWLINE && id != TokenId::CARRIAGE_RETURN) {
            ids.push_back(id);
        }
    }
    name.nameId = symbolIdForName(name.spelling);
    name.builtinId = builtinIdForName(name.spelling);
    return name;
}

Call IntegerParser::parseCall() {
    const std::size_t begin = byte_;
    const auto name = consumeQualifiedName();
    if (!at(TokenId::LPAREN)) throw IntegerParserError("Expected '(' after call name");
    Call result(name.spelling, name.nameId, parseArguments(), name.builtinId);
    result.sourceSpan = span(begin, byte_);
    return result;
}

Call IntegerParser::parseAnnotation() {
    require(TokenId::AT, "Expected '@'");
    const auto name = consumeQualifiedName();
    if (!at(TokenId::LPAREN)) {
        if (name.builtinId == BuiltinId::OverrideAnnotation) {
            return Call(name.spelling, name.nameId, {}, name.builtinId);
        }
        throw IntegerParserError("Annotation method '" + name.spelling + "' requires an argument list");
    }
    return Call(name.spelling, name.nameId, parseArguments(true), name.builtinId);
}

TypeRef IntegerParser::parseTypeReference() {
    auto name = consumeQualifiedName(false);
    while (atAdjacentDot()) {
        match(TokenId::DOT);
        const auto segment = consumeQualifiedName(false);
        name.spelling += "." + segment.spelling;
    }
    TypeRef type{name.spelling, {}};
    if (match(TokenId::LESS)) {
        type.arguments.push_back(parseTypeReference());
        if (type.name == "optional") {
            while (match(TokenId::PIPE)) type.arguments.push_back(parseTypeReference());
        } else {
            while (match(TokenId::COMMA)) type.arguments.push_back(parseTypeReference());
        }
        require(TokenId::GREATER, "Expected '>' after generic type arguments");
    }
    if (type.name == "optional") {
        if (type.arguments.empty()) throw IntegerParserError("optional requires at least one type argument");
    } else if (type.name == "list") {
        if (type.arguments.size() != 1) throw IntegerParserError("list requires exactly one type argument");
    } else if (type.name == "Pair") {
        if (type.arguments.size() != 2) {
            throw IntegerParserError("Pair requires exactly two type arguments");
        }
    } else {
        if (!type.arguments.empty()) {
            throw IntegerParserError("Unsupported generic type '" + type.name + "'");
        }
        if (type.name != "obj" && !isFelidaeLikelyTypeName(type.name)) {
            throw IntegerParserError("Expected a Felidae type");
        }
    }
    return type;
}

const OperatorPatternDefinition& IntegerParser::registerOperatorPattern(
    OperatorPatternDefinition pattern) {
    if (!operators_) throw IntegerParserError("Operator registry is unavailable");
    // Pattern structure is parsed once by the registry.  Its literal anchors
    // are then encoded once into model-local integer sequences, before the
    // pattern becomes visible to the integer matcher.
    OperatorRegistry::compilePattern(pattern);
    for (auto& anchor : pattern.anchorLexemes) {
        for (auto& lexeme : anchor) {
            const auto encoded = input_.tokenizer().encode(lexeme.spelling);
            if (encoded.empty()) {
                throw IntegerParserError("Unable to encode mixfix anchor '" + lexeme.spelling + "'");
            }
            lexeme.pieceIds.clear();
            lexeme.pieceIds.reserve(encoded.size());
            for (const auto piece : encoded)
                lexeme.pieceIds.push_back(static_cast<int>(piece));
        }
    }
    return operators_->registerPattern(std::move(pattern));
}

void IntegerParser::prepareOperatorAnnotation(const Call& annotation) {
    if (!operators_ || (annotation.builtinId != BuiltinId::OverloadAnnotation &&
                        annotation.builtinId != BuiltinId::MixfixAnnotation &&
                        annotation.builtinId != BuiltinId::MatcherAnnotation)) return;
    ParsedOperatorAnnotation parsed = decodeParserAnnotation(annotation);
    const bool matcher = annotation.builtinId == BuiltinId::MatcherAnnotation;
    if (annotation.builtinId == BuiltinId::OverloadAnnotation && parsed.hasPattern) {
        throw IntegerParserError(
            "@overload cannot declare syntax; declare the pattern once with @mixfix");
    }
    const OperatorPatternDefinition* pattern = nullptr;
    try {
        if (parsed.pattern.empty()) {
            pattern = operators_->findPatternByOperator(parsed.operatorName);
            if (!pattern) throw IntegerParserError(matcher
                ? "@matcher requires an operator pattern declared by @mixfix"
                : "@overload requires an operator pattern declared by @mixfix");
        } else {
            pattern = operators_->findPattern(parsed.operatorName, parsed.pattern);
            if (!pattern) {
                OperatorPatternDefinition definition;
                definition.operatorName = parsed.operatorName;
                definition.pattern = parsed.pattern;
                for (const auto& capture : parsed.captures) definition.captureTypeNames.push_back(capture.type);
                definition.precedence = parsed.precedence;
                definition.associativity = parsed.associativity;
                definition.fixity = parsed.fixity;
                definition.hasDeclaredFixity = parsed.hasFixity;
                definition.inferFixityFromPattern = parsed.kind == BuiltinId::MixfixAnnotation;
                definition.isMixfixDeclaration = parsed.kind == BuiltinId::MixfixAnnotation;
                definition.visibility = parsed.visibility;
                pattern = &registerOperatorPattern(std::move(definition));
            }
        }
        // The interpreter registers overload/matcher implementations only
        // after the annotated method clause is complete.  Parsing registers
        // syntax here so subsequent source can be assembled by integer IDs.
        (void)matcher;
    } catch (const std::runtime_error& error) {
        throw IntegerParserError(error.what());
    }
}

std::shared_ptr<Goal> IntegerParser::parseGoal() {
    skipTrivia();
    const std::size_t begin = byte_;
    if (at(TokenId::RETURN)) {
        throw IntegerParserError(
            "'return' was removed; the final expression of a callable is its result");
    }
    if (at(TokenId::IF) || at(TokenId::ELIF)) {
        throw IntegerParserError(
            "Block-style 'if/elif' was removed; use 'condition then expression else expression'");
    }
    if (match(TokenId::VAR)) {
        throw IntegerParserError("'var' was removed; declare immutable bindings with 'def'");
    }
    if (match(TokenId::DEF)) {
        const auto variable = consumeQualifiedName(false);
        if (at(TokenId::LPAREN)) {
            parsingFactPattern_ = true;
            std::vector<Arg> arguments;
            try {
                arguments = parseArguments();
            } catch (...) {
                parsingFactPattern_ = false;
                throw;
            }
            parsingFactPattern_ = false;
            for (const auto& argument : arguments) {
                if (const auto output = nodeAs<VarExpr>(argument.value)) {
                    if (insideMethodBody_ && localBindings_.count(output->nameId) == 0 &&
                        std::none_of(patternVariables_.begin(), patternVariables_.end(),
                                     [&](const PatternVariable& known) { return known.id == output->nameId; })) {
                        patternVariables_.push_back(
                            PatternVariable{output->name, output->nameId, output->sourceSpan});
                    }
                    localBindings_.insert(output->nameId);
                }
            }
            Call call(variable.spelling, variable.nameId, std::move(arguments), variable.builtinId);
            call.patternBindings = true;
            auto result = std::make_shared<CallGoal>(std::move(call));
            stamp(result, begin, byte_);
            return result;
        }
        std::optional<TypeRef> declaredType;
        if (match(TokenId::COLON)) declaredType = parseTypeReference();
        std::shared_ptr<Expr> value;
        if (match(TokenId::ASSIGN)) {
            if (at(TokenId::DOT)) throw IntegerParserError("Expected a value after ':='");
            value = parseExpression();
        } else if (declaredType &&
                   (declaredType->name == "optional" || declaredType->name == "any")) {
            value = std::make_shared<NilExpr>();
        } else {
            throw IntegerParserError(
                "Ambiguous 'def " + variable.spelling +
                "'; use ':=' for a binding or '(...)' for a fact pattern");
        }
        std::shared_ptr<Goal> result;
        if (declaredType) {
            result = std::make_shared<MultiAssignGoal>(
                std::vector<AssignmentTarget>{
                    AssignmentTarget{variable.spelling, std::move(*declaredType)}},
                std::move(value));
        } else {
            result = std::make_shared<AssignGoal>(variable.spelling, std::move(value));
        }
        localBindings_.insert(variable.nameId);
        stamp(result, begin, byte_);
        return result;
    }
    if (match(TokenId::FOR)) {
        const auto variable = consumeQualifiedName(false);
        require(TokenId::IN, "Expected 'in' after for-loop variable");
        auto iterable = parseBinaryExpression(
            static_cast<int>(OperatorPrecedence::Control), TokenId::THEN);
        require(TokenId::THEN, "Expected 'then' after for-loop iterable");
        localBindings_.insert(variable.nameId);
        auto body = parseBlockBody();
        requireBlockEnd("Expected 'end' after for loop", begin);
        auto result = std::make_shared<ForGoal>(variable.spelling, std::move(iterable), std::move(body));
        stamp(result, begin, byte_);
        return result;
    }
    if (match(TokenId::WHILE)) {
        auto condition = parseBinaryExpression(
            static_cast<int>(OperatorPrecedence::Control), TokenId::THEN);
        require(TokenId::THEN, "Expected 'then' after while condition");
        auto body = parseBlockBody();
        requireBlockEnd("Expected 'end' after while loop", begin);
        auto result = std::make_shared<WhileGoal>(std::move(condition), std::move(body));
        stamp(result, begin, byte_);
        return result;
    }
    if (match(TokenId::SWITCH)) {
        auto value = parseExpression();
        std::vector<SwitchCase> cases;
        bool sawDefault = false;
        while (!atEnd() && !atBlockEnd()) {
            SwitchCase branch;
            if (match(TokenId::CASE)) {
                if (sawDefault) {
                    throw IntegerParserError("A switch case cannot follow the default branch");
                }
                branch.value = parseBinaryExpression(
                    static_cast<int>(OperatorPrecedence::Control), TokenId::THEN);
                require(TokenId::THEN, "Expected 'then' after switch case");
            } else if (match(TokenId::DEFAULT)) {
                if (sawDefault) throw IntegerParserError("Switch may contain only one default branch");
                sawDefault = true;
                require(TokenId::THEN, "Expected 'then' after switch default");
            } else {
                throw IntegerParserError("Expected 'case', 'default', or 'end' in switch");
            }
            branch.body = parseBlockBody();
            cases.push_back(std::move(branch));
        }
        if (cases.empty()) {
            if (atEnd()) {
                throw IntegerParserIncomplete(
                    "Switch requires at least one case or default branch");
            }
            throw IntegerParserError("Switch requires at least one case or default branch");
        }
        requireBlockEnd("Expected 'end' after switch", begin);
        auto result = std::make_shared<SwitchGoal>(std::move(value), std::move(cases));
        stamp(result, begin, byte_);
        return result;
    }
    if (match(TokenId::TRY)) {
        auto tryBody = parseBlockBody();
        std::vector<CatchClause> catches;
        do {
            require(TokenId::CATCH, "Expected 'catch' after try block");
            const auto variable = consumeQualifiedName(false);
            require(TokenId::THEN, "Expected 'then' after catch variable");
            localBindings_.insert(variable.nameId);
            catches.emplace_back(variable.spelling, parseBlockBody());
        } while (at(TokenId::CATCH));
        requireBlockEnd("Expected 'end' after try/catch", begin);
        auto result = std::make_shared<TryGoal>(std::move(tryBody), std::move(catches));
        stamp(result, begin, byte_);
        return result;
    }
    if (match(TokenId::BREAK)) {
        auto result = std::make_shared<BreakGoal>();

        stamp(result, begin, byte_);
        return result;
    }
    if (match(TokenId::CONTINUE)) {
        auto result = std::make_shared<ContinueGoal>();
        stamp(result, begin, byte_);
        return result;
    }
    if (match(TokenId::WHERE)) {
        auto expression = parseExpression();
        const auto comparison = nodeAs<OperatorExpression>(expression);
        if (!comparison || comparison->captureCount() != 2 ||
            !isComparisonOperator(comparison->coreOperator)) {
            throw IntegerParserError("Expected comparison after 'where'");
        }
        auto binary = std::make_shared<BinaryGoal>(comparison->capture(0),
            coreOperatorDefinition(comparison->coreOperator).token, comparison->capture(1));
        auto result = std::make_shared<WhereGoal>(std::move(binary));
        stamp(result, begin, byte_);
        return result;
    }
    if (match(TokenId::NOT)) {
        auto result = std::make_shared<NotGoal>(parseCall());
        stamp(result, begin, byte_);
        return result;
    }
    const auto start = byte_;
    const auto startPiece = piece_;
    if (atNameRange()) {
        const auto name = consumeNameRange();
        if (match(TokenId::ASSIGN)) {
            throw IntegerParserError("Bindings must begin with 'def'; use 'def " + name + " := ...'");
        }
        if (match(TokenId::COLON)) {
            throw IntegerParserError("Typed bindings must begin with 'def'; use 'def " + name + ": Type := ...'");
        }
    }
    byte_ = start;
    piece_ = startPiece;
    auto left = parseExpression();
    if (match(TokenId::ASSIGN)) {
        const auto access = nodeAs<AccessExpr>(left);
        const auto receiver = access
            ? nodeAs<VarExpr>(access->target) : nullptr;
        if (!access || !receiver || receiver->name != "this" || !insideClassMethod_) {
            throw IntegerParserError(
                "Mutable field assignment is only valid as 'this.field := value' inside a class method");
        }
        Call mutation(std::string(kFieldAssignTerm), {
            Arg{"receiver", access->target},
            Arg{"field", std::make_shared<StringExpr>(access->key)},
            Arg{"value", parseExpression()}});
        auto result = std::make_shared<CallGoal>(std::move(mutation));
        stamp(result, begin, byte_);
        return result;
    }
    // Ordinary expressions are values, including comparisons and calls. They
    // never acquire solver failure semantics merely because they occur in a
    // callable body. The most recently evaluated expression is the implicit
    // callable result; explicit `def Fact(...)` remains the pattern form.
    auto result = std::make_shared<ExpressionGoal>(std::move(left));
    stamp(result, begin, byte_);
    return result;
}

std::vector<std::shared_ptr<Goal>> IntegerParser::parseGoalList(TokenId::Id terminator) {
    std::vector<std::shared_ptr<Goal>> goals;
    if (at(terminator)) return goals;
    do {
        skipTrivia();
        const auto before = byte_;
        goals.push_back(parseGoal());
        if (byte_ == before) throw IntegerParserError("Integer parser made no progress in goal list");
        if (match(TokenId::COMMA)) continue;
        if (at(terminator)) break;
        throw IntegerParserError(
            "Expected ',' or closing delimiter in logical goal list at " +
            sourceLocation(byte_));
    } while (true);
    return goals;
}

std::vector<std::shared_ptr<Goal>> IntegerParser::parseBlockBody() {
    std::vector<std::shared_ptr<Goal>> goals;
    while (!atEnd() && !atBlockEnd() && !at(TokenId::ELSE) &&
           !at(TokenId::ELIF) && !at(TokenId::CASE) &&
           !at(TokenId::DEFAULT) && !at(TokenId::CATCH)) {
        const auto before = byte_;
        auto goal = parseGoal();
        if (byte_ == before) {
            throw IntegerParserError("Integer parser made no progress in block body");
        }
        const bool blockStatement =
            nodeAs<IfGoal>(goal) ||
            nodeAs<ForGoal>(goal) ||
            nodeAs<WhileGoal>(goal) ||
            nodeAs<SwitchGoal>(goal) ||
            nodeAs<TryGoal>(goal);
        goals.push_back(std::move(goal));

        if (blockStatement) {
            // `end` is the complete terminator for a nested block.
            continue;
        }
        if (match(TokenId::COMMA)) {
            continue;
        }
        if (match(TokenId::DOT)) {
            if (at(TokenId::DOT)) {
                throw IntegerParserError(
                    "Consecutive '..' is not valid Felidae syntax at " +
                    sourceLocation(byte_));
            }
            continue;
        }
        throw IntegerParserError(
            "Expected '.' after statement at " + sourceLocation(byte_));
    }
    return goals;
}

std::shared_ptr<Statement> IntegerParser::parseStatement() {
    lastClauseUsedBlockEnd_ = false;
    skipTrivia();
    const std::size_t begin = byte_;
    std::vector<Call> annotations;
    while (at(TokenId::AT)) {
        annotations.push_back(parseAnnotation());
        skipTrivia();
    }
    for (const auto& annotation : annotations) prepareOperatorAnnotation(annotation);
    const bool hasDefKeyword = match(TokenId::DEF);
    if (match(TokenId::VAR)) {
        throw IntegerParserError("'var' was removed; declare immutable bindings with 'def'");
    }
    if (annotations.empty() && match(TokenId::CLASS)) {
        if (hasDefKeyword) {
            throw IntegerParserError("Class declarations must not begin with 'def'");
        }
        try {
            return parseClassStatement(begin);
        } catch (const IntegerParserIncomplete&) {
            throw;
        } catch (const IntegerParserError& error) {
            throw IntegerParserError(
                "In class declaration: " + std::string(error.what()));
        }
    }
    if (match(TokenId::IMPORT)) {
        if (hasDefKeyword) throw IntegerParserError("Import declarations must not begin with 'def'");
        if (!annotations.empty()) throw IntegerParserError("Annotations can only be applied to method declarations");
        std::vector<std::string> paths;
        if (match(TokenId::LPAREN)) {
            if (!at(TokenId::RPAREN)) {
                do { paths.push_back(consumeString()); } while (match(TokenId::COMMA));
            }
            require(TokenId::RPAREN, "Expected ')' after import paths");
        } else {
            paths.push_back(consumeString());
        }
        consumeStatementTerminator("import declaration");
        auto result = std::make_shared<ImportStmt>(std::move(paths));
        stamp(result, begin, byte_);
        return result;
    }
    const auto checkpoint = byte_;
    const auto checkpointPiece = piece_;
    if (atNameRange()) {
        const auto name = consumeNameRange();
        TypeRef declaredType;
        if (hasDefKeyword && match(TokenId::COLON)) declaredType = parseTypeReference();
        if (match(TokenId::ASSIGN)) {
            if (!hasDefKeyword) {
                throw IntegerParserError("Bindings must begin with 'def'; use 'def " + name + " := ...'");
            }
            if (!annotations.empty()) throw IntegerParserError("Annotations can only be applied to method declarations");
            if (at(TokenId::DOT)) throw IntegerParserError("Expected a value after ':='");
            auto result = std::make_shared<GlobalBindingStmt>(name, parseExpression(), std::move(declaredType));
            globalBindings_.insert(symbolIdForName(name));
            consumeStatementTerminator("global binding");
            stamp(result, begin, byte_);
            return result;
        }
        if (hasDefKeyword && !declaredType.name.empty() &&
            (declaredType.name == "optional" || declaredType.name == "any")) {
            consumeStatementTerminator("optional global binding");
            auto result = std::make_shared<GlobalBindingStmt>(
                name, std::make_shared<NilExpr>(), std::move(declaredType));
            globalBindings_.insert(symbolIdForName(name));
            stamp(result, begin, byte_);
            return result;
        }
        if (hasDefKeyword && !declaredType.name.empty()) {
            throw IntegerParserError("Typed binding '" + name + "' requires ':=' unless its type is optional or any");
        }
        if (hasDefKeyword && at(TokenId::DOT)) {
            const auto dotByte = byte_;
            const auto dotPiece = piece_;
            match(TokenId::DOT);
            const bool continuesQualifiedName = atNameRange();
            byte_ = dotByte;
            piece_ = dotPiece;
            if (!continuesQualifiedName) {
                throw IntegerParserError(
                    "Ambiguous 'def " + name +
                    "'; use ':=' for a binding or '(...)' for a fact");
            }
        }
    }
    byte_ = checkpoint;
    piece_ = checkpointPiece;
    // Native and user clauses may use qualified heads such as `math.sin`.
    // The separators are already atomic grammar IDs, so assemble the entire
    // head before requiring its argument list.
    const auto clauseName = consumeQualifiedName();
    std::vector<std::string> parentNames;
    if (match(TokenId::EXTEND)) {
        do { parentNames.push_back(consumeQualifiedName().spelling); } while (match(TokenId::COMMA));
    }
    if (!at(TokenId::LPAREN)) {
        throw IntegerParserError("Expected '(' after clause name '" + clauseName.spelling +
                                 "' at " + sourceLocation(byte_));
    }
    parsingDeclarationHead_ = true;
    std::vector<Arg> headArguments;
    try {
        headArguments = parseArguments();
    } catch (...) {
        parsingDeclarationHead_ = false;
        throw;
    }
    parsingDeclarationHead_ = false;
    Call head(clauseName.spelling, clauseName.nameId, std::move(headArguments), clauseName.builtinId);
    std::vector<std::shared_ptr<Goal>> body;
    std::vector<std::vector<std::shared_ptr<Goal>>> fallbackBranches;
    bool emptyDeclaration = false;
    const bool hasArrow = match(TokenId::ARROW);
    if (hasArrow) {
        if (!hasDefKeyword) {
            throw IntegerParserError("Function and rule declarations must begin with 'def'");
        }
        const auto outerBindings = localBindings_;
        localBindings_.clear();
        const bool outerInsideMethodBody = insideMethodBody_;
        insideMethodBody_ = true;
        variableMentions_.clear();
        patternVariables_.clear();
        // `def eligible(id: x) => def Observation(id: x).` shares `x` between the
        // head and the body: a head variable counts as a mention.
        for (const auto& parameter : head.args) {
            if (const auto variable = nodeAs<VarExpr>(parameter.value)) ++variableMentions_[variable->nameId];
        }
        for (const auto& parameter : head.args) {
            if (!parameter.name.empty()) localBindings_.insert(parameter.nameId);
            else if (const auto variable = nodeAs<VarExpr>(parameter.value))
                localBindings_.insert(variable->nameId);
        }
        for (const auto& annotation : annotations) {
            if (annotation.builtinId != BuiltinId::OverloadAnnotation &&
                annotation.builtinId != BuiltinId::MixfixAnnotation &&
                annotation.builtinId != BuiltinId::MatcherAnnotation) continue;
            const auto parsed = decodeParserAnnotation(annotation);
            for (const auto& binding : parsed.captures) localBindings_.insert(binding.nameId);
            for (const auto& binding : parsed.factors) localBindings_.insert(binding.nameId);
            for (const auto& binding : parsed.produces) localBindings_.insert(binding.nameId);
        }
        const auto emptyByte = byte_;
        const auto emptyPiece = piece_;
        if (match(TokenId::LPAREN) && match(TokenId::RPAREN)) {
            emptyDeclaration = true;
        } else {
            byte_ = emptyByte;
            piece_ = emptyPiece;
            if (match(TokenId::LBRACE) && match(TokenId::RBRACE)) {
                emptyDeclaration = true;
            } else {
                byte_ = emptyByte;
                piece_ = emptyPiece;
            body = parseBlockBody();
            while (match(TokenId::ELSE)) {
                const auto beforeBranch = byte_;
                auto branch = parseBlockBody();
                if (branch.empty() || byte_ == beforeBranch) {
                    if (atEnd()) {
                        throw IntegerParserIncomplete(
                            "Expected fallback branch after 'else'");
                    }
                    throw IntegerParserError("Expected fallback branch after 'else'");
                }
                fallbackBranches.push_back(std::move(branch));
            }
            lastClauseUsedBlockEnd_ = matchBlockEnd();
            }
        }
        localBindings_ = outerBindings;
        insideMethodBody_ = outerInsideMethodBody;
        // A leading underscore marks a name as deliberately unused (`_` alone
        // is the "ignore this field" placeholder and never a variable).
        for (const auto& variable : patternVariables_) {
            if (variable.name.front() == '_' || variableMentions_[variable.id] != 1) continue;
            warnings_.push_back(Warning{
                "'" + variable.name + "' is bound by a fact pattern but never used again, so it matches every "
                "value. Use it, or rename it '_" + variable.name + "' if that is intended; "
                "if it should have matched an existing name, check the spelling.",
                variable.span});
        }
        patternVariables_.clear();
        variableMentions_.clear();
        if (emptyDeclaration) lastClauseUsedBlockEnd_ = matchBlockEnd();
        if (!lastClauseUsedBlockEnd_) {
            if (atEnd()) {
                throw IntegerParserIncomplete(
                    "Expected 'end' after function or rule declaration (block starts at " +
                    sourceLocation(begin) + ")");
            }
            throw IntegerParserError("Expected 'end' after function or rule declaration");
        }
    }
    if (!hasArrow) {
        consumeStatementTerminator("fact or rule declaration");
    }
    // Value-producing callables are identified by their expression sequence;
    // no return keyword or return-dependent fallback path exists.
    const ClauseKind kind = !hasArrow ? (hasDefKeyword ? ClauseKind::Fact : ClauseKind::EntryCall) :
        emptyDeclaration ? ClauseKind::NativeDeclaration :
        (head.nameId == kMainSymbolId || !annotations.empty() || isMethodStyleHead(head) || hasValueExpression(body) || !fallbackBranches.empty() ? ClauseKind::Method :
         body.empty() ? ClauseKind::Fact : ClauseKind::Rule);
    // A method parameter names a type (`n: number`) or is untyped; the runtime
    // binds the argument to the parameter name and never compares it with a
    // literal, so `n: 0` would silently match every call. Relational rules keep
    // literal head arguments: they unify.
    if (kind == ClauseKind::Method) {
        for (const auto& parameter : head.args) {
            if (nodeAs<NumberExpr>(parameter.value) || nodeAs<StringExpr>(parameter.value) ||
                nodeAs<BoolExpr>(parameter.value) || nodeAs<AtomExpr>(parameter.value) ||
                nodeAs<NilExpr>(parameter.value)) {
                throw IntegerParserError(
                    "Parameter '" + parameter.name + "' of '" + head.name +
                    "' must name a type such as 'number' or 'string'; a literal value is not a "
                    "pattern. Compare it in the body instead, for example 'x = 0 then a else b.' at " +
                    sourceLocation(begin));
            }
        }
    }
    auto result = std::make_shared<ClauseStmt>(std::move(head), std::move(parentNames), std::move(body),
                                               std::move(fallbackBranches),
                                               emptyDeclaration, kind);
    if (!annotations.empty() && result->clauseKind != ClauseKind::Method) {
        throw IntegerParserError("Annotations can only be applied to complete method declarations");
    }
    result->annotations = std::move(annotations);
    stamp(result, begin, byte_);
    return result;
}

std::shared_ptr<ClassStmt> IntegerParser::parseClassStatement(std::size_t begin) {
    auto className = consumeQualifiedName(false);
    while (atAdjacentDot()) {
        match(TokenId::DOT);
        const auto related = consumeQualifiedName(false);
        className.spelling += "." + related.spelling;
    }
    className.nameId = symbolIdForName(className.spelling);
    if (className.spelling == "Link") {
        throw IntegerParserError(
            "Link is reserved for persistent graph edges and cannot be declared as a class");
    }
    const auto consumeDottedType = [&]() {
        auto type = consumeQualifiedName(false);
        while (atAdjacentDot()) {
            match(TokenId::DOT);
            const auto segment = consumeQualifiedName(false);
            type.spelling += "." + segment.spelling;
        }
        type.nameId = symbolIdForName(type.spelling);
        return type;
    };
    const auto headerByte = byte_;
    const auto headerPiece = piece_;
    if (atNameRange() && consumeQualifiedName(false).spelling == "relationship") {
        throw IntegerParserError(
            "Relationship classes were removed; create graph edges with Link(from:, to:, properties:)");
    } else {
        byte_ = headerByte;
        piece_ = headerPiece;
    }
    std::vector<std::string> parents;
    if (match(TokenId::EXTEND) || match(TokenId::EXTENDS)) {
        do { parents.push_back(consumeDottedType().spelling); } while (match(TokenId::COMMA));
    }
    std::vector<ClassFieldDecl> fields;
    ClassKeyDecl key;
    std::vector<ClassIndexDecl> indexes;
    std::vector<std::shared_ptr<ClauseStmt>> methods;
    std::unordered_set<SymbolId> fieldIds;
    const auto finishOrdinaryMember = [&](const char* description) {
        if (match(TokenId::DOT)) {
            if (at(TokenId::DOT)) {
                throw IntegerParserError(
                    "Consecutive '..' is not valid Felidae syntax at " +
                    sourceLocation(byte_));
            }
            return;
        }
        throw IntegerParserError(
            "Expected '.' after " + std::string(description) +
            " at " + sourceLocation(byte_));
    };
    const auto parseClassMethod = [&]() {
        const bool previousClassMethod = insideClassMethod_;
        insideClassMethod_ = true;
        std::shared_ptr<ClauseStmt> method;
        try {
            method = nodeAs<ClauseStmt>(parseStatement());
        } catch (...) {
            insideClassMethod_ = previousClassMethod;
            throw;
        }
        insideClassMethod_ = previousClassMethod;
        if (!method || method->clauseKind != ClauseKind::Method || !lastClauseUsedBlockEnd_) {
            throw IntegerParserError("Class methods must be methods terminated by 'end'");
        }
        method->head.name = className.spelling + "." + method->head.name;
        method->head.nameId = symbolIdForName(method->head.name);
        return method;
    };
    while (!atEnd() && !atBlockEnd()) {
        const auto fieldBegin = byte_;
        const auto fieldPiece = piece_;
        if (at(TokenId::AT)) {
            methods.push_back(parseClassMethod());
            continue;
        }
        bool hasFieldDef = false;
        if (at(TokenId::DEF)) {
            const auto lookaheadByte = byte_;
            const auto lookaheadPiece = piece_;
            match(TokenId::DEF);
            (void)consumeQualifiedName(false);
            hasFieldDef = at(TokenId::COLON);
            byte_ = lookaheadByte;
            piece_ = lookaheadPiece;
            if (!hasFieldDef) {
                methods.push_back(parseClassMethod());
                continue;
            }
            match(TokenId::DEF);
        }
        if (match(TokenId::INDEX)) {
            require(TokenId::LPAREN, "Expected '(' after class index");
            ClassIndexDecl index;
            do {
                const auto indexed = consumeQualifiedName();
                index.fields.push_back(indexed.spelling);
                index.fieldIds.push_back(indexed.nameId);
            } while (match(TokenId::COMMA));
            require(TokenId::RPAREN, "Expected ')' after class index fields");
            if (index.fields.empty()) throw IntegerParserError("Class index requires at least one field");
            finishOrdinaryMember("class index declaration");
            index.sourceSpan = span(fieldBegin, byte_);
            indexes.push_back(std::move(index));
            continue;
        }
        if (!atNameRange()) {
            throw IntegerParserError(
                "Expected a class member or 'end' at " +
                sourceLocation(byte_));
        }
        const auto field = consumeQualifiedName();
        if (field.spelling == "foreign") {
            throw IntegerParserError(
                "Foreign keys were removed; create graph edges with Link(from:, to:, properties:)");
        }
        if (field.spelling == "key" && at(TokenId::LPAREN)) {
            if (!key.fields.empty()) throw IntegerParserError("Class may declare only one key");
            match(TokenId::LPAREN);
            do {
                const auto keyed = consumeQualifiedName(false);
                key.fields.push_back(keyed.spelling);
                key.fieldIds.push_back(keyed.nameId);
            } while (match(TokenId::COMMA));
            require(TokenId::RPAREN, "Expected ')' after class key fields");
            if (key.fields.empty()) throw IntegerParserError("Class key requires at least one field");
            finishOrdinaryMember("class key declaration");
            key.sourceSpan = span(fieldBegin, byte_);
            continue;
        }
        if (at(TokenId::LPAREN)) {
            byte_ = fieldBegin;
            piece_ = fieldPiece;
            methods.push_back(parseClassMethod());
            continue;
        }
        if (!hasFieldDef) {
            throw IntegerParserError(
                "Class field '" + field.spelling +
                "' must begin with 'def'");
        }
        require(TokenId::COLON, "Expected ':' after class field name");
        TypeRef type;
        try {
            type = parseTypeReference();
        } catch (const IntegerParserError& error) {
            throw IntegerParserError(
                "While parsing type of class field '" + field.spelling +
                "': " + error.what());
        }
        std::shared_ptr<Expr> defaultValue;
        if (match(TokenId::ASSIGN)) defaultValue = parseExpression();
        if (!fieldIds.insert(field.nameId).second)
            throw IntegerParserError("Duplicate class field '" + field.spelling + "'");
        finishOrdinaryMember("class field declaration");
        fields.emplace_back(field.spelling, field.nameId, std::move(type),
                            std::move(defaultValue), span(fieldBegin, byte_));
    }
    requireBlockEnd("Expected 'end' after class declaration", begin);
    if (!key.fields.empty()) {
        for (std::size_t i = 0; i < key.fields.size(); ++i) {
            if (fieldIds.count(key.fieldIds[i]) == 0) {
                throw IntegerParserError("Class key references unknown field '" + key.fields[i] + "'");
            }
            const auto keyed = std::find_if(fields.begin(), fields.end(), [&](const ClassFieldDecl& field) {
                return field.nameId == key.fieldIds[i];
            });
            if (keyed != fields.end() && keyed->type.isOptional()) {
                throw IntegerParserError("Class key field '" + key.fields[i] + "' cannot be optional");
            }
        }
    }
    auto result = std::make_shared<ClassStmt>(className.spelling, className.nameId, std::move(parents),
                                               std::move(fields), std::move(key), std::move(indexes),
                                               std::move(methods));
    stamp(result, begin, byte_);
    return result;
}

Program IntegerParser::parseProgram() {
    Program program;
    while (!programComplete())
        program.addStatement(parseNextProgramStatement());
    return program;
}

bool IntegerParser::programComplete() {
    return atEnd();
}

std::shared_ptr<Statement> IntegerParser::parseNextProgramStatement() {
    if (programComplete()) {
        throw IntegerParserError("Expected a program statement");
    }
    const auto before = byte_;
    // The iteration budget guards one statement against a runaway parse. It is
    // not a limit on file size, so it restarts for every top-level statement.
    statementIterations_ = 0;
    std::shared_ptr<Statement> statement;
    try {
        statement = parseStatement();
    } catch (const IntegerParserIncomplete& error) {
        const std::string message = error.what();
        if (message.find("line ") != std::string::npos) throw;
        throw IntegerParserIncomplete(message + " at " + sourceLocation(byte_));
    } catch (const IntegerParserError& error) {
        const std::string message = error.what();
        if (message.find("line ") != std::string::npos) throw;
        throw IntegerParserError(message + " at " + sourceLocation(byte_));
    }
    ++metrics_.statementCount;
    if (byte_ == before) {
        throw IntegerParserError("Integer parser made no progress in program");
    }
    return statement;
}

bool IntegerParser::emptyInput() {
    return atEnd();
}

bool IntegerParser::startsDeclaration() {
    return at(TokenId::DEF) || at(TokenId::CLASS) || at(TokenId::AT) ||
           at(TokenId::IMPORT);
}

std::shared_ptr<Expr> IntegerParser::parseMap() {
    const std::size_t begin = byte_;
    require(TokenId::LBRACE, "Expected '{'");
    std::vector<MapEntry> entries;
    if (!at(TokenId::RBRACE)) {
        do {
            const auto key = consumeNameRange();
            require(TokenId::COLON, "Expected ':' after map key");
            entries.emplace_back(key, parseExpression());
        } while (match(TokenId::COMMA));
    }
    require(TokenId::RBRACE, "Expected '}' after map");
    auto result = std::make_shared<MapExpr>(std::move(entries));
    stamp(result, begin, byte_);
    return result;
}

std::shared_ptr<Expr> IntegerParser::parsePrimary() {
    RecursionScope recursion(*this);
    skipTrivia();
    const std::size_t begin = byte_;
    if (at(TokenId::DOT)) {
        throw IntegerParserError(
            "Member access cannot begin with '.' at " +
            sourceLocation(begin));
    }
    if (at(TokenId::QUOTE)) {
        auto result = std::make_shared<StringExpr>(consumeString());
        stamp(result, begin, byte_);
        return result;
    }
    if (at(TokenId::ATOM_QUOTE)) {
        auto result = std::make_shared<AtomExpr>(consumeAtom());
        stamp(result, begin, byte_);
        return result;
    }
    if (piece_ < input_.entries().size() && isDecimalDigitId(input_.entries()[piece_].id)) {
        auto result = std::make_shared<NumberExpr>(consumeNumber());
        stamp(result, begin, byte_);
        return result;
    }
    if (match(TokenId::TRUE)) { auto result = std::make_shared<BoolExpr>(true); stamp(result, begin, byte_); return result; }
    if (match(TokenId::FALSE)) { auto result = std::make_shared<BoolExpr>(false); stamp(result, begin, byte_); return result; }
    if (match(TokenId::NIL)) { auto result = std::make_shared<NilExpr>(); stamp(result, begin, byte_); return result; }
    if (match(TokenId::THIS)) {
        if (!insideClassMethod_) {
            throw IntegerParserError("'this' is only valid inside a class method");
        }
        auto result = std::make_shared<VarExpr>("this", symbolIdForName("this"));
        stamp(result, begin, byte_);
        return result;
    }
    if (match(TokenId::SUPER)) {
        if (!insideClassMethod_) {
            throw IntegerParserError("'super()' is only valid inside a class method");
        }
        require(TokenId::LPAREN, "Expected '(' after super");
        require(TokenId::RPAREN, "super does not accept arguments");
        auto result = std::make_shared<TermExpr>(
            std::string(kSuperTerm), std::vector<Arg>{});
        stamp(result, begin, byte_);
        return result;
    }
    if (match(TokenId::NEW)) {
        // `new school.student(...)` is a dotted type in explicit type context.
        auto type = consumeQualifiedName(false);
        while (atAdjacentDot()) {
            match(TokenId::DOT);
            const auto related = consumeQualifiedName(false);
            type.spelling += "." + related.spelling;
        }
        auto arguments = parseArguments();
        arguments.insert(arguments.begin(), Arg{"type", std::make_shared<StringExpr>(type.spelling)});
        auto result = std::make_shared<TermExpr>("Object:new", std::move(arguments));
        stamp(result, begin, byte_);
        return result;
    }
    if (match(TokenId::LAMBDA)) {
        require(TokenId::LPAREN, "Expected '(' after lambda");
        auto source = parseExpression();
        require(TokenId::COMMA, "Expected ',' after lambda source");
        const auto variable = consumeNameRange();
        require(TokenId::ARROW, "Expected '=>' after lambda variable");
        auto body = parseExpression();
        require(TokenId::RPAREN, "Expected ')' after lambda");
        auto result = std::make_shared<LambdaExpr>(std::move(source), variable, std::move(body));
        stamp(result, begin, byte_);
        return result;
    }
    if (at(TokenId::LBRACKET)) return parseArray();
    if (at(TokenId::LBRACE)) return parseMap();
    if (match(TokenId::LPAREN)) {
        const auto tupleByte = byte_;
        const auto tuplePiece = piece_;
        std::vector<MapEntry> fields;
        if (atNameRange()) {
            const auto name = consumeNameRange();
            if (match(TokenId::COLON)) {
                fields.emplace_back(name, parseExpression());
                while (match(TokenId::COMMA)) {
                    const auto field = consumeNameRange();
                    require(TokenId::COLON, "Expected ':' after tuple field name");
                    fields.emplace_back(field, parseExpression());
                }
                require(TokenId::RPAREN, "Expected ')' after named tuple");
                auto result = std::make_shared<MapExpr>(std::move(fields));
                stamp(result, begin, byte_);
                return result;
            }
        }
        byte_ = tupleByte;
        piece_ = tuplePiece;
        auto result = parseExpression();
        require(TokenId::RPAREN, "Expected ')' after grouped expression");
        stamp(result, begin, byte_);
        return result;
    }
    if (atNameRange()) {
        // A registered qualified callable takes precedence over member access.
        // Otherwise keep dots for parseUnary so `people.get()` remains a
        // receiver call rather than an unrelated qualified name.
        const auto nameByte = byte_;
        const auto namePiece = piece_;
        auto name = consumeQualifiedName();
        const bool qualifiedCallable = name.spelling.find('.') != std::string::npos &&
            name.builtinId != BuiltinId::Unknown;
        if (!qualifiedCallable) {
            byte_ = nameByte;
            piece_ = namePiece;
            name = consumeQualifiedName(false);
        }
        if (at(TokenId::LPAREN)) {
            auto result = std::make_shared<TermExpr>(name.spelling, name.nameId, parseArguments(),
                                                     name.builtinId);
            stamp(result, begin, byte_);
            return result;
        }
        const auto fixedArity = name.builtinId != BuiltinId::Unknown
            ? fixedBuiltinArity(name.builtinId)
            : signatures_->uniqueFixedArity(name.spelling);
        // A parameter or binding named like a callable (`count >= 400`) is that
        // variable, not the start of a whitespace call.
        const bool boundVariable = localBindings_.count(name.nameId) != 0 ||
                                   globalBindings_.count(name.nameId) != 0;
        if (!boundVariable && !atAdjacentDot() && fixedArity && *fixedArity != 0) {
            auto result = std::make_shared<TermExpr>(
                name.spelling, name.nameId, parseWhitespaceArguments(*fixedArity),
                name.builtinId);
            stamp(result, begin, byte_);
            return result;
        }
        if (!boundVariable && !atAdjacentDot() &&
            signatures_->hasAmbiguousFixedArities(name.spelling)) {
            throw IntegerParserError(
                "Call to '" + name.spelling +
                "' has multiple declared arities; use parentheses to select one");
        }
        std::shared_ptr<Expr> result;
        if (parsingDeclarationHead_ || parsingFactPattern_ || insideQuery_ ||
            localBindings_.count(name.nameId) != 0 ||
            globalBindings_.count(name.nameId) != 0 || atAdjacentDot()) {
            result = std::make_shared<VarExpr>(name.spelling, name.nameId,
                                               languageTypeIdForName(name.spelling));
            if (insideMethodBody_) ++variableMentions_[name.nameId];
        } else {
            result = std::make_shared<AtomExpr>(name.spelling);
        }
        stamp(result, begin, byte_);
        return result;
    }
    if (atEnd()) {
        throw IntegerParserIncomplete(
            "Expected an expression at " + sourceLocation(byte_));
    }
    throw IntegerParserError("Expected an expression at " +
                             sourceLocation(byte_));
}

std::shared_ptr<Expr> IntegerParser::parseExpression() {
    const auto sequence = [&]() -> std::shared_ptr<Expr> {
        std::vector<std::shared_ptr<Expr>> expressions;
        expressions.push_back(parseBinaryExpression(
            static_cast<int>(OperatorPrecedence::Control), TokenId::ELSE));
        while (match(TokenId::COMMA)) {
            expressions.push_back(parseBinaryExpression(
                static_cast<int>(OperatorPrecedence::Control), TokenId::ELSE));
        }
        if (expressions.size() == 1) return expressions.front();
        return std::make_shared<SequenceExpr>(std::move(expressions));
    };

    auto condition = parseBinaryExpression(
        static_cast<int>(OperatorPrecedence::Control), TokenId::THEN);
    if (!match(TokenId::THEN)) return condition;

    std::vector<ConditionalBranch> branches;
    for (;;) {
        branches.push_back({std::move(condition), sequence()});
        if (!match(TokenId::ELSE)) {
            return std::make_shared<ConditionalExpr>(std::move(branches));
        }

        const auto candidate = parseBinaryExpression(
            static_cast<int>(OperatorPrecedence::Control), TokenId::THEN);
        if (match(TokenId::THEN)) {
            condition = candidate;
            continue;
        }

        std::vector<std::shared_ptr<Expr>> fallbackExpressions;
        fallbackExpressions.push_back(candidate);
        while (match(TokenId::COMMA)) {
            fallbackExpressions.push_back(parseBinaryExpression(
                static_cast<int>(OperatorPrecedence::Control), TokenId::ELSE));
        }
        std::shared_ptr<Expr> fallback = fallbackExpressions.size() == 1
            ? fallbackExpressions.front()
            : std::make_shared<SequenceExpr>(std::move(fallbackExpressions));
        return std::make_shared<ConditionalExpr>(
            std::move(branches), std::move(fallback));
    }
}

bool IntegerParser::atPatternLexeme(const PatternLexeme& lexeme) {
    const auto savedByte = byte_;
    const auto savedPiece = piece_;
    const bool matched = matchPatternLexeme(lexeme);
    byte_ = savedByte;
    piece_ = savedPiece;
    return matched;
}

bool IntegerParser::matchPatternLexeme(const PatternLexeme& lexeme) {
    if (lexeme.pieceIds.empty()) return false;
    const auto& entries = input_.entries();
    const auto savedByte = byte_;
    const auto savedPiece = piece_;
    skipTrivia();
    std::size_t wanted = 0;
    while (wanted < lexeme.pieceIds.size()) {
        if (piece_ >= entries.size() || entries[piece_].begin > byte_ ||
            entries[piece_].end <= byte_) {
            byte_ = savedByte;
            piece_ = savedPiece;
            return false;
        }
        if (entries[piece_].id != lexeme.pieceIds[wanted]) {
            byte_ = savedByte;
            piece_ = savedPiece;
            return false;
        }
        ++wanted;
        byte_ = entries[piece_++].end;
    }
    return true;
}

bool IntegerParser::atPatternAnchor(const std::vector<PatternLexeme>& anchor) {
    const auto savedByte = byte_;
    const auto savedPiece = piece_;
    const bool matched = matchPatternAnchor(anchor);
    byte_ = savedByte;
    piece_ = savedPiece;
    return matched;
}

bool IntegerParser::matchPatternAnchor(const std::vector<PatternLexeme>& anchor) {
    const auto savedByte = byte_;
    const auto savedPiece = piece_;
    for (const auto& lexeme : anchor) {
        if (matchPatternLexeme(lexeme)) continue;
        byte_ = savedByte;
        piece_ = savedPiece;
        return false;
    }
    return true;
}

std::shared_ptr<Expr> IntegerParser::tryParseLeadingPattern() {
    if (!operators_) return {};
    const auto startByte = byte_;
    const auto startPiece = piece_;
    std::shared_ptr<Expr> selected;
    std::size_t selectedByte = startByte;
    std::size_t selectedPiece = startPiece;
    for (const auto& pattern : operators_->patterns()) {
        if (pattern.startsWithCapture || pattern.anchorLexemes.empty() ||
            pattern.anchorLexemes.front().empty()) continue;
        ++metrics_.backtrackingAttempts;
        if (!atPatternLexeme(pattern.anchorLexemes.front().front())) continue;
        byte_ = startByte;
        piece_ = startPiece;
        try {
            if (!matchPatternAnchor(pattern.anchorLexemes.front())) continue;
            std::vector<OperatorCapture> captures;
            captures.reserve(pattern.captureNames.size());
            for (std::size_t index = 0; index < pattern.captureNames.size(); ++index) {
                const bool adjacent = index + 1 < pattern.captureNames.size() &&
                    (index >= pattern.followingAnchorIndices.size() ||
                     !pattern.followingAnchorIndices[index].has_value());
                const auto following = index < pattern.followingAnchorIndices.size()
                    ? pattern.followingAnchorIndices[index] : std::optional<std::size_t>{};
                const auto* stopAnchor = following && *following < pattern.anchorLexemes.size()
                    ? &pattern.anchorLexemes[*following] : nullptr;
                auto captured = adjacent ? parseUnary() : parseBinaryExpression(
                    static_cast<int>(pattern.precedence), TokenId::UNKNOWN, stopAnchor);
                captures.emplace_back(pattern.captureNames[index], std::move(captured));
                if (following && (*following >= pattern.anchorLexemes.size() ||
                                  !matchPatternAnchor(pattern.anchorLexemes[*following]))) {
                    throw IntegerParserError("mixfix candidate did not consume its next anchor");
                }
            }
            if (!selected || byte_ > selectedByte) {
                selected = std::make_shared<OperatorExpression>(
                    pattern.operatorId, pattern.patternId, std::move(captures));
                selectedByte = byte_;
                selectedPiece = piece_;
            }
        } catch (const IntegerParserError&) {
            // Another candidate sharing this integer anchor may still match.
        }
        byte_ = startByte;
        piece_ = startPiece;
    }
    if (!selected) return {};
    byte_ = selectedByte;
    piece_ = selectedPiece;
    return selected;
}

std::shared_ptr<Expr> IntegerParser::tryParseTrailingPattern(std::shared_ptr<Expr> left,
                                                              int minimumPrecedence) {
    if (!operators_) return {};
    const auto startByte = byte_;
    const auto startPiece = piece_;
    std::shared_ptr<Expr> immediate;
    std::size_t immediateByte = startByte;
    std::size_t immediatePiece = startPiece;
    for (const auto& pattern : operators_->patterns()) {
        if (!pattern.startsWithCapture || pattern.captureNames.empty() ||
            pattern.anchorLexemes.empty() || pattern.anchorLexemes.front().empty() ||
            static_cast<int>(pattern.precedence) < minimumPrecedence) continue;
        ++metrics_.backtrackingAttempts;
        if (!atPatternLexeme(pattern.anchorLexemes.front().front())) continue;
        byte_ = startByte;
        piece_ = startPiece;
        try {
            if (!matchPatternAnchor(pattern.anchorLexemes.front())) continue;
            std::vector<OperatorCapture> captures;
            captures.reserve(pattern.captureNames.size());
            captures.emplace_back(pattern.captureNames.front(), left->clone());
            for (std::size_t index = 1; index < pattern.captureNames.size(); ++index) {
                const bool adjacent = index + 1 < pattern.captureNames.size() &&
                    (index >= pattern.followingAnchorIndices.size() ||
                     !pattern.followingAnchorIndices[index].has_value());
                const auto following = index < pattern.followingAnchorIndices.size()
                    ? pattern.followingAnchorIndices[index] : std::optional<std::size_t>{};
                const auto* stopAnchor = following && *following < pattern.anchorLexemes.size()
                    ? &pattern.anchorLexemes[*following] : nullptr;
                auto captured = adjacent ? parseUnary() : parseBinaryExpression(
                    static_cast<int>(pattern.precedence), TokenId::UNKNOWN, stopAnchor);
                captures.emplace_back(pattern.captureNames[index], std::move(captured));
                if (following && (*following >= pattern.anchorLexemes.size() ||
                                  !matchPatternAnchor(pattern.anchorLexemes[*following]))) {
                    throw IntegerParserError("trailing mixfix candidate did not consume its next anchor");
                }
            }
            if (!immediate || byte_ > immediateByte) {
                immediate = std::make_shared<OperatorExpression>(
                    pattern.operatorId, pattern.patternId, std::move(captures));
                immediateByte = byte_;
                immediatePiece = piece_;
            }
        } catch (const IntegerParserError&) {
            // Another pattern sharing this anchor may still match.
        }
        byte_ = startByte;
        piece_ = startPiece;
    }
    if (immediate) {
        byte_ = immediateByte;
        piece_ = immediatePiece;
        return immediate;
    }
    const OperatorPatternDefinition* selected = nullptr;
    std::size_t selectedLength = 0;
    for (const auto& pattern : operators_->patterns()) {
        if (!pattern.startsWithCapture || pattern.captureNames.empty() ||
            pattern.anchorLexemes.empty() || pattern.anchorLexemes.front().empty() ||
            static_cast<int>(pattern.precedence) < minimumPrecedence) continue;
        ++metrics_.backtrackingAttempts;
        if (!atPatternLexeme(pattern.anchorLexemes.front().front())) continue;
        const auto length = pattern.anchorLexemes.front().size();
        if (selected && length == selectedLength) {
            throw IntegerParserError("Ambiguous integer mixfix anchor sequence");
        }
        if (!selected || length > selectedLength) {
            selected = &pattern;
            selectedLength = length;
        }
    }
    if (!selected) {
        const auto deferredStartByte = byte_;
        const auto deferredStartPiece = piece_;
        std::shared_ptr<Expr> deferred;
        std::size_t deferredByte = deferredStartByte;
        std::size_t deferredPiece = deferredStartPiece;
        for (const auto* pattern : operators_->deferredTrailingCapturePatterns()) {
            if (static_cast<int>(pattern->precedence) < minimumPrecedence) continue;
            ++metrics_.backtrackingAttempts;
            byte_ = deferredStartByte;
            piece_ = deferredStartPiece;
            try {
                std::vector<OperatorCapture> captures;
                captures.reserve(pattern->captureNames.size());
                captures.emplace_back(pattern->captureNames.front(), left->clone());
                for (std::size_t index = 1; index < pattern->captureNames.size(); ++index) {
                    const bool adjacent = index + 1 < pattern->captureNames.size() &&
                        (index >= pattern->followingAnchorIndices.size() ||
                         !pattern->followingAnchorIndices[index].has_value());
                    const auto following = index < pattern->followingAnchorIndices.size()
                        ? pattern->followingAnchorIndices[index] : std::optional<std::size_t>{};
                    const auto* stopAnchor = following && *following < pattern->anchorLexemes.size()
                        ? &pattern->anchorLexemes[*following] : nullptr;
                    auto captured = adjacent ? parseUnary() : parseBinaryExpression(
                        static_cast<int>(pattern->precedence), TokenId::UNKNOWN, stopAnchor);
                    captures.emplace_back(pattern->captureNames[index], std::move(captured));
                    if (following && (*following >= pattern->anchorLexemes.size() ||
                                      !matchPatternAnchor(pattern->anchorLexemes[*following]))) {
                        throw IntegerParserError("deferred mixfix candidate did not consume its next anchor");
                    }
                }
                if (!deferred || byte_ > deferredByte) {
                    deferred = std::make_shared<OperatorExpression>(
                        pattern->operatorId, pattern->patternId, std::move(captures));
                    deferredByte = byte_;
                    deferredPiece = piece_;
                }
            } catch (const IntegerParserError&) {
                // A different deferred shape may consume this same ID range.
            }
        }
        if (!deferred) {
            byte_ = deferredStartByte;
            piece_ = deferredStartPiece;
            return {};
        }
        byte_ = deferredByte;
        piece_ = deferredPiece;
        return deferred;
    }
    if (!matchPatternAnchor(selected->anchorLexemes.front())) {
        throw IntegerParserError("Integer mixfix anchor disappeared during assembly");
    }
    std::vector<OperatorCapture> captures;
    captures.reserve(selected->captureNames.size());
    captures.emplace_back(selected->captureNames.front(), std::move(left));
    for (std::size_t index = 1; index < selected->captureNames.size(); ++index) {
        const bool adjacent = index + 1 < selected->captureNames.size() &&
            (index >= selected->followingAnchorIndices.size() ||
             !selected->followingAnchorIndices[index].has_value());
        const auto following = index < selected->followingAnchorIndices.size()
            ? selected->followingAnchorIndices[index] : std::optional<std::size_t>{};
        const auto* stopAnchor = following && *following < selected->anchorLexemes.size()
            ? &selected->anchorLexemes[*following] : nullptr;
        auto captured = adjacent ? parseUnary() : parseBinaryExpression(
            static_cast<int>(selected->precedence), TokenId::UNKNOWN, stopAnchor);
        captures.emplace_back(selected->captureNames[index], std::move(captured));
        if (index < selected->followingAnchorIndices.size() &&
            selected->followingAnchorIndices[index]) {
            const auto anchorIndex = *selected->followingAnchorIndices[index];
            if (anchorIndex >= selected->anchorLexemes.size() ||
                !matchPatternAnchor(selected->anchorLexemes[anchorIndex])) {
                throw IntegerParserError("Expected integer mixfix literal anchor for '" +
                                         selected->operatorName + "' at " +
                                         sourceLocation(byte_));
            }
        }
    }
    return std::make_shared<OperatorExpression>(selected->operatorId, selected->patternId,
                                                std::move(captures));
}

std::shared_ptr<Expr> IntegerParser::parseUnary() {
    if (auto pattern = tryParseLeadingPattern()) return pattern;
    if (match(TokenId::NOT)) return std::make_shared<OperatorExpression>(CoreOperator::LogicalNot, parseUnary());
    if (match(TokenId::MINUS)) {
        auto operand = parseUnary();
        if (const auto number = nodeAs<NumberExpr>(operand)) {
            return std::make_shared<NumberExpr>(-number->value);
        }
        return std::make_shared<OperatorExpression>(CoreOperator::UnaryMinus, std::move(operand));
    }
    if (match(TokenId::PLUS)) return parseUnary();
    auto result = parsePrimary();
    while (atAdjacentDot()) {
        const auto beforeByte = byte_;
        const auto beforePiece = piece_;
        match(TokenId::DOT);
        const auto memberKeyword = piece_ < input_.entries().size()
            ? builtinTokenSpelling(input_.entries()[piece_].id) : std::string_view{};
        const bool classReference = at(TokenId::CLASS);
        if (!atNameRange() && memberKeyword.empty() && !classReference) {
            byte_ = beforeByte;
            piece_ = beforePiece;
            break;
        }
        std::string member;
        if (classReference) {
            member = "class";
            match(TokenId::CLASS);
        } else if (atNameRange()) {
            member = consumeNameRange();
        } else {
            member = std::string(memberKeyword);
            byte_ = input_.entries()[piece_++].end;
        }
        if (!at(TokenId::LPAREN)) {
            if (member == "class") {
                const auto qualifiedName = [&](const auto& self, const std::shared_ptr<Expr>& value) -> std::string {
                    if (const auto reference = nodeAs<VarExpr>(value)) return reference->name;
                    if (const auto access = nodeAs<AccessExpr>(value)) {
                        const auto prefix = self(self, access->target);
                        return prefix.empty() ? std::string{} : prefix + "." + access->key;
                    }
                    return {};
                };
                const auto qualified = qualifiedName(qualifiedName, result);
                if (qualified.empty()) {
                    throw IntegerParserError(".class requires a class name");
                }
                result = std::make_shared<ClassRefExpr>(qualified);
                continue;
            }
            if (member == "function") {
                const auto reference = nodeAs<VarExpr>(result);
                if (!reference) {
                    throw IntegerParserError(".function requires a function name");
                }
                result = std::make_shared<FunctionRefExpr>(reference->name);
                continue;
            }
            result = std::make_shared<AccessExpr>(std::move(result), member);
            continue;
        }
        auto arguments = parseArguments();
        const bool runtimeBuiltinMember =
            member == "all" || member == "count" || member == "get" ||
            member == "first" || member == "sum" || member == "average" ||
            member == "min" || member == "max" ||
            member == "where" || member == "select" || member == "insert" ||
            member == "save" || member == "update" || member == "delete" ||
            member == "AndWhere" || member == "OrWhere" || member == "limit" ||
            member == "order_by" || member == "join" ||
            member == "recursive_join" || member == "shortest_path" ||
            member == "len" || member == "push";
        if (runtimeBuiltinMember) {
            // Facts are built into RocksDB and are queried through their own
            // class, as in Employee.count(). `Fact` is only the root of the type
            // lineage; it is neither a library nor a queryable class.
            if (member == "join" || member == "recursive_join" || member == "shortest_path") {
                for (const auto& argument : arguments) {
                    if (argument.name == "direction" &&
                        nodeAs<VarExpr>(argument.value)) {
                        throw IntegerParserError(
                            "Graph direction requires forward.class, backward.class, or both.class");
                    }
                }
            }
            std::vector<Arg> invokeArgs;
            invokeArgs.reserve(arguments.size() + 2);
            invokeArgs.emplace_back("receiver", std::move(result));
            invokeArgs.emplace_back("member", std::make_shared<StringExpr>(member));
            for (auto& argument : arguments) invokeArgs.push_back(std::move(argument));
            result = std::make_shared<TermExpr>(
                std::string(kMemberInvokeTerm), std::move(invokeArgs));
            continue;
        }
        if (const auto receiver = nodeAs<VarExpr>(result)) {
            const std::string qualified = receiver->name + "." + member;
            const BuiltinId builtin = builtinIdForName(qualified);
            if (builtin != BuiltinId::Unknown) {
                result = std::make_shared<TermExpr>(qualified, std::move(arguments), builtin);
                continue;
            }
        }
        // Class methods are registered as `Class.method`, but the receiver's
        // concrete class may only be known after evaluating an earlier call
        // in a chain. Preserve the receiver and member for runtime dispatch.
        std::vector<Arg> invokeArgs;
        invokeArgs.reserve(arguments.size() + 2);
        invokeArgs.emplace_back("receiver", std::move(result));
        invokeArgs.emplace_back("member", std::make_shared<StringExpr>(member));
        for (auto& argument : arguments) invokeArgs.push_back(std::move(argument));
        result = std::make_shared<TermExpr>(
            std::string(kMemberInvokeTerm), std::move(invokeArgs));
    }
    return result;
}

std::shared_ptr<Expr> IntegerParser::parseBinaryExpression(
    int minimumPrecedence, TokenId::Id stop, const std::vector<PatternLexeme>* stopAnchor) {
    RecursionScope recursion(*this);
    auto left = parseUnary();
    while (true) {
        step();
        skipTrivia();
        if (piece_ >= input_.entries().size() || input_.entries()[piece_].begin > byte_ ||
            input_.entries()[piece_].end <= byte_) break;
        if (stop != TokenId::UNKNOWN && input_.entries()[piece_].id == stop) break;
        if (stopAnchor && atPatternAnchor(*stopAnchor)) break;
        if (input_.entries()[piece_].id == TokenId::EQUAL &&
            piece_ + 1 < input_.entries().size() &&
            input_.entries()[piece_ + 1].id == TokenId::EQUAL &&
            input_.entries()[piece_].end == input_.entries()[piece_ + 1].begin) {
            throw IntegerParserError(
                "'==' was removed; use '=' for equality comparison at " +
                sourceLocation(input_.entries()[piece_].begin));
        }
        if (auto pattern = tryParseTrailingPattern(left, minimumPrecedence)) {
            left = std::move(pattern);
            continue;
        }
        const auto definition = infixOperatorForId(input_.entries()[piece_].id);
        if (!definition || static_cast<int>(definition->precedence) < minimumPrecedence) break;
        match(input_.entries()[piece_].id);
        const int nextMinimum = definition->associativity == OperatorAssociativity::Right
            ? static_cast<int>(definition->precedence)
            : static_cast<int>(definition->precedence) + 1;
        auto right = parseBinaryExpression(nextMinimum, stop, stopAnchor);
        left = std::make_shared<OperatorExpression>(definition->id, std::move(left), std::move(right));
    }
    return left;
}

std::shared_ptr<Expr> IntegerParser::parseExpressionText() {
    if (at(TokenId::QUESTION)) {
        throw IntegerParserError(
            "External '?' queries were removed; use --query 'bucket.where(...).' instead");
    }
    try {
        auto result = parseExpression();
        if (!atEnd()) throw IntegerParserError("Unexpected source after expression");
        return result;
    } catch (const IntegerParserIncomplete& error) {
        const std::string message = error.what();
        if (message.find("line ") != std::string::npos) throw;
        throw IntegerParserIncomplete(message + " at " + sourceLocation(byte_));
    } catch (const IntegerParserError& error) {
        const std::string message = error.what();
        if (message.find("line ") != std::string::npos) throw;
        throw IntegerParserError(message + " at " + sourceLocation(byte_));
    }
}

std::shared_ptr<Expr> IntegerParser::parseTerminatedExpressionText() {
    if (at(TokenId::QUESTION)) {
        throw IntegerParserError(
            "External '?' queries were removed; use --query 'bucket.where(...).' instead");
    }
    try {
        auto result = parseExpression();
        consumeStatementTerminator("expression");
        if (!atEnd()) throw IntegerParserError("Unexpected source after expression terminator");
        return result;
    } catch (const IntegerParserIncomplete& error) {
        const std::string message = error.what();
        if (message.find("line ") != std::string::npos) throw;
        throw IntegerParserIncomplete(message + " at " + sourceLocation(byte_));
    } catch (const IntegerParserError& error) {
        const std::string message = error.what();
        if (message.find("line ") != std::string::npos) throw;
        throw IntegerParserError(message + " at " + sourceLocation(byte_));
    }
}

SourceSpan IntegerParser::span(std::size_t begin, std::size_t end) const {
    const auto start = input_.lineColumn(begin);
    const auto finish = input_.lineColumn(end);
    SourceSpan result;
    result.startLine = start.line;
    result.startColumn = start.column;
    result.endLine = finish.line;
    result.endColumn = finish.column;
    return result;
}

void IntegerParser::stamp(const std::shared_ptr<AstNode>& node,
                          std::size_t begin,
                          std::size_t end) const {
    node->sourceSpan = span(begin, end);
}

} // namespace Felidae
