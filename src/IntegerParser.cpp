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
                             std::shared_ptr<OperatorRegistry> operators)
    : input_(input), operators_(std::move(operators)) {
    metrics_.sourceEncodeCount = input.encodeCount();
    metrics_.tokenCount = input.entries().size();
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

bool IntegerParser::atBlockEnd() {
    skipTrivia();
    return piece_ < input_.entries().size() && input_.entries()[piece_].id == TokenId::END;
}

bool IntegerParser::matchBlockEnd() {
    if (!atBlockEnd()) return false;
    const auto end = input_.entries()[piece_++].end;
    byte_ = end;
    if (at(TokenId::DOT)) (void)match(TokenId::DOT);
    return true;
}

void IntegerParser::require(TokenId::Id id, const char* message) {
    if (!match(id)) {
        const std::string detail = std::string(message) + " at source byte " +
                                   std::to_string(byte_);
        if (atEnd()) throw IntegerParserIncomplete(detail);
        throw IntegerParserError(detail);
    }
}

void IntegerParser::requireBlockEnd(const char* message) {
    if (matchBlockEnd()) return;
    if (atEnd()) throw IntegerParserIncomplete(message);
    throw IntegerParserError(message);
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
    // The word vocabulary also emits that same ID as the prefix of identifiers such
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
    return id > TokenId::UNKNOWN && !isBuiltinTokenId(id);
}

bool IntegerParser::sourceContainsLineBreak(std::size_t begin, std::size_t end) const {
    for (const auto& entry : input_.entries()) {
        if (entry.end <= begin || entry.begin >= end) continue;
        if (entry.id == TokenId::NEWLINE || entry.id == TokenId::CARRIAGE_RETURN) return true;
    }
    return false;
}

bool IntegerParser::lineBreakBeforeNextSignificantPiece() const {
    const auto& source = input_.source();
    for (std::size_t index = byte_; index < source.size(); ++index) {
        const char byte = source[index];
        if (byte == '\n' || byte == '\r') return true;
        if (byte == '#') {
            const auto newline = source.find_first_of("\r\n", index);
            return newline != std::string::npos;
        }
        if (byte != ' ' && byte != '\t') return false;
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
        if (atEnd()) throw IntegerParserIncomplete("Expected a token name range");
        const auto id = piece_ < input_.entries().size() ? input_.entries()[piece_].id : TokenId::UNKNOWN;
        throw IntegerParserError("Expected a token name range at source byte " +
                                 std::to_string(byte_) + " (ID " + std::to_string(id) + ")");
    }
    const std::size_t begin = byte_;
    const auto& pieces = input_.entries();
    // A logical name is a contiguous run of non-grammar token IDs.
    // The word vocabulary may split one source name into many adjacent pieces.
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
            // Literal content is a lexer-owned source span; the word vocabulary supplies IDs
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
                const auto binding = consumeQualifiedName();
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

IntegerParser::QualifiedName IntegerParser::consumeQualifiedName(
    bool allowDottedName) {
    skipTrivia();
    const auto firstPiece = piece_;
    const bool capitalized =
        piece_ < input_.entries().size() &&
        std::isupper(static_cast<unsigned char>(input_.source().at(
            input_.entries()[piece_].begin))) != 0;
    QualifiedName name{consumeNameRange(), 0, BuiltinId::Unknown, capitalized};
    while (allowDottedName && !capitalized && at(TokenId::DOT)) {
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
    while (match(TokenId::DOT)) {
        const auto segment = consumeQualifiedName(false);
        name.spelling += "." + segment.spelling;
    }
    TypeRef type{name.spelling, {}};
    if (match(TokenId::LESS)) {
        do { type.arguments.push_back(parseTypeReference()); } while (match(TokenId::COMMA));
        require(TokenId::GREATER, "Expected '>' after generic type arguments");
    }
    if (type.name == "optional" || type.name == "list") {
        if (type.arguments.size() != 1) {
            throw IntegerParserError(type.name + " requires exactly one type argument");
        }
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
    ParsedOperatorAnnotation parsed;
    try {
        parsed = decodeOperatorAnnotation(annotation);
    } catch (const std::runtime_error& error) {
        throw IntegerParserError(error.what());
    }
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
    if (match(TokenId::FOR)) {
        const auto variable = consumeQualifiedName(false);
        require(TokenId::IN, "Expected 'in' after for-loop variable");
        auto iterable = parseBinaryExpression(
            static_cast<int>(OperatorPrecedence::Control), TokenId::THEN);
        require(TokenId::THEN, "Expected 'then' after for-loop iterable");
        auto body = parseGoalList(TokenId::DOT);
        requireBlockEnd("Expected 'end' after for loop");
        auto result = std::make_shared<ForGoal>(variable.spelling, std::move(iterable), std::move(body));
        stamp(result, begin, byte_);
        return result;
    }
    if (match(TokenId::WHILE)) {
        auto condition = parseBinaryExpression(
            static_cast<int>(OperatorPrecedence::Control), TokenId::THEN);
        require(TokenId::THEN, "Expected 'then' after while condition");
        auto body = parseGoalList(TokenId::DOT);
        requireBlockEnd("Expected 'end' after while loop");
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
            branch.body = parseGoalList(TokenId::DOT);
            cases.push_back(std::move(branch));
        }
        if (cases.empty()) {
            if (atEnd()) {
                throw IntegerParserIncomplete(
                    "Switch requires at least one case or default branch");
            }
            throw IntegerParserError("Switch requires at least one case or default branch");
        }
        requireBlockEnd("Expected 'end' after switch");
        auto result = std::make_shared<SwitchGoal>(std::move(value), std::move(cases));
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
    if (match(TokenId::IF)) {
        // `elif` is sugar, not a new AST shape: `if A then T1 elif B then T2
        // else T3 end` desugars to exactly the nested
        // `if A then T1 else if B then T2 else T3 end end` a user could
        // already write by hand - one IfGoal
        // per condition, each one's elseBranch holding the next. Only one
        // `end` appears in the source for the whole chain, so matchBlockEnd
        // runs once, after every branch is parsed, never per synthesized
        // nesting level.
        const auto parseCondition = [&]() -> std::shared_ptr<Goal> {
            auto conditionExpression = parseBinaryExpression(
                static_cast<int>(OperatorPrecedence::Control), TokenId::THEN);
            require(TokenId::THEN, "Expected 'then' after if condition");
            if (const auto comparison = std::dynamic_pointer_cast<OperatorExpression>(conditionExpression);
                comparison && comparison->captureCount() == 2 &&
                isComparisonOperator(comparison->coreOperator)) {
                return std::make_shared<BinaryGoal>(comparison->capture(0),
                    coreOperatorDefinition(comparison->coreOperator).token, comparison->capture(1));
            }
            return std::make_shared<BinaryGoal>(std::move(conditionExpression), TokenId::EQUAL,
                                                std::make_shared<BoolExpr>(true), true);
        };
        struct Branch {
            std::size_t begin;
            std::shared_ptr<Goal> condition;
            std::vector<std::shared_ptr<Goal>> thenBranch;
        };
        std::vector<Branch> branches;
        branches.push_back({begin, parseCondition(), parseGoalList(TokenId::DOT)});
        while (true) {
            const auto branchBegin = byte_;
            if (!match(TokenId::ELIF)) break;
            branches.push_back({branchBegin, parseCondition(), parseGoalList(TokenId::DOT)});
        }
        std::vector<std::shared_ptr<Goal>> elseBranch;
        if (match(TokenId::ELSE)) elseBranch = parseGoalList(TokenId::DOT);
        requireBlockEnd("Expected 'end' after if statement");
        std::shared_ptr<Goal> result;
        auto tailElse = std::move(elseBranch);
        for (auto branch = branches.rbegin(); branch != branches.rend(); ++branch) {
            auto ifGoal = std::make_shared<IfGoal>(std::move(branch->condition),
                                                   std::move(branch->thenBranch), std::move(tailElse));
            stamp(ifGoal, branch->begin, byte_);
            result = ifGoal;
            tailElse = std::vector<std::shared_ptr<Goal>>{std::move(ifGoal)};
        }
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
            // already one token stream; this merely assembles the
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
        if (match(TokenId::COLON)) {
            auto type = parseTypeReference();
            require(TokenId::ASSIGN, "Expected ':=' after typed local binding");
            auto result = std::make_shared<MultiAssignGoal>(
                std::vector<AssignmentTarget>{AssignmentTarget{name, std::move(type)}},
                parseExpression());
            stamp(result, begin, byte_);
            return result;
        }
    }
    byte_ = start;
    piece_ = startPiece;
    auto left = parseExpression();
    if (match(TokenId::ASSIGN)) {
        const auto access = std::dynamic_pointer_cast<AccessExpr>(left);
        const auto receiver = access
            ? std::dynamic_pointer_cast<VarExpr>(access->target) : nullptr;
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

// True when the parser sits at the start of what can only be a new clause
// head (`qualifiedName(...) =>`, optionally followed by `as designation`),
// never a goal continuing the current body: no goal is itself a bare
// callable head immediately followed by an arrow. Checking this shape - independent
// of indentation - is what stops a same-line body (`f() => return x`, whose
// one line sits at indent 0 like any top-level clause) from swallowing the
// clause that follows it; the indentation dedent check in parseGoalList only
// protects a genuinely indented body. Pure lookahead: parser position is
// always restored before returning.
bool IntegerParser::looksLikeClauseHead() {
    skipTrivia();
    if (!atNameRange()) return false;
    const auto savedByte = byte_;
    const auto savedPiece = piece_;
    bool result = false;
    try {
        consumeQualifiedName();
        if (match(TokenId::LPAREN)) {
            std::size_t depth = 1;
            const auto& pieces = input_.entries();
            while (depth > 0 && piece_ < pieces.size()) {
                const auto id = pieces[piece_].id;
                byte_ = pieces[piece_++].end;
                if (id == TokenId::LPAREN) ++depth;
                else if (id == TokenId::RPAREN) --depth;
            }
            if (depth == 0) {
                if (match(TokenId::AS)) {
                    do { consumeQualifiedName(); } while (match(TokenId::COMMA));
                }
                result = at(TokenId::ARROW);
            }
        }
    } catch (const IntegerParserError&) {
        result = false;
    }
    byte_ = savedByte;
    piece_ = savedPiece;
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
        if (atBlockEnd()) break;
        if (looksLikeClauseHead()) break;
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
        if (atEnd() || at(terminator) || at(TokenId::ELSE) ||
            at(TokenId::CASE) || at(TokenId::DEFAULT) || atBlockEnd()) break;
        if (!sourceContainsLineBreak(before, byte_) || sourceLineIndent(byte_) < bodyIndent) break;
    } while (true);
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
    if (annotations.empty() && match(TokenId::CLASS)) {
        if (hasDefKeyword) {
            throw IntegerParserError("Class declarations must not begin with 'def'");
        }
        return parseClassStatement(begin);
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
            if (hasDefKeyword) {
                throw IntegerParserError("Global bindings must not begin with 'def'");
            }
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
    const bool hasArrow = match(TokenId::ARROW);
    if (hasArrow) {
        if (!hasDefKeyword) {
            throw IntegerParserError("Function and rule declarations must begin with 'def'");
        }
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
        if (emptyDeclaration) lastClauseUsedBlockEnd_ = matchBlockEnd();
        if (!lastClauseUsedBlockEnd_) {
            if (atEnd()) {
                throw IntegerParserIncomplete(
                    "Expected 'end' after function or rule declaration");
            }
            throw IntegerParserError("Expected 'end' after function or rule declaration");
        }
    } else if (hasDefKeyword) {
        if (atEnd()) {
            throw IntegerParserIncomplete(
                "Expected '=>' after function or rule declaration");
        }
        throw IntegerParserError("Expected '=>' after function or rule declaration");
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
    stamp(result, begin, byte_);
    return result;
}

std::shared_ptr<ClassStmt> IntegerParser::parseClassStatement(std::size_t begin) {
    auto className = consumeQualifiedName();
    if (!className.isCapitalized)
        throw IntegerParserError("Class names must begin with an uppercase letter");
    while (match(TokenId::DOT)) {
        const auto related = consumeQualifiedName(false);
        if (!related.isCapitalized) {
            throw IntegerParserError("Every segment of a dotted class name must begin with an uppercase letter");
        }
        className.spelling += "." + related.spelling;
    }
    className.nameId = symbolIdForName(className.spelling);
    if (className.spelling == "Link") {
        throw IntegerParserError(
            "Link is reserved for persistent graph edges and cannot be declared as a class");
    }
    const auto consumeDottedType = [&]() {
        auto type = consumeQualifiedName();
        if (!type.isCapitalized) throw IntegerParserError("Type names must begin with an uppercase letter");
        while (match(TokenId::DOT)) {
            const auto segment = consumeQualifiedName(false);
            if (!segment.isCapitalized) {
                throw IntegerParserError("Every segment of a dotted type name must begin with an uppercase letter");
            }
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
    const auto parseClassMethod = [&]() {
        const bool previousClassMethod = insideClassMethod_;
        insideClassMethod_ = true;
        std::shared_ptr<ClauseStmt> method;
        try {
            method = std::dynamic_pointer_cast<ClauseStmt>(parseStatement());
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
        if (at(TokenId::AT) || at(TokenId::DEF)) {
            methods.push_back(parseClassMethod());
            continue;
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
            consumeStatementTerminator(fieldBegin);
            index.sourceSpan = span(fieldBegin, byte_);
            indexes.push_back(std::move(index));
            continue;
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
            consumeStatementTerminator(fieldBegin);
            key.sourceSpan = span(fieldBegin, byte_);
            continue;
        }
        if (at(TokenId::LPAREN)) {
            byte_ = fieldBegin;
            piece_ = fieldPiece;
            methods.push_back(parseClassMethod());
            continue;
        }
        require(TokenId::COLON, "Expected ':' after class field name");
        auto type = parseTypeReference();
        std::shared_ptr<Expr> defaultValue;
        if (match(TokenId::ASSIGN)) defaultValue = parseExpression();
        if (!fieldIds.insert(field.nameId).second)
            throw IntegerParserError("Duplicate class field '" + field.spelling + "'");
        consumeStatementTerminator(fieldBegin);
        fields.emplace_back(field.spelling, field.nameId, std::move(type),
                            std::move(defaultValue), span(fieldBegin, byte_));
    }
    requireBlockEnd("Expected 'end' after class declaration");
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

bool IntegerParser::emptyInput() {
    return atEnd();
}

bool IntegerParser::startsProgramStatement() {
    if (at(TokenId::DEF) || at(TokenId::CLASS) || at(TokenId::AT) ||
        at(TokenId::IMPORT)) {
        return true;
    }

    const auto savedByte = byte_;
    const auto savedPiece = piece_;
    bool globalBinding = false;
    if (atNameRange()) {
        (void)consumeNameRange();
        globalBinding = at(TokenId::ASSIGN);
    }
    byte_ = savedByte;
    piece_ = savedPiece;
    if (globalBinding) return true;

    // Calls are expressions by default in the REPL. A normal Felidae `.`
    // terminator explicitly selects top-level fact/rule source semantics.
    TokenId::Id last = TokenId::UNKNOWN;
    bool inComment = false;
    for (const auto& entry : input_.entries()) {
        if (inComment) {
            if (entry.id == TokenId::NEWLINE ||
                entry.id == TokenId::CARRIAGE_RETURN) inComment = false;
            continue;
        }
        if (entry.id == TokenId::COMMENT) {
            inComment = true;
            continue;
        }
        if (entry.id == TokenId::SPACE || entry.id == TokenId::TAB ||
            entry.id == TokenId::NEWLINE || entry.id == TokenId::CARRIAGE_RETURN) {
            continue;
        }
        last = entry.id;
    }
    return last == TokenId::DOT;
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
        auto result = std::make_shared<StringExpr>(consumeString());
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
        auto type = consumeQualifiedName();
        if (!type.isCapitalized) throw IntegerParserError("new expects a capitalized fact or class name");
        while (match(TokenId::DOT)) {
            const auto related = consumeQualifiedName(false);
            if (!related.isCapitalized) {
                throw IntegerParserError("Every segment of a dotted type name must begin with an uppercase letter");
            }
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
        auto result = parseExpression();
        require(TokenId::RPAREN, "Expected ')' after grouped expression");
        stamp(result, begin, byte_);
        return result;
    }
    if (atNameRange()) {
        // Keep dots for parseUnary so `people.get()` is a receiver call,
        // rather than an unrelated qualified callable named `people.get`.
        const auto name = consumeQualifiedName(false);
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
    if (atEnd()) {
        throw IntegerParserIncomplete(
            "Expected an expression at source byte " + std::to_string(byte_));
    }
    throw IntegerParserError("Expected an expression");
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
                                         selected->operatorName + "' at source byte " +
                                         std::to_string(byte_));
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
        if (const auto number = std::dynamic_pointer_cast<NumberExpr>(operand)) {
            return std::make_shared<NumberExpr>(-number->value);
        }
        return std::make_shared<OperatorExpression>(CoreOperator::UnaryMinus, std::move(operand));
    }
    if (match(TokenId::PLUS)) return parseUnary();
    auto result = parsePrimary();
    while (at(TokenId::DOT)) {
        const auto beforeByte = byte_;
        const auto beforePiece = piece_;
        match(TokenId::DOT);
        const auto separatorEnd = byte_;
        const auto memberKeyword = piece_ < input_.entries().size()
            ? builtinTokenSpelling(input_.entries()[piece_].id) : std::string_view{};
        const bool classReference = at(TokenId::CLASS);
        if ((!atNameRange() && memberKeyword.empty() && !classReference) ||
            sourceContainsLineBreak(separatorEnd, byte_)) {
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
                const auto reference = std::dynamic_pointer_cast<VarExpr>(result);
                if (!reference) {
                    throw IntegerParserError(".class requires a class name");
                }
                result = std::make_shared<ClassRefExpr>(reference->name);
                continue;
            }
            if (member == "function") {
                const auto reference = std::dynamic_pointer_cast<VarExpr>(result);
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
        const auto type = std::dynamic_pointer_cast<VarExpr>(result);
        const auto bucket = std::dynamic_pointer_cast<TermExpr>(result);
        const std::string staticType = type && type->isCapitalized
            ? type->name
            : (bucket && bucket->isCapitalized && bucket->args.empty() ? bucket->name : std::string{});
        const bool runtimeBuiltinMember =
            member == "all" || member == "count" || member == "get" ||
            member == "where" || member == "select" || member == "insert" ||
            member == "save" || member == "update" || member == "delete" ||
            member == "AndWhere" || member == "OrWhere" || member == "limit" ||
            member == "order_by" || member == "join" ||
            member == "recursive_join" || member == "shortest_path" ||
            member == "len" || member == "push";
        if (!staticType.empty() || runtimeBuiltinMember) {
            if (member == "join" || member == "recursive_join" || member == "shortest_path") {
                for (const auto& argument : arguments) {
                    if (argument.name == "direction" &&
                        std::dynamic_pointer_cast<VarExpr>(argument.value)) {
                        throw IntegerParserError(
                            "Graph direction requires forward.class, backward.class, or both.class");
                    }
                }
            }
            std::vector<Arg> invokeArgs;
            invokeArgs.reserve(arguments.size() + 2);
            invokeArgs.emplace_back("receiver", !staticType.empty()
                ? std::shared_ptr<Expr>(std::make_shared<ClassRefExpr>(staticType))
                : std::move(result));
            invokeArgs.emplace_back("member", std::make_shared<StringExpr>(member));
            for (auto& argument : arguments) invokeArgs.push_back(std::move(argument));
            result = std::make_shared<TermExpr>(
                std::string(kMemberInvokeTerm), std::move(invokeArgs));
            continue;
        }
        if (const auto receiver = std::dynamic_pointer_cast<VarExpr>(result)) {
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

SourceSpan IntegerParser::span(std::size_t begin, std::size_t end) const {
    SourceSpan result;
    const auto advance = [](SourceSpan& target, const IntegerTokenList::Entry& entry,
                            std::size_t count) {
        if (entry.id == TokenId::NEWLINE || entry.id == TokenId::CARRIAGE_RETURN) {
            ++target.endLine;
            target.endColumn = 1;
        } else {
            target.endColumn += static_cast<int>(count);
        }
    };
    SourceSpan cursor;
    for (const auto& entry : input_.entries()) {
        if (entry.begin >= begin) break;
        const auto count = std::min(entry.end, begin) - entry.begin;
        advance(cursor, entry, count);
    }
    result.startLine = cursor.endLine;
    result.startColumn = cursor.endColumn;
    result.endLine = result.startLine;
    result.endColumn = result.startColumn;
    for (const auto& entry : input_.entries()) {
        if (entry.end <= begin) continue;
        if (entry.begin >= end) break;
        const auto overlapBegin = std::max(entry.begin, begin);
        const auto overlapEnd = std::min(entry.end, end);
        advance(result, entry, overlapEnd - overlapBegin);
    }
    return result;
}

void IntegerParser::stamp(const std::shared_ptr<AstNode>& node,
                          std::size_t begin,
                          std::size_t end) const {
    node->sourceSpan = span(begin, end);
}

} // namespace Felidae
