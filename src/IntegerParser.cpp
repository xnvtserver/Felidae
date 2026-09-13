#include "IntegerParser.h"

#include "BuiltinRegistry.h"
#include "Operator.h"
#include "OperatorAnnotation.h"
#include "Tokenizer.h"

#include <algorithm>
#include <cctype>
#include <tuple>
#include <utility>

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
        throw IntegerParserError(std::string(message) + describeLocation(byte_));
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

const std::vector<std::size_t>& IntegerParser::lineBreakOffsets() const {
    if (!lineBreakOffsetsBuilt_) {
        for (const auto& entry : input_.entries()) {
            if (entry.id == TokenId::NEWLINE || entry.id == TokenId::CARRIAGE_RETURN) {
                lineBreakOffsets_.push_back(entry.begin);
            }
        }
        lineBreakOffsetsBuilt_ = true;
    }
    return lineBreakOffsets_;
}

bool IntegerParser::sourceContainsLineBreak(std::size_t begin, std::size_t end) const {
    // The previous version rescanned every token from index 0 on every call;
    // parseGoalList calls this once per goal in a sequential list, so a long
    // straight-line body (thousands of statements) turned parsing into an
    // O(n^2) pass - confirmed by --metrics-json: loadMs grew ~9x for a 3x
    // increase in statement count while parserIterations grew only ~3x
    // (linear). Binary search into the cached, monotonically increasing
    // line-break offsets bounds each call to O(log n) instead.
    const auto& breaks = lineBreakOffsets();
    auto it = std::lower_bound(breaks.begin(), breaks.end(), begin);
    return it != breaks.end() && *it < end;
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
    // Same O(n^2) hazard as sourceContainsLineBreak above, and the same fix:
    // reuse the shared lineBreakOffsets() cache (one binary search finds the
    // line containing `offset`) instead of a separate bespoke backward-walk
    // through entries(). The leading run of SPACE/TAB is then just the raw
    // source bytes from the line's start - every whitespace byte there is a
    // SPACE/TAB entry by construction, so there is nothing token-specific
    // left to look at.
    const auto& breaks = lineBreakOffsets();
    const auto it = std::lower_bound(breaks.begin(), breaks.end(), offset);
    const std::size_t lineStart = (it == breaks.begin()) ? 0 : *std::prev(it) + 1;
    const auto& source = input_.source();
    std::size_t indent = 0;
    for (std::size_t i = lineStart; i < offset && i < source.size(); ++i) {
        if (source[i] == ' ') ++indent;
        else if (source[i] == '\t') indent += 4;
        else break;
    }
    return indent;
}

bool IntegerParser::startsOwnLine(std::size_t offset) const {
    // Shares sourceLineIndent's line-start lookup (same binary search into
    // lineBreakOffsets()), but asks a different question: not "how much
    // leading whitespace," but "is everything before `offset` on this line
    // whitespace at all" - i.e. does `offset` begin its own line, or does it
    // sit after other content on a line something else already started
    // (`def f() => return x`, where "return" shares a line with "def").
    const auto& breaks = lineBreakOffsets();
    const auto it = std::lower_bound(breaks.begin(), breaks.end(), offset);
    const std::size_t lineStart = (it == breaks.begin()) ? 0 : *std::prev(it) + 1;
    const auto& source = input_.source();
    for (std::size_t i = lineStart; i < offset && i < source.size(); ++i) {
        if (source[i] != ' ' && source[i] != '\t') return false;
    }
    return true;
}

void IntegerParser::consumeStatementTerminator(std::size_t statementBegin) {
    if (match(TokenId::DOT) || atEnd()) return;
    if (sourceContainsLineBreak(statementBegin, byte_)) return;
    throw IntegerParserError("Expected '.' or newline after statement" + describeLocation(byte_));
}

std::string IntegerParser::describeLocation(std::size_t offset) const {
    const auto& source = input_.source();
    const std::string suffix = " at source byte " + std::to_string(offset);
    if (offset >= source.size()) return suffix + " (end of input)";
    const unsigned char first = static_cast<unsigned char>(source[offset]);
    if (std::isspace(first)) return suffix + " (whitespace)";
    std::size_t end = offset;
    if (std::isalnum(first) || first == '_' || first >= 0x80) {
        while (end < source.size()) {
            const unsigned char current = static_cast<unsigned char>(source[end]);
            if (!(std::isalnum(current) || current == '_' || current >= 0x80)) break;
            ++end;
        }
    } else {
        end = offset + 1;
    }
    return suffix + " (found '" + std::string(source.substr(offset, end - offset)) + "')";
}

std::string IntegerParser::consumeNameRange() {
    skipTrivia();
    if (!atNameRange()) {
        throw IntegerParserError("Expected a token name range" + describeLocation(byte_));
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
    // One shared parser for every parenthesized name-list this grammar has -
    // a `def` head's parameters, a call's arguments, an annotation's
    // bindings - so a repeated name is rejected here once, for all of them,
    // the same way a class body already rejects a repeated field and a
    // mixfix pattern already rejects a repeated capture. Before this, a
    // duplicate named parameter (`def foo(x: number, x: number)`) or a
    // duplicate named argument at a call site silently let the last one win
    // rather than reporting the shape mismatch a real `def foo(x, x)` is.
    std::unordered_set<SymbolId> namedArgumentIds;
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
            if (named && !namedArgumentIds.insert(name.nameId).second) {
                throw IntegerParserError("Duplicate named argument '" + name.spelling + "'");
            }
            arguments.emplace_back(named ? std::move(name.spelling) : std::string{},
                                   named ? name.nameId : 0, std::move(value));
        } while (match(TokenId::COMMA));
    }
    require(TokenId::RPAREN, "Expected ')' after arguments");
    return arguments;
}

IntegerParser::QualifiedName IntegerParser::consumeQualifiedName(bool allowNamespaceSeparators,
                                                                  bool allowDottedName,
                                                                  bool allowCapitalizedDotted) {
    skipTrivia();
    const auto firstPiece = piece_;
    const bool capitalized =
        piece_ < input_.entries().size() &&
        std::isupper(static_cast<unsigned char>(input_.source().at(
            input_.entries()[piece_].begin))) != 0;
    QualifiedName name{consumeNameRange(), 0, BuiltinId::Unknown, capitalized};
    while ((allowDottedName && (allowCapitalizedDotted || !capitalized) && at(TokenId::DOT)) ||
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
        throw IntegerParserError("Annotation method '" + name.spelling + "' requires an argument list");
    }
    return Call(name.spelling, name.nameId, parseArguments(true), name.builtinId);
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
            lexeme.pieceIds.clear();
            // An anchor word that happens to spell a reserved keyword (e.g.
            // "as" in "evaluate {x} as {y}") must be registered under the
            // exact same ID the static lexer emits for it when it later
            // scans the real source - TokenId::AS, a single fixed-grammar
            // ID - not the byte-level tokenizer's encoding of the same
            // letters. Encoding it the second way here, unconditionally,
            // used to register an anchor that could never match anything
            // the lexer would ever actually produce; this makes the two
            // agree by construction instead of relying on them to happen
            // to agree.
            const TokenId::Id fixedId = fixedGrammarTokenId(lexeme.spelling);
            if (fixedId != TokenId::UNKNOWN) {
                lexeme.pieceIds.push_back(fixedId);
                continue;
            }
            const auto encoded = input_.tokenizer().encode(lexeme.spelling);
            if (encoded.empty()) {
                throw IntegerParserError("Unable to encode mixfix anchor '" + lexeme.spelling + "'");
            }
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
        // `elif` is sugar, not a new AST shape: `if A then T1 elif B then T2
        // else T3 end` desugars to exactly the nested
        // `if A then T1 else if B then T2 else T3 end end` a user could
        // already write by hand (see optional_end_blocks.fx) - one IfGoal
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
                                                makeTruthValue(true));
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
        (void)matchBlockEnd();
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
            // `return (` is ambiguous: `return (a: 1, b: 2)` is a record and
            // `return (1, 2)` a positional tuple, but `return (a + b) / c`
            // is just an ordinary expression whose grouped sub-expression
            // happens to come first. A name or a second field settles it as
            // a tuple; a single unnamed field does not, so resume the exact
            // same precedence climb an ordinary expression would have used
            // from here, in case more of it follows this ')'.
            if (fields.size() == 1 && fields.front().name.empty()) {
                fields.front().value = continueBinaryExpression(
                    std::move(fields.front().value),
                    static_cast<int>(OperatorPrecedence::Control));
            }
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
    // A body that starts on the same line as its own header (`def f() =>
    // return x`, as opposed to an indented block on the following lines) has
    // no real indentation to dedent from - sourceLineIndent measures the
    // *line's* leading whitespace, which is 0 whether "return" sits right
    // after "=>" or a fresh top-level statement starts the next line, so the
    // usual dedent check can never fire for it. Such a body was never meant
    // to continue past its own line, so the fix is not indentation-based at
    // all: the first line break after it starts always ends it, full stop.
    bool sameLineBody = false;
    do {
        // Measure indentation at the first significant ID, not at the
        // preceding arrow/terminator.  This keeps a bare return from pulling
        // the next top-level declaration into its method body.
        skipTrivia();
        if (atBlockEnd()) break;
        // `def` unambiguously starts a new declaration, never a goal
        // continuing this body - an O(1) token check, replacing the
        // speculative parse-ahead-and-backtrack looksLikeClauseHead() used
        // to require for the same purpose.
        if (at(TokenId::DEF)) break;
        const auto before = byte_;
        if (!hasBodyIndent) {
            bodyIndent = sourceLineIndent(before);
            sameLineBody = !startsOwnLine(before);
            hasBodyIndent = true;
        }
        goals.push_back(parseGoal());
        if (byte_ == before) throw IntegerParserError("Integer parser made no progress in goal list");
        if (const auto returned = std::dynamic_pointer_cast<ReturnGoal>(goals.back());
            returned && returned->fields.empty() && sourceContainsLineBreak(before, byte_)) {
            break;
        }
        if (match(TokenId::COMMA)) continue;
        if (atEnd() || at(terminator) || at(TokenId::ELSE) || atBlockEnd()) break;
        if (!sourceContainsLineBreak(before, byte_)) break;
        if (sameLineBody || sourceLineIndent(byte_) < bodyIndent) break;
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
    if (annotations.empty() && match(TokenId::CLASS)) return parseClassStatement(begin);
    if (match(TokenId::IMPORT)) {
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
    // `def` makes what follows unambiguous before a single token of it is
    // parsed: a callable declaration (method, rule, or native stub), never a
    // goal continuing some other body. That is what replaces
    // looksLikeClauseHead()'s speculative lookahead below - parseGoalList no
    // longer has to tentatively parse ahead and backtrack to tell "new
    // declaration" from "goal in the current body" apart; it just checks
    // for this one token.
    if (match(TokenId::DEF)) {
        // Native and user clauses may use qualified heads such as `math.sin`
        // or `Logic.negate` - a `def` head is never ambiguous the way a
        // dotted expression can be, so capitalized dotted names are allowed
        // here specifically (see consumeQualifiedName's comment).
        const auto clauseName = consumeQualifiedName(true, true, true);
        std::vector<std::string> parentNames;
        if (match(TokenId::EXTEND)) {
            do { parentNames.push_back(consumeQualifiedName().spelling); } while (match(TokenId::COMMA));
        }
        if (!at(TokenId::LPAREN)) {
            throw IntegerParserError("Expected '(' after 'def " + clauseName.spelling +
                                     "'" + describeLocation(byte_));
        }
        Call head(clauseName.spelling, clauseName.nameId, parseArguments(), clauseName.builtinId);
        if (match(TokenId::AS)) {
            do {
                const auto designation = consumeQualifiedName();
                head.designations.push_back(designation.spelling);
                head.designationIds.push_back(designation.nameId);
            } while (match(TokenId::COMMA));
        }
        if (!match(TokenId::ARROW)) {
            throw IntegerParserError("Expected '=>' after 'def " + clauseName.spelling +
                                     "(...)'" + describeLocation(byte_));
        }
        std::vector<std::shared_ptr<Goal>> body;
        std::vector<std::vector<std::shared_ptr<Goal>>> fallbackBranches;
        bool emptyDeclaration = false;
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
            lastClauseUsedBlockEnd_ = matchBlockEnd();
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
    if (!annotations.empty()) {
        throw IntegerParserError("Annotations can only be applied to a 'def' declaration");
    }
    const auto checkpoint = byte_;
    const auto checkpointPiece = piece_;
    if (atNameRange()) {
        const auto name = consumeNameRange();
        if (match(TokenId::ASSIGN)) {
            auto result = std::make_shared<GlobalBindingStmt>(name, parseExpression());
            consumeStatementTerminator(begin);
            stamp(result, begin, byte_);
            return result;
        }
    }
    byte_ = checkpoint;
    piece_ = checkpointPiece;
    // No `def`, and not `name := ...` either: a bare `name(...)` with no
    // arrow. This is the one shape that stays genuinely ambiguous even with
    // `def` mandatory for declarations, because Felidae's fact syntax is
    // legitimately bare too (`fact(name: "tiger").`, a Prolog-style
    // predicate fact - this is a logic-programming language, not every bare
    // declaration is a class instance). Whether this is a fresh fact
    // assertion or a call to something already declared by name can only be
    // known once declarations up to this point are visible, so this stays
    // ClauseKind::Fact at parse time and Interpreter::addStreamedStatement/
    // addProgram resolve it: if the name already names a declared clause,
    // it is treated as an entry call (executed immediately) instead of a
    // new fact. A capitalized dotted name is allowed here too (see
    // consumeQualifiedName's comment) so a bare top-level entry call to an
    // already-declared `def Logic.negate(...)`-style function parses the
    // same as its declaration does.
    const auto clauseName = consumeQualifiedName(true, true, true);
    // A fact can declare its parent type too (`Employee extend NamedEntity(...)`),
    // the same as a def'd clause can.
    std::vector<std::string> parentNames;
    if (match(TokenId::EXTEND)) {
        do { parentNames.push_back(consumeQualifiedName().spelling); } while (match(TokenId::COMMA));
    }
    if (!at(TokenId::LPAREN)) {
        throw IntegerParserError("Expected '(' after '" + clauseName.spelling +
                                 "'" + describeLocation(byte_) +
                                 " (declarations need a 'def' prefix)");
    }
    Call head(clauseName.spelling, clauseName.nameId, parseArguments(), clauseName.builtinId);
    if (match(TokenId::AS)) {
        do {
            const auto designation = consumeQualifiedName();
            head.designations.push_back(designation.spelling);
            head.designationIds.push_back(designation.nameId);
        } while (match(TokenId::COMMA));
    }
    consumeStatementTerminator(begin);
    auto result = std::make_shared<ClauseStmt>(std::move(head), std::move(parentNames),
                                               std::vector<std::shared_ptr<Goal>>{},
                                               std::vector<std::vector<std::shared_ptr<Goal>>>{},
                                               false, ClauseKind::Fact);
    result->designations = result->head.designations;
    result->designationIds = result->head.designationIds;
    stamp(result, begin, byte_);
    return result;
}

std::shared_ptr<ClassStmt> IntegerParser::parseClassStatement(std::size_t begin) {
    const auto className = consumeQualifiedName(false);
    if (!className.isCapitalized)
        throw IntegerParserError("Class names must begin with an uppercase letter");
    std::vector<std::string> parents;
    if (match(TokenId::EXTEND) || match(TokenId::EXTENDS)) {
        do { parents.push_back(consumeQualifiedName().spelling); } while (match(TokenId::COMMA));
    }
    std::vector<ClassFieldDecl> fields;
    std::vector<ClassIndexDecl> indexes;
    std::vector<std::shared_ptr<ClauseStmt>> methods;
    std::unordered_set<SymbolId> fieldIds;
    while (!atEnd() && !atBlockEnd()) {
        const auto fieldBegin = byte_;
        if (match(TokenId::INDEX)) {
            require(TokenId::LPAREN, "Expected '(' after class index");
            ClassIndexDecl index;
            do {
                const auto indexed = consumeQualifiedName(false);
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
        // `def` unambiguously starts a method, the same way it does at the
        // top level - no need to speculatively consume a name, peek for '(',
        // and backtrack to tell a method from a field the way this used to.
        if (at(TokenId::DEF)) {
            auto method = std::dynamic_pointer_cast<ClauseStmt>(parseStatement());
            if (!method || method->clauseKind != ClauseKind::Method || !lastClauseUsedBlockEnd_)
                throw IntegerParserError("Class methods must be methods terminated by 'end'");
            method->head.args.insert(method->head.args.begin(), Arg("self", symbolIdForName("self"),
                std::make_shared<VarExpr>(className.spelling, className.nameId)));
            method->head.name = className.spelling + "." + method->head.name;
            method->head.nameId = symbolIdForName(method->head.name);
            methods.push_back(std::move(method));
            continue;
        }
        const auto field = consumeQualifiedName(false);
        require(TokenId::COLON, "Expected ':' after class field name");
        const auto type = consumeQualifiedName();
        if (!isFelidaeLikelyTypeName(type.spelling))
            throw IntegerParserError("Expected a type name for class field '" + field.spelling + "'");
        if (!fieldIds.insert(field.nameId).second)
            throw IntegerParserError("Duplicate class field '" + field.spelling + "'");
        consumeStatementTerminator(fieldBegin);
        fields.push_back(ClassFieldDecl{field.spelling, field.nameId, type.spelling, span(fieldBegin, byte_)});
    }
    if (!matchBlockEnd()) throw IntegerParserError("Expected 'end' after class declaration");
    auto result = std::make_shared<ClassStmt>(className.spelling, className.nameId, std::move(parents),
                                               std::move(fields), std::move(indexes), std::move(methods));
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
        auto result = std::make_shared<StringExpr>(consumeString());
        stamp(result, begin, byte_);
        return result;
    }
    if (piece_ < input_.entries().size() && isDecimalDigitId(input_.entries()[piece_].id)) {
        auto result = std::make_shared<NumberExpr>(consumeNumber());
        stamp(result, begin, byte_);
        return result;
    }
    if (match(TokenId::TRUE)) { auto result = makeTruthValue(true); stamp(result, begin, byte_); return result; }
    if (match(TokenId::FALSE)) { auto result = makeTruthValue(false); stamp(result, begin, byte_); return result; }
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
        // Keep dots for parseUnary so `people.get()` is a receiver call,
        // rather than an unrelated qualified callable named `people.get`.
        const auto name = consumeQualifiedName(true, false);
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
        const auto startByte = byte_;
        const auto startPiece = piece_;
        std::shared_ptr<Expr> deferred;
        std::size_t deferredByte = startByte;
        std::size_t deferredPiece = startPiece;
        for (const auto* pattern : operators_->deferredTrailingCapturePatterns()) {
            if (static_cast<int>(pattern->precedence) < minimumPrecedence) continue;
            ++metrics_.backtrackingAttempts;
            byte_ = startByte;
            piece_ = startPiece;
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
            byte_ = startByte;
            piece_ = startPiece;
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
                                         selected->operatorName + "'" + describeLocation(byte_));
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
    while (at(TokenId::DOT) || at(TokenId::COLON)) {
        const auto beforeByte = byte_;
        const auto beforePiece = piece_;
        const auto separator = input_.entries()[piece_].id;
        match(separator);
        const auto separatorEnd = byte_;
        const auto memberKeyword = piece_ < input_.entries().size()
            ? builtinTokenSpelling(input_.entries()[piece_].id) : std::string_view{};
        if ((!atNameRange() && memberKeyword.empty()) || sourceContainsLineBreak(separatorEnd, byte_)) {
            byte_ = beforeByte;
            piece_ = beforePiece;
            break;
        }
        std::string member;
        if (atNameRange()) {
            member = consumeNameRange();
        } else {
            member = std::string(memberKeyword);
            byte_ = input_.entries()[piece_++].end;
        }
        if (!at(TokenId::LPAREN)) {
            result = std::make_shared<AccessExpr>(std::move(result), member);
            continue;
        }
        auto arguments = parseArguments();
        const auto type = std::dynamic_pointer_cast<VarExpr>(result);
        const auto prepend = [&](std::shared_ptr<Expr> value, const char* name) {
            arguments.insert(arguments.begin(), Arg{name, std::move(value)});
        };
        if (type && type->isCapitalized) {
            if (member == "get") {
                if (arguments.size() != 1 ||
                    (arguments.front().name != "pos" &&
                     arguments.front().name != "position" &&
                     arguments.front().name != "index")) {
                    throw IntegerParserError("Type.get requires exactly one named position argument");
                }
                prepend(std::make_shared<StringExpr>(type->name), "type");
                result = std::make_shared<TermExpr>("Fact:first", std::move(arguments), BuiltinId::FactFirst);
                continue;
            }
            if (member == "where") {
                std::vector<MapEntry> fields;
                fields.reserve(arguments.size());
                for (const auto& argument : arguments) {
                    if (argument.name.empty()) throw IntegerParserError("Type.where requires named fields");
                    fields.emplace_back(argument.name, argument.value);
                }
                std::vector<Arg> selectArgs;
                selectArgs.emplace_back("type", std::make_shared<StringExpr>(type->name));
                selectArgs.emplace_back("match", std::make_shared<MapExpr>(std::move(fields)));
                result = std::make_shared<TermExpr>("Fact:where", std::move(selectArgs), BuiltinId::FactSelect);
                continue;
            }
            const std::string qualified = "Fact:" + member;
            const BuiltinId factBuiltin = builtinIdForName(qualified);
            if (factBuiltin == BuiltinId::FactAll ||
                factBuiltin == BuiltinId::FactCount ||
                factBuiltin == BuiltinId::FactInsert ||
                factBuiltin == BuiltinId::FactProject ||
                factBuiltin == BuiltinId::FactSearch ||
                factBuiltin == BuiltinId::FactJoin) {
                const char* receiverName =
                    factBuiltin == BuiltinId::FactProject ||
                    factBuiltin == BuiltinId::FactSearch ||
                    factBuiltin == BuiltinId::FactJoin ? "fact" : "type";
                prepend(std::make_shared<StringExpr>(type->name), receiverName);
                result = std::make_shared<TermExpr>(qualified, std::move(arguments), factBuiltin);
                continue;
            }
            const BuiltinId aggregate = builtinIdForName(member);
            if (aggregate == BuiltinId::Sum || aggregate == BuiltinId::Average ||
                aggregate == BuiltinId::Min || aggregate == BuiltinId::Max) {
                const double operation = aggregate == BuiltinId::Sum ? 0.0 :
                    aggregate == BuiltinId::Average ? 1.0 :
                    aggregate == BuiltinId::Min ? 2.0 : 3.0;
                prepend(std::make_shared<NumberExpr>(operation), "operation");
                prepend(std::make_shared<StringExpr>(type->name), "fact");
                result = std::make_shared<TermExpr>(
                    "Fact:aggregate", std::move(arguments), BuiltinId::FactAggregate);
                continue;
            }
        }
        if (const auto receiver = std::dynamic_pointer_cast<VarExpr>(result)) {
            const std::string qualified = receiver->name + "." + member;
            const BuiltinId builtin = builtinIdForName(qualified);
            if (builtin != BuiltinId::Unknown) {
                result = std::make_shared<TermExpr>(qualified, std::move(arguments), builtin);
                continue;
            }
        }
        if (member == "get" || member == "len" || member == "push") {
            prepend(std::move(result), "data");
            const BuiltinId builtin = member == "get" ? BuiltinId::ArrayGet :
                member == "len" ? BuiltinId::ArrayLen : BuiltinId::ArrayPush;
            result = std::make_shared<TermExpr>("array:" + member, std::move(arguments), builtin);
            continue;
        }
        if (member == "update") {
            prepend(std::move(result), "selection");
            result = std::make_shared<TermExpr>("Fact:update", std::move(arguments), BuiltinId::FactUpdate);
            continue;
        }
        if (member == "AndWhere" || member == "OrWhere") {
            std::vector<MapEntry> fields;
            fields.reserve(arguments.size());
            for (const auto& argument : arguments) {
                if (argument.name.empty()) throw IntegerParserError(member + " requires named fields");
                fields.emplace_back(argument.name, argument.value);
            }
            std::vector<Arg> filterArgs;
            filterArgs.emplace_back("selection", std::move(result));
            filterArgs.emplace_back("match", std::make_shared<MapExpr>(std::move(fields)));
            result = std::make_shared<TermExpr>(
                member == "AndWhere" ? "Fact:andWhere" : "Fact:orWhere",
                std::move(filterArgs), member == "AndWhere"
                    ? BuiltinId::FactAndWhere : BuiltinId::FactOrWhere);
            continue;
        }
        if (member == "limit") {
            prepend(std::move(result), "selection");
            result = std::make_shared<TermExpr>("Fact:limit", std::move(arguments), BuiltinId::FactLimit);
            continue;
        }
        if (member == "delete") {
            prepend(std::move(result), "selection");
            result = std::make_shared<TermExpr>("Fact:delete", std::move(arguments), BuiltinId::FactDelete);
            continue;
        }
        // Type.where(field: value, ...) is handled above, gated on a bare
        // capitalized receiver. Reaching here with `where` means the
        // receiver is some other expression - e.g. join(TypeA, TypeB) -
        // whose rows are plain {left, right} pairs rather than one type's
        // fields, so the single argument is a boolean predicate expression
        // (left.id == right.school_id) evaluated once per row instead of a
        // map of field equalities.
        if (member == "where") {
            if (arguments.size() != 1 || !arguments.front().name.empty()) {
                throw IntegerParserError(
                    "where(...) on a join result expects a single predicate expression, "
                    "e.g. where(left.id == right.school_id)");
            }
            std::vector<Arg> whereArgs;
            whereArgs.emplace_back("selection", std::move(result));
            whereArgs.emplace_back("predicate", std::move(arguments.front().value));
            result = std::make_shared<TermExpr>("Array:where", std::move(whereArgs), BuiltinId::ArrayWhere);
            continue;
        }
        if (member == "order_by") {
            prepend(std::move(result), "selection");
            result = std::make_shared<TermExpr>("Array:orderBy", std::move(arguments), BuiltinId::ArrayOrderBy);
            continue;
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
    return continueBinaryExpression(std::move(left), minimumPrecedence, stop, stopAnchor);
}

// The operator-precedence loop proper, factored out of parseBinaryExpression
// so a value already parsed some other way - e.g. `return (a + b) / c`'s
// leading `(a + b)`, parsed as a return-tuple candidate before it was known
// to be a plain grouped expression instead - can resume the exact same
// continuation instead of a second copy of this precedence climb.
std::shared_ptr<Expr> IntegerParser::continueBinaryExpression(
    std::shared_ptr<Expr> left, int minimumPrecedence, TokenId::Id stop,
    const std::vector<PatternLexeme>* stopAnchor) {
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
    // stamp() calls this for essentially every AST node, so the previous
    // version - which replayed the *entire* token stream from position 0 to
    // recompute line/column state on every single call - was the dominant
    // cost in parsing any file with many statements (profiled at ~23% of
    // total samples parsing a 3000-statement program; confirmed fixed by
    // this rewrite). Line/column state is memoryless: it depends only on how
    // many line breaks occurred strictly before a byte position and where
    // the most recent one was, not on how it got there - so both endpoints
    // can be computed independently via one binary search each into the
    // cached, sorted line-break offsets, instead of one full replay per call.
    const auto& breaks = lineBreakOffsets();
    const auto lineColumnAt = [&](std::size_t pos) -> std::pair<int, int> {
        const auto it = std::lower_bound(breaks.begin(), breaks.end(), pos);
        const auto countBefore = static_cast<int>(it - breaks.begin());
        const std::size_t lineStart = countBefore == 0 ? 0 : breaks[static_cast<std::size_t>(countBefore) - 1] + 1;
        return {1 + countBefore, static_cast<int>(pos - lineStart) + 1};
    };
    SourceSpan result;
    std::tie(result.startLine, result.startColumn) = lineColumnAt(begin);
    std::tie(result.endLine, result.endColumn) = lineColumnAt(end);
    return result;
}

void IntegerParser::stamp(const std::shared_ptr<AstNode>& node,
                          std::size_t begin,
                          std::size_t end) const {
    node->sourceSpan = span(begin, end);
}

} // namespace Felidae
