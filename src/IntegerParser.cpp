#include "IntegerParser.h"

#include "MixfixStateModel.h"

#include "BuiltinRegistry.h"
#include "Operator.h"
#include "OperatorAnnotation.h"
#include "SentencePieceModel.h"

#include <sentencepiece.pb.h>
#include <sentencepiece_processor.h>

#include <algorithm>
#include <cctype>
#include <functional>
#include <unordered_map>

namespace Felidae {

namespace {
const SymbolId kMainSymbolId = symbolIdForName("main");

bool isFragmentableBuiltin(TokenId::Id id) {
    switch (id) {
    case TokenId::IMPORT: case TokenId::NOT: case TokenId::AND: case TokenId::OR:
    case TokenId::THEN: case TokenId::AS: case TokenId::IF: case TokenId::ELSE:
    case TokenId::RETURN: case TokenId::WHERE: case TokenId::EXTEND: case TokenId::LAMBDA:
    case TokenId::TRUE: case TokenId::FALSE: case TokenId::NIL:
        return true;
    default:
        return false;
    }
}

std::string pieceFragment(std::string piece) {
    // SentencePiece's whitespace marker has no lexical spelling once
    // IntegerParser has already skipped trivia. Byte-fallback pieces are
    // converted to their original byte so `re` + `turn` and byte fragments
    // participate in the same finite matching table.
    constexpr std::string_view marker = "\xE2\x96\x81";
    if (piece.starts_with(marker)) piece.erase(0, marker.size());
    if (piece.size() == 6 && piece.starts_with("<0x") && piece[5] == '>') {
        const auto digit = [](char value) -> int {
            if (value >= '0' && value <= '9') return value - '0';
            if (value >= 'A' && value <= 'F') return value - 'A' + 10;
            return -1;
        };
        const int high = digit(piece[3]);
        const int low = digit(piece[4]);
        if (high >= 0 && low >= 0) return std::string(1, static_cast<char>((high << 4) | low));
    }
    return piece;
}

const std::vector<std::vector<int>>& builtinPieceSequences(TokenId::Id id) {
    static std::unordered_map<TokenId::Id, std::vector<std::vector<int>>> cache;
    if (const auto found = cache.find(id); found != cache.end()) return found->second;
    auto& sequences = cache[id];
    const auto spelling = builtinTokenSpelling(id);
    if (!isFragmentableBuiltin(id) || spelling.empty()) return sequences;
    const auto& model = felidaeSentencePieceModel();
    std::vector<std::pair<int, std::string>> pieces;
    pieces.reserve(static_cast<std::size_t>(model.GetPieceSize()));
    for (int pieceId = 0; pieceId < model.GetPieceSize(); ++pieceId) {
        auto fragment = pieceFragment(model.IdToPiece(pieceId));
        if (!fragment.empty() && fragment.size() <= spelling.size()) {
            pieces.emplace_back(pieceId, std::move(fragment));
        }
    }
    std::vector<int> current;
    const auto collect = [&](const auto& self, std::size_t offset) -> void {
        if (sequences.size() >= 64) return;
        if (offset == spelling.size()) {
            sequences.push_back(current);
            return;
        }
        for (const auto& [pieceId, fragment] : pieces) {
            if (!spelling.substr(offset).starts_with(fragment)) continue;
            current.push_back(pieceId);
            self(self, offset + fragment.size());
            current.pop_back();
        }
    };
    collect(collect, 0);
    return sequences;
}

bool hasValueReturn(const std::vector<std::shared_ptr<Goal>>& goals) {
    for (const auto& goal : goals) {
        if (const auto returned = std::dynamic_pointer_cast<ReturnGoal>(goal)) {
            if (!returned->fields.empty()) return true;
        }
    }
    return false;
}

bool isMethodStyleHead(const Call& head) {
    if (head.args.empty()) return false;
    for (const auto& argument : head.args) {
        const auto type = std::dynamic_pointer_cast<VarExpr>(argument.value);
        if (argument.name.empty() || !type ||
            (type->languageTypeId == LanguageTypeId::Unknown &&
             !isFelidaeTypeAnnotationName(type->name))) {
            return false;
        }
    }
    return true;
}
} // namespace

IntegerParser::IntegerParser(const IntegerTokenList& input,
                             std::shared_ptr<OperatorRegistry> operators,
                             MixfixStateModel* mixfixModel)
    : input_(input), operators_(std::move(operators)), mixfixModel_(mixfixModel) {
    metrics_.sourceEncodeCount = input.encodeCount();
    metrics_.tokenCount = input.entries().size();
    const auto advance = [](SourceSpan& target, const IntegerTokenList::Entry& entry,
                            std::size_t count) {
        if (entry.id == TokenId::NEWLINE || entry.id == TokenId::CARRIAGE_RETURN) {
            ++target.endLine;
            target.endColumn = 1;
        } else {
            target.endColumn += static_cast<int>(count);
        }
    };
    pieceStarts_.reserve(input.entries().size() + 1);
    SourceSpan cursor;
    for (const auto& entry : input.entries()) {
        pieceStarts_.push_back(cursor);
        advance(cursor, entry, entry.end - entry.begin);
    }
    pieceStarts_.push_back(cursor);
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
    if (++metrics_.iterations > kMaximumIterations) {
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

std::size_t IntegerParser::builtinSequenceLength(TokenId::Id id) const {
    const auto& entries = input_.entries();
    if (piece_ >= entries.size()) return 0;
    if (entries[piece_].id == id) return 1;
    if (!isFragmentableBuiltin(id)) return 0;
    for (const auto& sequence : builtinPieceSequences(id)) {
        if (sequence.size() < 2 || piece_ + sequence.size() > entries.size()) continue;
        std::size_t cursor = byte_;
        bool matches = true;
        for (std::size_t index = 0; index < sequence.size(); ++index) {
            const auto& entry = entries[piece_ + index];
            if (entry.id != sequence[index] || entry.begin > cursor || entry.end <= cursor) {
                matches = false;
                break;
            }
            cursor = entry.end;
        }
        if (matches) return sequence.size();
    }
    return 0;
}

bool IntegerParser::at(TokenId::Id id) {
    skipTrivia();
    return builtinSequenceLength(id) != 0;
}

bool IntegerParser::match(TokenId::Id id) {
    if (!at(id)) return false;
    const auto count = builtinSequenceLength(id);
    const auto& entries = input_.entries();
    byte_ = entries[piece_ + count - 1].end;
    piece_ += count;
    return true;
}

void IntegerParser::require(TokenId::Id id, const char* message) {
    if (!match(id)) {
        throw IntegerParserError(std::string(message) + " at source byte " + std::to_string(byte_));
    }
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
    // `as` is an atomic grammar ID when it stands alone after a fact or query.
    // SentencePiece also emits that same ID as the prefix of identifiers such
    // as `Assessment`; contiguous IDs are one identifier range, never a
    // grammar boundary.
    if (id == TokenId::AS) {
        if (piece_ + 1 < pieces.size() &&
            pieces[piece_ + 1].begin == pieces[piece_].end &&
            !isIdentifierBoundaryId(pieces[piece_ + 1].id)) {
            return true;
        }
        std::size_t following = piece_ + 1;
        while (following < pieces.size() &&
               (pieces[following].id == TokenId::SPACE || pieces[following].id == TokenId::TAB)) {
            ++following;
        }
        if (following < pieces.size() && pieces[following].id == TokenId::COLON) return true;
    }
    return !isBuiltinTokenId(id) && id != TokenId::UNKNOWN;
}

bool IntegerParser::sourceContainsLineBreak(std::size_t begin, std::size_t end) const {
    for (const auto& entry : input_.entries()) {
        if (entry.end <= begin || entry.begin >= end) continue;
        if (entry.id == TokenId::NEWLINE || entry.id == TokenId::CARRIAGE_RETURN) return true;
    }
    return false;
}

bool IntegerParser::lineBreakBeforeNextSignificantPiece() const {
    const auto& entries = input_.entries();
    bool inComment = false;
    for (std::size_t index = piece_; index < entries.size(); ++index) {
        const auto id = entries[index].id;
        if (id == TokenId::NEWLINE || id == TokenId::CARRIAGE_RETURN) return true;
        if (inComment) continue;
        if (id == TokenId::COMMENT) {
            inComment = true;
            continue;
        }
        if (id != TokenId::SPACE && id != TokenId::TAB) return false;
    }
    return false;
}

std::size_t IntegerParser::sourceLineIndent(std::size_t offset) const {
    std::size_t indent = 0;
    bool afterLineBreak = true;
    for (const auto& entry : input_.entries()) {
        if (entry.begin >= offset) break;
        if (entry.id == TokenId::NEWLINE || entry.id == TokenId::CARRIAGE_RETURN) {
            indent = 0;
            afterLineBreak = true;
        } else if (afterLineBreak && entry.id == TokenId::SPACE) {
            ++indent;
        } else if (afterLineBreak && entry.id == TokenId::TAB) {
            indent += 4;
        } else {
            afterLineBreak = false;
        }
    }
    return indent;
}

void IntegerParser::consumeStatementTerminator(std::size_t statementBegin) {
    if (match(TokenId::DOT) || atEnd()) return;
    if (sourceContainsLineBreak(statementBegin, byte_)) return;
    throw IntegerParserError("Expected '.' or newline after statement at source byte " +
                             std::to_string(byte_));
}

std::string IntegerParser::consumeNameRange() {
    skipTrivia();
    if (!atNameRange()) {
        const auto id = piece_ < input_.entries().size() ? input_.entries()[piece_].id : TokenId::UNKNOWN;
        throw IntegerParserError("Expected a SentencePiece name range at source byte " +
                                 std::to_string(byte_) + " (ID " + std::to_string(id) + ")");
    }
    const std::size_t begin = byte_;
    const auto& pieces = input_.entries();
    // A logical name is a contiguous run of non-grammar SentencePiece IDs.
    // SentencePiece may split one source name into many adjacent pieces.
    while (piece_ < pieces.size()) {
        const auto id = pieces[piece_].id;
        if (id == TokenId::UNKNOWN || isIdentifierBoundaryId(id)) break;
        byte_ = pieces[piece_++].end;
    }
    if (byte_ == begin) throw IntegerParserError("Empty SentencePiece name range");
    return input_.source().substr(begin, byte_ - begin);
}

IntegerParser::StringLiteral IntegerParser::consumeString() {
    require(TokenId::QUOTE, "Expected a string literal");
    std::vector<int> ids;
    bool containsEscape = false;
    while (piece_ < input_.entries().size()) {
        const auto id = input_.entries()[piece_].id;
        if (id == TokenId::QUOTE) {
            byte_ = input_.entries()[piece_++].end;
            std::string value;
            const auto status = felidaeSentencePieceModel().Decode(ids, &value);
            if (!status.ok()) throw IntegerParserError("Unable to decode string literal IDs");
            // SentencePiece faithfully decodes the lexical escape marker.  A
            // Felidae string value owns the escaped character instead, so do
            // this value-level normalization after decoding; grammar parsing
            // never falls back to scanning source characters.
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
            StringLiteral literal;
            literal.value = std::move(unescaped);
            literal.containsEscape = containsEscape;
            literal.sentencePieceIds.reserve(ids.size());
            for (const auto piece : ids) {
                if (piece < 0) throw IntegerParserError("SentencePiece emitted an invalid string piece");
                literal.sentencePieceIds.push_back(static_cast<std::uint32_t>(piece));
            }
            return literal;
        }
        if (id == TokenId::BACKSLASH) {
            containsEscape = true;
            ids.push_back(id);
            byte_ = input_.entries()[piece_++].end;
            if (piece_ == input_.entries().size()) throw IntegerParserError("Unterminated string escape");
        }
        ids.push_back(input_.entries()[piece_].id);
        byte_ = input_.entries()[piece_++].end;
    }
    throw IntegerParserError("Unterminated string literal");
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
    if (!at(TokenId::RPAREN)) {
        do {
            skipTrivia();
            const std::size_t before = byte_;
            QualifiedName name;
            bool named = false;
            if (atNameRange()) {
                const auto nameStart = byte_;
                const auto pieceStart = piece_;
                const auto candidate = consumeQualifiedName(false);
                if (match(TokenId::COLON)) {
                    name = candidate;
                    named = true;
                }
                else {
                    byte_ = nameStart;
                    piece_ = pieceStart;
                }
            }
            std::shared_ptr<Expr> value;
            if (allowAnnotationBindings && named && at(TokenId::LBRACKET)) {
                require(TokenId::LBRACKET, "Expected '[' for annotation bindings");
                std::vector<std::shared_ptr<Expr>> bindings;
                if (!at(TokenId::RBRACKET)) {
                    do {
                        const auto binding = consumeQualifiedName(false);
                        require(TokenId::COLON, "Expected ':' after annotation binding name");
                        const auto type = consumeQualifiedName();
                        auto typeExpr = std::make_shared<VarExpr>(
                            type.spelling, type.nameId, languageTypeIdForName(type.spelling),
                            type.isCapitalized);
                        bindings.push_back(std::make_shared<MapExpr>(std::vector<MapEntry>{
                            MapEntry{binding.spelling, binding.nameId, std::move(typeExpr)}}));
                    } while (match(TokenId::COMMA));
                }
                require(TokenId::RBRACKET, "Expected ']' after annotation bindings");
                value = std::make_shared<ArrayExpr>(std::move(bindings));
            } else if (allowAnnotationBindings && named && atNameRange()) {
                const auto valueByte = byte_;
                const auto valuePiece = piece_;
                const auto binding = consumeQualifiedName(false);
                if (match(TokenId::COLON)) {
                    const auto type = consumeQualifiedName();
                    auto typeExpr = std::make_shared<VarExpr>(
                        type.spelling, type.nameId, languageTypeIdForName(type.spelling),
                        type.isCapitalized);
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

IntegerParser::QualifiedName IntegerParser::consumeQualifiedName(bool allowNamespaceSeparators) {
    const auto firstPiece = piece_;
    const bool capitalized = piece_ < input_.entries().size() &&
        isCapitalizedIdentifierStartId(input_.entries()[piece_].id);
    QualifiedName name{consumeNameRange(), 0, BuiltinId::Unknown, capitalized};
    while (at(TokenId::DOT) ||
           (allowNamespaceSeparators && (at(TokenId::COLON) || at(TokenId::DOUBLE_COLON)))) {
        const auto beforeByte = byte_;
        const auto beforePiece = piece_;
        const auto separator = input_.entries()[piece_].id;
        match(separator);
        const auto separatorEnd = byte_;
        if (!atNameRange() || sourceContainsLineBreak(separatorEnd, byte_)) {
            byte_ = beforeByte;
            piece_ = beforePiece;
            break;
        }
        name.spelling += separator == TokenId::DOT ? "." :
                         separator == TokenId::COLON ? ":" : "::";
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
    name.builtinId = builtinIdForPieceIds(ids);
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
        throw IntegerParserError("Annotation method '" + name.spelling + "' requires an argument list");
    }
    return Call(name.spelling, name.nameId, parseArguments(true), name.builtinId);
}

const OperatorPatternDefinition& IntegerParser::registerOperatorPattern(
    OperatorPatternDefinition pattern) {
    if (!operators_) throw IntegerParserError("Operator registry is unavailable");
    // Pattern structure is registered independently from source IDs. Literal
    // anchors are matched against offsets from the one full-source
    // SentencePiece encode; do not encode each anchor again.
    OperatorRegistry::compilePattern(pattern);
    return operators_->registerPattern(std::move(pattern));
}

void IntegerParser::prepareOperatorAnnotation(const Call& annotation) {
    const bool overload = annotation.builtinId == BuiltinId::OverloadAnnotation || annotation.name == "overload";
    const bool mixfix = annotation.builtinId == BuiltinId::MixfixAnnotation || annotation.name == "mixfix";
    const bool matcherAnnotation = annotation.builtinId == BuiltinId::MatcherAnnotation || annotation.name == "matcher";
    if (!operators_ || (!overload && !mixfix && !matcherAnnotation)) return;
    // SentencePiece may fragment an annotation spelling differently while the
    // canonical decoded name remains exact. Normalize the parser-owned call
    // here rather than requiring one tokenization for language syntax.
    Call normalized = annotation;
    if (overload) normalized.builtinId = BuiltinId::OverloadAnnotation;
    else if (mixfix) normalized.builtinId = BuiltinId::MixfixAnnotation;
    else normalized.builtinId = BuiltinId::MatcherAnnotation;
    ParsedOperatorAnnotation parsed;
    try {
        parsed = decodeOperatorAnnotation(normalized);
    } catch (const std::runtime_error& error) {
        throw IntegerParserError(error.what());
    }
    const bool matcher = normalized.builtinId == BuiltinId::MatcherAnnotation;
    const OperatorPatternDefinition* pattern = nullptr;
    try {
        if (parsed.pattern.empty()) {
            pattern = operators_->findPatternByOperator(parsed.operatorName);
            if (!pattern) throw IntegerParserError(matcher
                ? "@matcher requires an operator pattern declared by @overload"
                : "Initial operator overload requires 'pattern'");
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
    if (match(TokenId::IF)) {
        auto conditionExpression = parseBinaryExpression(
            static_cast<int>(OperatorPrecedence::Control), TokenId::THEN);
        require(TokenId::THEN, "Expected 'then' after if condition");
        std::shared_ptr<Goal> condition;
        if (const auto comparison = std::dynamic_pointer_cast<OperatorExpression>(conditionExpression);
            comparison && comparison->captureCount() == 2 &&
            isComparisonOperator(comparison->coreOperator)) {
            condition = std::make_shared<BinaryGoal>(comparison->capture(0),
                coreOperatorDefinition(comparison->coreOperator).token, comparison->capture(1));
        } else {
            condition = std::make_shared<BinaryGoal>(std::move(conditionExpression), TokenId::EQUAL,
                                                     std::make_shared<BoolExpr>(true));
        }
        auto thenBranch = parseGoalList(TokenId::DOT);
        std::vector<std::shared_ptr<Goal>> elseBranch;
        if (match(TokenId::ELSE)) elseBranch = parseGoalList(TokenId::DOT);
        auto result = std::make_shared<IfGoal>(std::move(condition), std::move(thenBranch),
                                               std::move(elseBranch));
        stamp(result, begin, byte_);
        return result;
    }
    if (match(TokenId::WHERE)) {
        auto expression = parseExpression();
        const auto comparison = std::dynamic_pointer_cast<OperatorExpression>(expression);
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
    if (match(TokenId::LPAREN)) {
        auto grouped = parseGoalList(TokenId::RPAREN);
        require(TokenId::RPAREN, "Expected ')' after grouped goals");
        if (grouped.size() == 1) return grouped.front();
        auto result = std::make_shared<GroupGoal>(std::move(grouped));
        stamp(result, begin, byte_);
        return result;
    }
    if (match(TokenId::RETURN)) {
        std::vector<Arg> fields;
        // `match` intentionally skips trivia, therefore this boundary must
        // be observed before probing for the optional parenthesized form.
        const bool terminatedByLineBreak = lineBreakBeforeNextSignificantPiece();
        if (!terminatedByLineBreak && match(TokenId::LPAREN)) {
            if (!at(TokenId::RPAREN)) {
                do {
                    skipTrivia();
                    std::string name;
                    const auto fieldByte = byte_;
                    const auto fieldPiece = piece_;
                    if (atNameRange()) {
                        const auto candidate = consumeNameRange();
                        if (match(TokenId::COLON)) name = candidate;
                        else { byte_ = fieldByte; piece_ = fieldPiece; }
                    }
                    fields.emplace_back(std::move(name), parseExpression());
                } while (match(TokenId::COMMA));
            }
            require(TokenId::RPAREN, "Expected ')' after return fields");
        } else {
            // `return value` is the established method form.  The source is
            // already one SentencePiece stream; this merely assembles the
            // following integer range as an expression rather than leaving it
            // to be misread as the next top-level declaration.
            skipTrivia();
            if (!terminatedByLineBreak && !atEnd() && !at(TokenId::ELSE) && !at(TokenId::DOT)) {
                fields.emplace_back("", parseExpression());
            }
        }
        auto result = std::make_shared<ReturnGoal>(std::move(fields));
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
            auto result = std::make_shared<AssignGoal>(name, parseExpression());
            stamp(result, begin, byte_);
            return result;
        }
    }
    byte_ = start;
    piece_ = startPiece;
    auto left = parseExpression();
    std::vector<QualifiedName> designations;
    if (match(TokenId::AS)) {
        do {
            designations.push_back(consumeQualifiedName());
        } while (match(TokenId::COMMA));
    }
    if (const auto comparison = std::dynamic_pointer_cast<OperatorExpression>(left);
        comparison && comparison->captureCount() == 2 &&
        isComparisonOperator(comparison->coreOperator)) {
        const auto definition = coreOperatorDefinition(comparison->coreOperator);
        auto result = std::make_shared<BinaryGoal>(comparison->capture(0), definition.token,
                                                   comparison->capture(1));
        stamp(result, begin, byte_);
        return result;
    }
    skipTrivia();
    if (piece_ < input_.entries().size() && input_.entries()[piece_].begin == byte_) {
        const auto definition = infixOperatorForId(input_.entries()[piece_].id);
        if (definition && isComparisonOperator(definition->id)) {
            match(input_.entries()[piece_].id);
            auto result = std::make_shared<BinaryGoal>(std::move(left), definition->token, parseExpression());
            stamp(result, begin, byte_);
            return result;
        }
    }
    const auto term = std::dynamic_pointer_cast<TermExpr>(left);
    if (!term) throw IntegerParserError("Expected a predicate call or comparison goal");
    Call call(term->name, term->nameId, term->args, term->builtinId);
    for (auto& designation : designations) {
        call.designations.push_back(std::move(designation.spelling));
        call.designationIds.push_back(designation.nameId);
    }
    auto result = std::make_shared<CallGoal>(std::move(call));
    stamp(result, begin, byte_);
    return result;
}

std::vector<std::shared_ptr<Goal>> IntegerParser::parseGoalList(TokenId::Id terminator) {
    std::vector<std::shared_ptr<Goal>> goals;
    if (at(terminator)) return goals;
    std::size_t bodyIndent = 0;
    bool hasBodyIndent = false;
    do {
        // Measure indentation at the first significant ID, not at the
        // preceding arrow/terminator.  This keeps a bare return from pulling
        // the next top-level declaration into its method body.
        skipTrivia();
        const auto before = byte_;
        if (!hasBodyIndent) {
            bodyIndent = sourceLineIndent(before);
            hasBodyIndent = true;
        }
        goals.push_back(parseGoal());
        if (byte_ == before) throw IntegerParserError("Integer parser made no progress in goal list");
        if (const auto returned = std::dynamic_pointer_cast<ReturnGoal>(goals.back());
            returned && returned->fields.empty() && sourceContainsLineBreak(before, byte_)) {
            break;
        }
        if (match(TokenId::COMMA)) continue;
        if (atEnd() || at(terminator) || at(TokenId::ELSE)) break;
        if (!sourceContainsLineBreak(before, byte_) ||
            sourceLineIndent(byte_) < bodyIndent) break;
        if (bodyIndent == 0) {
            // Zero-indented multi-line method bodies are supported for
            // compatibility.  They are ambiguous with the next top-level
            // declaration, so detect only unambiguous statement starters
            // without re-tokenizing source: annotations/imports, a callable
            // clause head. An assignment remains part of this body: without
            // indentation, a local assignment and a following global binding
            // are otherwise indistinguishable, and method-local semantics
            // take precedence until an explicit declaration boundary.
            if (at(TokenId::AT) || at(TokenId::IMPORT)) break;
            const auto statementByte = byte_;
            const auto statementPiece = piece_;
            bool topLevelStatement = false;
            if (atNameRange()) {
                (void)consumeQualifiedName();
                topLevelStatement = at(TokenId::LPAREN);
            }
            byte_ = statementByte;
            piece_ = statementPiece;
            if (topLevelStatement) break;
        }
    } while (true);
    return goals;
}

std::shared_ptr<Statement> IntegerParser::parseStatement() {
    skipTrivia();
    const std::size_t begin = byte_;
    std::vector<Call> annotations;
    while (at(TokenId::AT)) {
        annotations.push_back(parseAnnotation());
        skipTrivia();
    }
    for (const auto& annotation : annotations) prepareOperatorAnnotation(annotation);
    if (match(TokenId::IMPORT)) {
        if (!annotations.empty()) throw IntegerParserError("Annotations can only be applied to method declarations");
        std::vector<std::string> paths;
        if (match(TokenId::LPAREN)) {
            if (!at(TokenId::RPAREN)) {
                do { paths.push_back(consumeString().value); } while (match(TokenId::COMMA));
            }
            require(TokenId::RPAREN, "Expected ')' after import paths");
        } else {
            paths.push_back(consumeString().value);
        }
        consumeStatementTerminator(begin);
        auto result = std::make_shared<ImportStmt>(std::move(paths));
        stamp(result, begin, byte_);
        return result;
    }
    const auto checkpoint = byte_;
    const auto checkpointPiece = piece_;
    if (atNameRange()) {
        const auto name = consumeNameRange();
        if (match(TokenId::ASSIGN)) {
            if (!annotations.empty()) throw IntegerParserError("Annotations can only be applied to method declarations");
            auto result = std::make_shared<GlobalBindingStmt>(name, parseExpression());
            consumeStatementTerminator(begin);
            stamp(result, begin, byte_);
            return result;
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
                                 "' at source byte " + std::to_string(byte_));
    }
    Call head(clauseName.spelling, clauseName.nameId, parseArguments(), clauseName.builtinId);
    if (match(TokenId::AS)) {
        do {
            const auto designation = consumeQualifiedName();
            head.designations.push_back(designation.spelling);
            head.designationIds.push_back(designation.nameId);
        } while (match(TokenId::COMMA));
    }
    std::vector<std::shared_ptr<Goal>> body;
    std::vector<std::vector<std::shared_ptr<Goal>>> fallbackBranches;
    bool emptyDeclaration = false;
    if (match(TokenId::ARROW)) {
        if (match(TokenId::LPAREN)) {
            require(TokenId::RPAREN, "Expected ')' after empty declaration");
            emptyDeclaration = true;
        } else if (match(TokenId::LBRACE)) {
            require(TokenId::RBRACE, "Expected '}' after empty declaration");
            emptyDeclaration = true;
        } else {
            body = parseGoalList(TokenId::DOT);
            while (match(TokenId::ELSE)) {
                const auto beforeBranch = byte_;
                auto branch = parseGoalList(TokenId::DOT);
                if (branch.empty() || byte_ == beforeBranch) {
                    throw IntegerParserError("Expected fallback branch after 'else'");
                }
                fallbackBranches.push_back(std::move(branch));
            }
        }
    }
    consumeStatementTerminator(begin);
    // Annotations describe callable operator implementations.  They are
    // methods even when their body has a bare `return` (or no value return),
    // so classification must not depend solely on ReturnGoal fields.
    const ClauseKind kind = emptyDeclaration ? ClauseKind::NativeDeclaration :
        (head.nameId == kMainSymbolId || !annotations.empty() || isMethodStyleHead(head) || hasValueReturn(body) || !fallbackBranches.empty() ? ClauseKind::Method :
         body.empty() ? ClauseKind::Fact : ClauseKind::Rule);
    auto result = std::make_shared<ClauseStmt>(std::move(head), std::move(parentNames), std::move(body),
                                               std::move(fallbackBranches),
                                               emptyDeclaration, kind);
    result->designations = result->head.designations;
    result->designationIds = result->head.designationIds;
    if (!annotations.empty() && result->clauseKind != ClauseKind::Method) {
        throw IntegerParserError("Annotations can only be applied to complete method declarations");
    }
    result->annotations = std::move(annotations);
    for (const auto& annotation : result->annotations) registerOperatorImplementation(annotation, *result);
    stamp(result, begin, byte_);
    return result;
}

Program IntegerParser::parseProgram() {
    Program program;
    while (!atEnd()) {
        const auto before = byte_;
        program.addStatement(parseStatement());
        ++metrics_.statementCount;
        if (byte_ == before) throw IntegerParserError("Integer parser made no progress in program");
    }
    return program;
}

std::vector<std::shared_ptr<Goal>> IntegerParser::parseQuery() {
    match(TokenId::QUESTION);
    auto goals = parseGoalList(TokenId::DOT);
    if (match(TokenId::DOT) && !atEnd()) {
        throw IntegerParserError("Unexpected source after query terminator");
    }
    if (!atEnd()) throw IntegerParserError("Expected end of query");
    return goals;
}

bool IntegerParser::startsQuery() {
    return at(TokenId::QUESTION);
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
    if (at(TokenId::QUOTE)) {
        auto literal = consumeString();
        auto result = std::make_shared<StringExpr>(std::move(literal.value),
                                                   std::move(literal.sentencePieceIds),
                                                   literal.containsEscape);
        stamp(result, begin, byte_);
        return result;
    }
    if (piece_ < input_.entries().size() && isDecimalDigitId(input_.entries()[piece_].id)) {
        auto number = consumeNumber();
        // A postfix percent is a numeric literal, not modulo: `75%` is the
        // threshold 0.75. `%` remains modulo whenever a right operand follows.
        const auto beforePercentByte = byte_;
        const auto beforePercentPiece = piece_;
        // Percent is postfix only when immediately attached to its number.
        // Whitespace-delimited `%` remains the binary modulo operator.
        if (byte_ > 0 && byte_ < input_.source().size() &&
            input_.source()[byte_ - 1] != ' ' && input_.source()[byte_ - 1] != '\t' &&
            input_.source()[byte_] == '%' &&
            piece_ < input_.entries().size() && input_.entries()[piece_].id == TokenId::PERCENT) {
            byte_ = input_.entries()[piece_++].end;
            const bool hasRightOperand = (piece_ < input_.entries().size() && isDecimalDigitId(input_.entries()[piece_].id)) ||
                atNameRange() || at(TokenId::LPAREN) || at(TokenId::LBRACKET) || at(TokenId::LBRACE);
            if (hasRightOperand) { byte_ = beforePercentByte; piece_ = beforePercentPiece; }
            else number /= 100.0;
        }
        auto result = std::make_shared<NumberExpr>(number);
        stamp(result, begin, byte_);
        return result;
    }
    if (match(TokenId::TRUE)) { auto result = std::make_shared<BoolExpr>(true); stamp(result, begin, byte_); return result; }
    if (match(TokenId::FALSE)) { auto result = std::make_shared<BoolExpr>(false); stamp(result, begin, byte_); return result; }
    if (match(TokenId::NIL)) { auto result = std::make_shared<NilExpr>(); stamp(result, begin, byte_); return result; }
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
        auto result = parseExpression();
        require(TokenId::RPAREN, "Expected ')' after grouped expression");
        stamp(result, begin, byte_);
        return result;
    }
    if (atNameRange()) {
        const auto name = consumeQualifiedName();
        if (at(TokenId::LPAREN)) {
            auto result = std::make_shared<TermExpr>(name.spelling, name.nameId, parseArguments(),
                                                     name.builtinId, name.isCapitalized);
            stamp(result, begin, byte_);
            return result;
        }
        auto result = std::make_shared<VarExpr>(name.spelling, name.nameId,
                                                languageTypeIdForName(name.spelling),
                                                name.isCapitalized);
        stamp(result, begin, byte_);
        return result;
    }
    throw IntegerParserError("Expected an expression at source byte " +
                             std::to_string(byte_));
}

std::shared_ptr<Expr> IntegerParser::parseExpression() {
    return parseBinaryExpression(static_cast<int>(OperatorPrecedence::Control));
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
    const auto& entries = input_.entries();
    const auto savedByte = byte_;
    const auto savedPiece = piece_;
    skipTrivia();
    std::size_t wanted = 0;
    while (!lexeme.pieceIds.empty() && wanted < lexeme.pieceIds.size()) {
        if (piece_ >= entries.size() || entries[piece_].begin > byte_ ||
            entries[piece_].end <= byte_) {
            break;
        }
        if (entries[piece_].id != lexeme.pieceIds[wanted]) {
            break;
        }
        ++wanted;
        byte_ = entries[piece_++].end;
    }
    if (!lexeme.pieceIds.empty() && wanted == lexeme.pieceIds.size()) return true;

    // Anchors are registered once, but SentencePiece IDs are emitted from one
    // complete source encode.  The same spelling may therefore be one piece,
    // several pieces, or carry a neighbouring whitespace piece.  Use the
    // original encode offsets to recognize the registered literal spelling;
    // this is not a second tokenizer or a string grammar.
    byte_ = savedByte;
    piece_ = savedPiece;
    skipTrivia();
    const auto start = byte_;
    const auto end = start + lexeme.spelling.size();
    const auto& source = input_.source();
    if (end > source.size() || source.compare(start, lexeme.spelling.size(), lexeme.spelling) != 0) {
        byte_ = savedByte;
        piece_ = savedPiece;
        return false;
    }
    // A word-shaped anchor must end at a source identifier boundary. Without
    // this, `plan` could incorrectly assemble inside `planar` when the model
    // fragments that identifier differently from a declaration anchor.
    const auto isIdentifierByte = [](unsigned char value) {
        return std::isalnum(value) != 0 || value == '_';
    };
    if (!lexeme.spelling.empty() && isIdentifierByte(static_cast<unsigned char>(lexeme.spelling.back())) &&
        end < source.size() && isIdentifierByte(static_cast<unsigned char>(source[end]))) {
        byte_ = savedByte;
        piece_ = savedPiece;
        return false;
    }
    while (piece_ < entries.size() && entries[piece_].end <= end) ++piece_;
    if (piece_ < entries.size() && entries[piece_].begin < end) {
        byte_ = savedByte;
        piece_ = savedPiece;
        return false;
    }
    byte_ = end;
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
    std::shared_ptr<OperatorExpression> selected;
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
            auto candidate = std::make_shared<OperatorExpression>(
                pattern.operatorId, pattern.patternId, std::move(captures));
            candidate->resolvedMethodId = resolveMixfixMethod(*candidate);
            if (candidate->resolvedMethodId == 0 && mixfixModel_) {
                candidate->resolvedMethodId = resolveModelMixfixMethod(candidate);
            }
            stamp(candidate, startByte, byte_);
            if (!selected || byte_ > selectedByte ||
                (byte_ == selectedByte && candidate->resolvedMethodId != 0 &&
                 selected->resolvedMethodId == 0)) {
                selected = std::move(candidate);
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
    const auto leftSpan = left->sourceSpan;
    const auto startByte = byte_;
    const auto startPiece = piece_;
    const auto stampTrailing = [&](const std::shared_ptr<OperatorExpression>& expression,
                                   std::size_t endByte) {
        expression->sourceSpan = leftSpan;
        const auto ending = span(startByte, endByte);
        expression->sourceSpan.endLine = ending.endLine;
        expression->sourceSpan.endColumn = ending.endColumn;
    };
    std::shared_ptr<OperatorExpression> immediate;
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
                immediate->resolvedMethodId = resolveMixfixMethod(*immediate);
                if (immediate->resolvedMethodId == 0 && mixfixModel_) {
                    immediate->resolvedMethodId = resolveModelMixfixMethod(immediate);
                }
                stampTrailing(immediate, byte_);
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
        std::shared_ptr<OperatorExpression> deferred;
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
                    deferred->resolvedMethodId = resolveMixfixMethod(*deferred);
                    if (deferred->resolvedMethodId == 0 && mixfixModel_) {
                        deferred->resolvedMethodId = resolveModelMixfixMethod(deferred);
                    }
                    stampTrailing(deferred, byte_);
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
                                         selected->operatorName + "' at source byte " +
                                         std::to_string(byte_));
            }
        }
    }
    auto expression = std::make_shared<OperatorExpression>(selected->operatorId, selected->patternId,
                                                            std::move(captures));
    expression->resolvedMethodId = resolveMixfixMethod(*expression);
    if (expression->resolvedMethodId == 0 && mixfixModel_) {
        expression->resolvedMethodId = resolveModelMixfixMethod(expression);
    }
    stampTrailing(expression, byte_);
    return expression;
}

std::shared_ptr<Expr> IntegerParser::parseUnary() {
    if (auto pattern = tryParseLeadingPattern()) return pattern;
    if (match(TokenId::NOT)) return std::make_shared<OperatorExpression>(CoreOperator::LogicalNot, parseUnary());
    if (match(TokenId::MINUS)) {
        auto operand = parseUnary();
        if (const auto number = std::dynamic_pointer_cast<NumberExpr>(operand)) {
            return std::make_shared<NumberExpr>(-number->value);
        }
        return std::make_shared<OperatorExpression>(CoreOperator::UnaryMinus, std::move(operand));
    }
    if (match(TokenId::PLUS)) return parseUnary();
    auto result = parsePrimary();
    while (at(TokenId::DOT) || at(TokenId::COLON)) {
        const auto beforeByte = byte_;
        const auto beforePiece = piece_;
        const auto separator = input_.entries()[piece_].id;
        match(separator);
        const auto separatorEnd = byte_;
        if (!atNameRange() || sourceContainsLineBreak(separatorEnd, byte_)) {
            byte_ = beforeByte;
            piece_ = beforePiece;
            break;
        }
        result = std::make_shared<AccessExpr>(std::move(result), consumeNameRange());
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
    auto result = parseExpression();
    if (!atEnd()) throw IntegerParserError("Unexpected source after expression");
    return result;
}

FelidaeIr IntegerParser::compileExpressionIr() {
    const auto expression = parseExpressionText();
    const auto hasMixfix = [](const auto& self, const std::shared_ptr<Expr>& node) -> bool {
        if (const auto operation = std::dynamic_pointer_cast<OperatorExpression>(node)) {
            if (operation->coreOperator == CoreOperator::Unknown) return true;
            for (std::size_t index = 0; index < operation->captureCount(); ++index) {
                if (self(self, operation->capture(index))) return true;
            }
        } else if (const auto array = std::dynamic_pointer_cast<ArrayExpr>(node)) {
            for (const auto& item : array->items) if (self(self, item)) return true;
        } else if (const auto map = std::dynamic_pointer_cast<MapExpr>(node)) {
            for (const auto& entry : map->entries) if (self(self, entry.value)) return true;
        } else if (const auto access = std::dynamic_pointer_cast<AccessExpr>(node)) {
            return self(self, access->target);
        } else if (const auto term = std::dynamic_pointer_cast<TermExpr>(node)) {
            for (const auto& argument : term->args) if (self(self, argument.value)) return true;
        }
        return false;
    };
    if (hasMixfix(hasMixfix, expression)) {
        if (!mixfixModel_) {
            throw IntegerParserError("mixfix expression requires the verified MixfixStateModel compiler backend");
        }
        return compileModelRoutedMixfixExpressionIr(expression);
    }
    return compileAstExpressionIr(expression);
}

FelidaeIr IntegerParser::compileModelRoutedMixfixExpressionIr(
    const std::shared_ptr<Expr>& expression) const {
    // This vocabulary is deliberately fixed-size and structural. Dynamic
    // literals/symbols are collected into bounded parser-owned tables below;
    // model output can reference them but never manufacture a machine word.
    FelidaeIr shell;
    shell.registerCount = kMaximumMixfixRegisters;
    const auto addConstant = [&](IrConstantKind kind, IrWord value,
                                 std::vector<std::uint32_t> text = {}) {
        if (shell.constants.size() >= kMaximumMixfixReferences) {
            throw IntegerParserError("mixfix compiler context has too many constants");
        }
        if (kind == IrConstantKind::Text) {
            shell.texts.push_back(std::move(text));
            value = shell.texts.size() - 1;
        }
        shell.constants.push_back(value);
        shell.constantKinds.push_back(kind);
    };
    const auto addSymbol = [&](SymbolId symbol) {
        if (std::find(shell.symbols.begin(), shell.symbols.end(), symbol) != shell.symbols.end()) return;
        if (shell.symbols.size() >= kMaximumMixfixReferences) {
            throw IntegerParserError("mixfix compiler context has too many symbols");
        }
        shell.symbols.push_back(symbol);
    };
    const auto collect = [&](const auto& self, const std::shared_ptr<Expr>& node) -> void {
        if (const auto number = std::dynamic_pointer_cast<NumberExpr>(node)) {
            addConstant(IrConstantKind::Number, encodeIrNumber(number->value));
        } else if (const auto boolean = std::dynamic_pointer_cast<BoolExpr>(node)) {
            addConstant(IrConstantKind::Boolean, boolean->value ? 1 : 0);
        } else if (std::dynamic_pointer_cast<NilExpr>(node)) {
            addConstant(IrConstantKind::Nil, 0);
        } else if (const auto text = std::dynamic_pointer_cast<StringExpr>(node)) {
            if (text->containsEscape || text->sentencePieceIds.empty()) {
                throw IntegerParserError("string literal cannot be lowered without its original SentencePiece IDs");
            }
            addConstant(IrConstantKind::Text, 0, text->sentencePieceIds);
        } else if (const auto variable = std::dynamic_pointer_cast<VarExpr>(node)) {
            addSymbol(variable->nameId);
        } else if (const auto array = std::dynamic_pointer_cast<ArrayExpr>(node)) {
            for (const auto& item : array->items) self(self, item);
        } else if (const auto map = std::dynamic_pointer_cast<MapExpr>(node)) {
            if (!map->factType.empty()) addSymbol(symbolIdForName(map->factType));
            for (const auto& entry : map->entries) { addSymbol(entry.keyId); self(self, entry.value); }
        } else if (const auto access = std::dynamic_pointer_cast<AccessExpr>(node)) {
            self(self, access->target); addSymbol(access->keyId);
        } else if (const auto term = std::dynamic_pointer_cast<TermExpr>(node)) {
            addSymbol(term->nameId);
            for (const auto& argument : term->args) { if (!argument.name.empty()) addSymbol(argument.nameId); self(self, argument.value); }
        } else if (const auto operation = std::dynamic_pointer_cast<OperatorExpression>(node)) {
            for (std::size_t index = 0; index < operation->captureCount(); ++index) self(self, operation->capture(index));
        }
    };
    collect(collect, expression);
    if (const auto operation = std::dynamic_pointer_cast<OperatorExpression>(expression)) {
        for (const auto* overload : operators_->overloadsForPattern(operation->patternId)) {
            addSymbol(overload->methodId);
        }
    }

    MixfixContext context = makeMixfixContext(shell);

    const auto before = [](const SourceSpan& left, const SourceSpan& right) {
        return left.endLine < right.startLine ||
            (left.endLine == right.startLine && left.endColumn <= right.startColumn);
    };
    std::size_t firstPiece = 0;
    while (firstPiece < input_.entries().size() && before(pieceStarts_[firstPiece], expression->sourceSpan)) ++firstPiece;
    std::size_t pastLastPiece = firstPiece;
    while (pastLastPiece < input_.entries().size() &&
           !(expression->sourceSpan.endLine < pieceStarts_[pastLastPiece].startLine ||
             (expression->sourceSpan.endLine == pieceStarts_[pastLastPiece].startLine &&
              expression->sourceSpan.endColumn <= pieceStarts_[pastLastPiece].startColumn))) {
        ++pastLastPiece;
    }
    if (firstPiece == pastLastPiece) {
        throw IntegerParserError("mixfix compiler expression has no bounded SentencePiece source span");
    }
    return compileVerifiedMixfixSpanIr(*mixfixModel_, context, std::move(shell), firstPiece, pastLastPiece);
}

FelidaeIr IntegerParser::compileVerifiedMixfixSpanIr(MixfixStateModel& model,
                                                      const MixfixContext& context,
                                                      FelidaeIr irShell,
                                                      std::size_t firstPiece,
                                                      std::size_t pastLastPiece) const {
    const auto& entries = input_.entries();
    if (firstPiece >= pastLastPiece || pastLastPiece > entries.size()) {
        throw IntegerParserError("mixfix compiler span is outside the SentencePiece input");
    }
    std::vector<SentencePieceId> ids;
    ids.reserve(pastLastPiece - firstPiece);
    for (std::size_t index = firstPiece; index < pastLastPiece; ++index) {
        ids.push_back(entries[index].id);
    }
    try {
        return compileVerifiedMixfixIr(model, ids, context, std::move(irShell));
    } catch (const IrError& error) {
        throw IntegerParserError(std::string("mixfix compiler rejected span: ") + error.what());
    }
}

void IntegerParser::registerOperatorImplementation(const Call& annotation,
                                                    const ClauseStmt& method) {
    if (!operators_) return;
    const bool overload = annotation.builtinId == BuiltinId::OverloadAnnotation || annotation.name == "overload";
    const bool mixfix = annotation.builtinId == BuiltinId::MixfixAnnotation || annotation.name == "mixfix";
    if (!overload && !mixfix) return;
    Call normalized = annotation;
    normalized.builtinId = mixfix ? BuiltinId::MixfixAnnotation : BuiltinId::OverloadAnnotation;
    try {
        const auto parsed = decodeOperatorAnnotation(normalized);
        const auto* pattern = parsed.pattern.empty()
            ? operators_->findPatternByOperator(parsed.operatorName)
            : operators_->findPattern(parsed.operatorName, parsed.pattern);
        if (!pattern) throw IntegerParserError("operator implementation has no registered pattern");
        operators_->registerOverload(makeOperatorOverloadDefinition(
            parsed, *pattern, method.head.name, method.head.nameId, method.module));
    } catch (const std::runtime_error& error) {
        throw IntegerParserError(error.what());
    }
}

SymbolId IntegerParser::resolveMixfixMethod(const OperatorExpression& expression) const {
    if (!operators_) return 0;
    const auto expressionType = [](const std::shared_ptr<Expr>& expression) {
        if (std::dynamic_pointer_cast<NumberExpr>(expression)) return LanguageTypeId::Number;
        if (std::dynamic_pointer_cast<StringExpr>(expression)) return LanguageTypeId::String;
        if (std::dynamic_pointer_cast<BoolExpr>(expression)) return LanguageTypeId::Bool;
        if (std::dynamic_pointer_cast<ArrayExpr>(expression)) return LanguageTypeId::Array;
        if (const auto variable = std::dynamic_pointer_cast<VarExpr>(expression)) return variable->languageTypeId;
        return LanguageTypeId::Unknown;
    };
    std::vector<const OperatorOverloadDefinition*> matches;
    for (const auto* candidate : operators_->overloadsForPattern(expression.patternId)) {
        if (candidate->captures.size() != expression.captureCount()) continue;
        bool compatible = true;
        for (std::size_t index = 0; index < expression.captureCount(); ++index) {
            const auto wanted = candidate->captures[index].languageTypeId;
            const auto actual = expressionType(expression.capture(index));
            // `expr` is the structural capture category used by mixfix
            // patterns. It deliberately accepts every parsed expression
            // shape (literal, fact, variable, nested mixfix, and so on),
            // rather than acting as a concrete runtime value type.
            if (wanted != LanguageTypeId::Unknown && wanted != LanguageTypeId::Any &&
                wanted != LanguageTypeId::Expr &&
                actual != LanguageTypeId::Unknown && actual != wanted &&
                !(wanted == LanguageTypeId::Number &&
                  (actual == LanguageTypeId::Int || actual == LanguageTypeId::Float ||
                   actual == LanguageTypeId::Double || actual == LanguageTypeId::Decimal))) {
                compatible = false;
                break;
            }
        }
        if (compatible) matches.push_back(candidate);
    }
    return matches.size() == 1 ? matches.front()->methodId : 0;
}

SymbolId IntegerParser::resolveModelMixfixMethod(
    const std::shared_ptr<OperatorExpression>& expression) const {
    const auto ir = compileModelRoutedMixfixExpressionIr(expression);
    std::unordered_set<SymbolId> selected;
    for (std::size_t pc = 0; pc < ir.words.size();) {
        const auto opcode = static_cast<IrOpcode>(ir.words[pc]);
        if (opcode == IrOpcode::Call || opcode == IrOpcode::CallNamed) {
            const auto index = static_cast<std::size_t>(ir.words[pc + 2]);
            if (index >= ir.symbols.size()) throw IntegerParserError("mixfix model call has an invalid symbol reference");
            selected.insert(ir.symbols[index]);
        }
        switch (opcode) {
        case IrOpcode::End: ++pc; break;
        case IrOpcode::Call: case IrOpcode::SemanticEval: case IrOpcode::SsmProcess: case IrOpcode::MakeArray:
            pc += 4 + ir.words[pc + 3]; break;
        case IrOpcode::CallNamed: case IrOpcode::MakeMap:
            pc += 4 + ir.words[pc + 3] * 2; break;
        case IrOpcode::Jump: pc += 2; break;
        case IrOpcode::LoadConst: case IrOpcode::LoadSymbol: case IrOpcode::StoreSymbol: case IrOpcode::Move:
        case IrOpcode::JumpIfFalse: case IrOpcode::CallNative: case IrOpcode::MakeFact: case IrOpcode::Return: pc += 3; break;
        case IrOpcode::ForEachFact: case IrOpcode::Add: case IrOpcode::Sub: case IrOpcode::Mul: case IrOpcode::Div:
        case IrOpcode::Mod: case IrOpcode::GetField: case IrOpcode::SetField: case IrOpcode::Similarity: pc += 4; break;
        case IrOpcode::Membership: pc += 6; break;
        case IrOpcode::Compare: pc += 5; break;
        case IrOpcode::Count: throw IntegerParserError("mixfix model emitted an invalid opcode");
        }
    }
    if (selected.size() != 1) {
        throw IntegerParserError("mixfix model must emit exactly one target procedure call");
    }
    return *selected.begin();
}

FelidaeIr IntegerParser::compileAstExpressionIr(const std::shared_ptr<Expr>& expression,
                                                 const std::unordered_set<SymbolId>& factTypes) {
    FelidaeIr ir;
    const auto addNumber = [&](double number) {
        ir.constants.push_back(encodeIrNumber(number));
        ir.constantKinds.push_back(IrConstantKind::Number);
        return static_cast<IrWord>(ir.constants.size() - 1);
    };
    const auto addBoolean = [&](bool boolean) {
        ir.constants.push_back(boolean ? 1 : 0);
        ir.constantKinds.push_back(IrConstantKind::Boolean);
        return static_cast<IrWord>(ir.constants.size() - 1);
    };
    const auto addText = [&](const StringExpr& text) {
        if (text.containsEscape || text.sentencePieceIds.empty()) {
            throw IntegerParserError("string literal cannot be lowered without its original SentencePiece IDs");
        }
        ir.texts.push_back(text.sentencePieceIds);
        ir.constants.push_back(static_cast<IrWord>(ir.texts.size() - 1));
        ir.constantKinds.push_back(IrConstantKind::Text);
        return static_cast<IrWord>(ir.constants.size() - 1);
    };
    const auto addNil = [&]() {
        ir.constants.push_back(0);
        ir.constantKinds.push_back(IrConstantKind::Nil);
        return static_cast<IrWord>(ir.constants.size() - 1);
    };
    auto lower = [&](auto&& self, const std::shared_ptr<Expr>& value) -> RegisterId {
        const auto emittedAt = ir.words.size();
        const auto result = static_cast<RegisterId>(ir.registerCount++);
        if (const auto number = std::dynamic_pointer_cast<NumberExpr>(value)) {
            ir.words.insert(ir.words.end(), {static_cast<IrWord>(IrOpcode::LoadConst), result,
                                             addNumber(number->value)});
        } else if (const auto boolean = std::dynamic_pointer_cast<BoolExpr>(value)) {
            ir.words.insert(ir.words.end(), {static_cast<IrWord>(IrOpcode::LoadConst), result,
                                             addBoolean(boolean->value)});
        } else if (const auto text = std::dynamic_pointer_cast<StringExpr>(value)) {
            ir.words.insert(ir.words.end(), {static_cast<IrWord>(IrOpcode::LoadConst), result,
                                             addText(*text)});
        } else if (std::dynamic_pointer_cast<NilExpr>(value)) {
            ir.words.insert(ir.words.end(), {static_cast<IrWord>(IrOpcode::LoadConst), result,
                                             addNil()});
        } else if (const auto variable = std::dynamic_pointer_cast<VarExpr>(value)) {
            ir.symbols.push_back(variable->nameId);
            ir.words.insert(ir.words.end(), {static_cast<IrWord>(IrOpcode::LoadSymbol), result,
                                             static_cast<IrWord>(ir.symbols.size() - 1)});
        } else if (const auto term = std::dynamic_pointer_cast<TermExpr>(value)) {
            if (term->nameId == symbolIdForName("for_each_fact")) {
                if (term->args.size() != 2) throw IntegerParserError("for_each_fact requires a fact type and callback");
                const auto type = std::dynamic_pointer_cast<VarExpr>(term->args[0].value);
                const auto callback = std::dynamic_pointer_cast<VarExpr>(term->args[1].value);
                if (!type || !callback || !factTypes.contains(type->nameId)) {
                    throw IntegerParserError("for_each_fact requires a declared fact type and deterministic callback");
                }
                ir.symbols.push_back(type->nameId);
                const auto typeSymbol = static_cast<IrWord>(ir.symbols.size() - 1);
                ir.symbols.push_back(callback->nameId);
                ir.words.insert(ir.words.end(), {static_cast<IrWord>(IrOpcode::ForEachFact), result, typeSymbol,
                                                 static_cast<IrWord>(ir.symbols.size() - 1)});
            } else if (term->nameId == symbolIdForName("similarity")) {
                if (term->args.size() != 2) throw IntegerParserError("similarity requires exactly two arguments");
                const auto left = self(self, term->args[0].value);
                const auto right = self(self, term->args[1].value);
                ir.words.insert(ir.words.end(), {static_cast<IrWord>(IrOpcode::Similarity), result, left, right});
            } else if (term->nameId == symbolIdForName("membership")) {
                if (term->args.size() != 2) throw IntegerParserError("membership requires a value and named profile");
                const auto valueRegister = self(self, term->args[0].value);
                const auto profile = self(self, term->args[1].value);
                const auto field = [&](const char* name) {
                    const auto fieldRegister = static_cast<RegisterId>(ir.registerCount++);
                    ir.symbols.push_back(symbolIdForName(name));
                    ir.words.insert(ir.words.end(), {static_cast<IrWord>(IrOpcode::GetField), fieldRegister, profile,
                                                     static_cast<IrWord>(ir.symbols.size() - 1)});
                    return fieldRegister;
                };
                const auto peak = field("peak");
                const auto fadesIn = field("fades_in");
                const auto fadesOut = field("fades_out");
                ir.words.insert(ir.words.end(), {static_cast<IrWord>(IrOpcode::Membership), result, valueRegister, peak, fadesIn, fadesOut});
            } else {
            std::vector<RegisterId> arguments;
            arguments.reserve(term->args.size());
            const bool hasNamedArguments = std::any_of(term->args.begin(), term->args.end(),
                [](const Arg& argument) { return !argument.name.empty(); });
            std::vector<IrWord> names;
            names.reserve(term->args.size());
            for (const auto& argument : term->args) {
                arguments.push_back(self(self, argument.value));
                if (hasNamedArguments && argument.name.empty()) {
                    names.push_back(0);
                } else if (hasNamedArguments) {
                    ir.symbols.push_back(argument.nameId);
                    names.push_back(static_cast<IrWord>(ir.symbols.size()));
                }
            }
            // Capitalized terms with named fields are first-class fact values
            // even without a separately declared fact schema. The AST carries
            // this grammar decision; lowering does not infer it from text.
            const bool factValue = factTypes.contains(term->nameId) ||
                (term->isCapitalized && hasNamedArguments);
            if (factValue) {
                if (!hasNamedArguments) {
                    throw IntegerParserError("fact construction requires named fields");
                }
                ir.symbols.push_back(term->nameId);
                ir.words.insert(ir.words.end(), {static_cast<IrWord>(IrOpcode::MakeFact), result,
                                                 static_cast<IrWord>(ir.symbols.size() - 1)});
                for (std::size_t index = 0; index < arguments.size(); ++index) {
                    if (names[index] == 0) throw IntegerParserError("fact construction requires named fields");
                    ir.words.insert(ir.words.end(), {static_cast<IrWord>(IrOpcode::SetField), result,
                                                     names[index] - 1, arguments[index]});
                }
            } else {
                ir.symbols.push_back(term->nameId);
                if (!hasNamedArguments) {
                ir.words.insert(ir.words.end(), {static_cast<IrWord>(IrOpcode::Call), result,
                                                 static_cast<IrWord>(ir.symbols.size() - 1),
                                                 static_cast<IrWord>(arguments.size())});
                ir.words.insert(ir.words.end(), arguments.begin(), arguments.end());
                } else {
                ir.words.insert(ir.words.end(), {static_cast<IrWord>(IrOpcode::CallNamed), result,
                                                 static_cast<IrWord>(ir.symbols.size() - 1),
                                                 static_cast<IrWord>(arguments.size())});
                for (std::size_t index = 0; index < arguments.size(); ++index) {
                    ir.words.push_back(names[index]);
                    ir.words.push_back(arguments[index]);
                }
                }
            }
            }
        } else if (const auto array = std::dynamic_pointer_cast<ArrayExpr>(value)) {
            std::vector<RegisterId> items;
            items.reserve(array->items.size());
            for (const auto& item : array->items) items.push_back(self(self, item));
            ir.words.insert(ir.words.end(), {static_cast<IrWord>(IrOpcode::MakeArray), result, 0,
                                             static_cast<IrWord>(items.size())});
            ir.words.insert(ir.words.end(), items.begin(), items.end());
        } else if (const auto map = std::dynamic_pointer_cast<MapExpr>(value)) {
            std::vector<std::pair<IrSymbolRef, RegisterId>> entries;
            entries.reserve(map->entries.size());
            for (const auto& entry : map->entries) {
                const auto item = self(self, entry.value);
                ir.symbols.push_back(entry.keyId);
                entries.emplace_back(static_cast<IrSymbolRef>(ir.symbols.size() - 1), item);
            }
            if (map->factType.empty()) {
                ir.words.insert(ir.words.end(), {static_cast<IrWord>(IrOpcode::MakeMap), result, 0,
                                                 static_cast<IrWord>(entries.size())});
                for (const auto& [field, item] : entries) {
                    ir.words.push_back(field);
                    ir.words.push_back(item);
                }
            } else {
                ir.symbols.push_back(symbolIdForName(map->factType));
                ir.words.insert(ir.words.end(), {static_cast<IrWord>(IrOpcode::MakeFact), result,
                                                 static_cast<IrWord>(ir.symbols.size() - 1)});
                for (const auto& [field, item] : entries) {
                    ir.words.insert(ir.words.end(), {static_cast<IrWord>(IrOpcode::SetField), result,
                                                     field, item});
                }
            }
        } else if (const auto access = std::dynamic_pointer_cast<AccessExpr>(value)) {
            const auto target = self(self, access->target);
            ir.symbols.push_back(access->keyId);
            ir.words.insert(ir.words.end(), {static_cast<IrWord>(IrOpcode::GetField), result, target,
                                             static_cast<IrWord>(ir.symbols.size() - 1)});
        } else if (const auto operation = std::dynamic_pointer_cast<OperatorExpression>(value)) {
            const auto core = operation->coreOperator;
            if (core == CoreOperator::Unknown) {
                if (operation->resolvedMethodId == 0) {
                    throw IntegerParserError("mixfix expression requires verified model target selection");
                }
                std::vector<RegisterId> arguments;
                arguments.reserve(operation->captureCount());
                for (std::size_t index = 0; index < operation->captureCount(); ++index) {
                    arguments.push_back(self(self, operation->capture(index)));
                }
                ir.symbols.push_back(operation->resolvedMethodId);
                ir.words.insert(ir.words.end(), {static_cast<IrWord>(IrOpcode::Call), result,
                                                 static_cast<IrWord>(ir.symbols.size() - 1),
                                                 static_cast<IrWord>(arguments.size())});
                ir.words.insert(ir.words.end(), arguments.begin(), arguments.end());
            } else if (core == CoreOperator::UnaryPlus) {
                return self(self, operation->capture(0));
            } else if (core == CoreOperator::UnaryMinus) {
                const auto source = self(self, operation->capture(0));
                const auto zero = static_cast<RegisterId>(ir.registerCount++);
                ir.words.insert(ir.words.end(), {static_cast<IrWord>(IrOpcode::LoadConst), zero,
                                                 addNumber(0.0),
                                                 static_cast<IrWord>(IrOpcode::Sub), result, zero, source});
            } else if (core == CoreOperator::LogicalNot) {
                const auto source = self(self, operation->capture(0));
                const auto trueConstant = addBoolean(true);
                const auto falseConstant = addBoolean(false);
                const auto branch = ir.words.size();
                ir.words.insert(ir.words.end(), {static_cast<IrWord>(IrOpcode::JumpIfFalse), source, 0,
                                                 static_cast<IrWord>(IrOpcode::LoadConst), result, falseConstant,
                                                 static_cast<IrWord>(IrOpcode::Jump), 0});
                const auto falseTarget = ir.words.size();
                ir.words.insert(ir.words.end(), {static_cast<IrWord>(IrOpcode::LoadConst), result, trueConstant});
                const auto endTarget = ir.words.size();
                ir.words[branch + 2] = falseTarget;
                ir.words[branch + 7] = endTarget;
            } else if (core == CoreOperator::LogicalAnd || core == CoreOperator::LogicalOr) {
                const auto left = self(self, operation->capture(0));
                const auto right = self(self, operation->capture(1));
                const auto trueConstant = addBoolean(true);
                const auto falseConstant = addBoolean(false);
                if (core == CoreOperator::LogicalAnd) {
                    const auto leftBranch = ir.words.size();
                    ir.words.insert(ir.words.end(), {static_cast<IrWord>(IrOpcode::JumpIfFalse), left, 0,
                                                     static_cast<IrWord>(IrOpcode::JumpIfFalse), right, 0,
                                                     static_cast<IrWord>(IrOpcode::LoadConst), result, trueConstant,
                                                     static_cast<IrWord>(IrOpcode::Jump), 0});
                    const auto falseTarget = ir.words.size();
                    ir.words.insert(ir.words.end(), {static_cast<IrWord>(IrOpcode::LoadConst), result, falseConstant});
                    const auto endTarget = ir.words.size();
                    ir.words[leftBranch + 2] = falseTarget;
                    ir.words[leftBranch + 5] = falseTarget;
                    ir.words[leftBranch + 10] = endTarget;
                } else {
                    const auto leftBranch = ir.words.size();
                    ir.words.insert(ir.words.end(), {static_cast<IrWord>(IrOpcode::JumpIfFalse), left, 0,
                                                     static_cast<IrWord>(IrOpcode::LoadConst), result, trueConstant,
                                                     static_cast<IrWord>(IrOpcode::Jump), 0});
                    const auto rightBranch = ir.words.size();
                    ir.words.insert(ir.words.end(), {static_cast<IrWord>(IrOpcode::JumpIfFalse), right, 0,
                                                     static_cast<IrWord>(IrOpcode::Jump), leftBranch + 3});
                    const auto falseTarget = ir.words.size();
                    ir.words.insert(ir.words.end(), {static_cast<IrWord>(IrOpcode::LoadConst), result, falseConstant});
                    const auto endTarget = ir.words.size();
                    ir.words[leftBranch + 2] = rightBranch;
                    ir.words[leftBranch + 7] = endTarget;
                    ir.words[rightBranch + 2] = falseTarget;
                }
            } else {
                const auto left = self(self, operation->capture(0));
                const auto right = self(self, operation->capture(1));
                IrOpcode opcode;
                IrComparison comparison = IrComparison::Equal;
                switch (core) {
                case CoreOperator::Add: opcode = IrOpcode::Add; break;
                case CoreOperator::Subtract: opcode = IrOpcode::Sub; break;
                case CoreOperator::Multiply: opcode = IrOpcode::Mul; break;
                case CoreOperator::Divide: opcode = IrOpcode::Div; break;
                case CoreOperator::Modulo: opcode = IrOpcode::Mod; break;
                case CoreOperator::StrictEqual: opcode = IrOpcode::Compare; break;
                case CoreOperator::StrictNotEqual: opcode = IrOpcode::Compare; comparison = IrComparison::NotEqual; break;
                case CoreOperator::Less: opcode = IrOpcode::Compare; comparison = IrComparison::Less; break;
                case CoreOperator::LessEqual: opcode = IrOpcode::Compare; comparison = IrComparison::LessEqual; break;
                case CoreOperator::Greater: opcode = IrOpcode::Compare; comparison = IrComparison::Greater; break;
                case CoreOperator::GreaterEqual: opcode = IrOpcode::Compare; comparison = IrComparison::GreaterEqual; break;
                default: throw IntegerParserError("expression has no direct IR lowering yet");
                }
                ir.words.insert(ir.words.end(), {static_cast<IrWord>(opcode), result, left, right});
                if (opcode == IrOpcode::Compare) ir.words.push_back(static_cast<IrWord>(comparison));
            }
        } else {
            throw IntegerParserError("expression has no direct IR lowering yet");
        }
        const auto& span = value->sourceSpan;
        ir.sourceMap.push_back(IrSourceMapEntry{emittedAt,
            {span.startLine, span.startColumn, span.endLine, span.endColumn}});
        return result;
    };
    const auto result = lower(lower, expression);
    ir.words.insert(ir.words.end(), {static_cast<IrWord>(IrOpcode::Return), result, 0,
                                     static_cast<IrWord>(IrOpcode::End)});
    IrVerifier::verify(ir);
    return ir;
}

FelidaeIr IntegerParser::compileAstGlobalBindingIr(const GlobalBindingStmt& binding,
                                                    const std::unordered_set<SymbolId>& factTypes) {
    if (!binding.expr) throw IntegerParserError("Global binding has no value expression");
    auto ir = compileAstExpressionIr(binding.expr, factTypes);
    if (ir.words.size() < 4 ||
        ir.words[ir.words.size() - 4] != static_cast<IrWord>(IrOpcode::Return) ||
        ir.words.back() != static_cast<IrWord>(IrOpcode::End)) {
        throw IntegerParserError("Expression IR is missing its terminal return");
    }
    const auto result = ir.words[ir.words.size() - 3];
    ir.symbols.push_back(symbolIdForName(binding.name));
    const auto symbol = static_cast<IrWord>(ir.symbols.size() - 1);
    ir.words.resize(ir.words.size() - 4);
    ir.words.insert(ir.words.end(), {
        static_cast<IrWord>(IrOpcode::StoreSymbol), symbol, result,
        static_cast<IrWord>(IrOpcode::LoadSymbol), result, symbol,
        static_cast<IrWord>(IrOpcode::Return), result, 0,
        static_cast<IrWord>(IrOpcode::End),
    });
    const auto& span = binding.sourceSpan;
    ir.sourceMap.push_back(IrSourceMapEntry{ir.words.size() - 10,
        {span.startLine, span.startColumn, span.endLine, span.endColumn}});
    IrVerifier::verify(ir);
    return ir;
}

FelidaeIr IntegerParser::compileAstEntryMethodIr(const ClauseStmt& method,
                                                  const std::unordered_set<SymbolId>& factTypes) {
    if (method.body.size() != 1 || !method.fallbackBranches.empty()) {
        throw IntegerParserError("Method has not reached direct entry IR lowering yet");
    }
    const auto returned = std::dynamic_pointer_cast<ReturnGoal>(method.body.front());
    if (!returned) throw IntegerParserError("Direct entry IR lowering requires a return goal");
    if (returned->fields.empty()) {
        auto nil = std::make_shared<NilExpr>();
        nil->sourceSpan = returned->sourceSpan;
        return compileAstExpressionIr(nil, factTypes);
    }
    bool named = false;
    for (const auto& field : returned->fields) named = named || !field.name.empty();
    if (!named) {
        if (returned->fields.size() != 1) {
            throw IntegerParserError("Direct entry IR lowering requires one positional return value");
        }
        return compileAstExpressionIr(returned->fields.front().value, factTypes);
    }
    std::vector<MapEntry> entries;
    entries.reserve(returned->fields.size());
    for (const auto& field : returned->fields) {
        if (field.name.empty()) {
            throw IntegerParserError("Direct entry IR lowering cannot mix named and positional return fields");
        }
        entries.emplace_back(field.name, field.nameId, field.value);
    }
    auto map = std::make_shared<MapExpr>(std::move(entries));
    map->sourceSpan = returned->sourceSpan;
    return compileAstExpressionIr(map, factTypes);
}

SourceSpan IntegerParser::span(std::size_t begin, std::size_t end) const {
    const auto advance = [](SourceSpan& target, const IntegerTokenList::Entry& entry,
                            std::size_t count) {
        if (entry.id == TokenId::NEWLINE || entry.id == TokenId::CARRIAGE_RETURN) {
            ++target.endLine;
            target.endColumn = 1;
        } else {
            target.endColumn += static_cast<int>(count);
        }
    };
    const auto& entries = input_.entries();
    const auto first = std::lower_bound(entries.begin(), entries.end(), begin,
        [](const IntegerTokenList::Entry& entry, std::size_t offset) {
            return entry.end <= offset;
        });
    const auto firstIndex = static_cast<std::size_t>(first - entries.begin());
    SourceSpan cursor = pieceStarts_.at(firstIndex);
    if (first != entries.end() && first->begin < begin) {
        advance(cursor, *first, begin - first->begin);
    }
    SourceSpan result;
    result.startLine = cursor.endLine;
    result.startColumn = cursor.endColumn;
    result.endLine = result.startLine;
    result.endColumn = result.startColumn;
    for (auto entry = first; entry != entries.end(); ++entry) {
        if (entry->begin >= end) break;
        const auto overlapBegin = std::max(entry->begin, begin);
        const auto overlapEnd = std::min(entry->end, end);
        advance(result, *entry, overlapEnd - overlapBegin);
    }
    return result;
}

void IntegerParser::stamp(const std::shared_ptr<AstNode>& node,
                          std::size_t begin,
                          std::size_t end) const {
    node->sourceSpan = span(begin, end);
}

} // namespace Felidae
