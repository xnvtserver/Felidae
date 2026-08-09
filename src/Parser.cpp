#include "Parser.h"
#include "BuiltinRegistry.h"
#include "OperatorAnnotation.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <unordered_set>

namespace Felidae {

namespace {
struct TokenChain {
    std::vector<std::string> parts;
    size_t end = 0;
};

TokenChain parseTokenChain(const std::vector<Token>& tokens, size_t pos) {
    TokenChain chain{{}, pos};
    if (pos >= tokens.size() ||
        (tokens[pos].type != TokenType::Ident && tokens[pos].type != TokenType::BuiltinFunction)) {
        return chain;
    }

    chain.parts.push_back(tokens[pos].text);
    chain.end = pos + 1;
    while (chain.end + 1 < tokens.size() &&
           (tokens[chain.end].type == TokenType::Colon ||
            tokens[chain.end].type == TokenType::Dot ||
            tokens[chain.end].type == TokenType::DoubleColon) &&
           (tokens[chain.end + 1].type == TokenType::Ident ||
            tokens[chain.end + 1].type == TokenType::BuiltinFunction) &&
           (tokens[chain.end].type != TokenType::Dot ||
            tokens[chain.end].line == tokens[chain.end + 1].line)) {
        chain.parts.push_back(tokens[chain.end + 1].text);
        chain.end += 2;
    }
    return chain;
}

bool tokenChainEquals(const TokenChain& chain, std::initializer_list<const char*> expected) {
    if (chain.parts.size() != expected.size()) return false;
    size_t i = 0;
    for (const char* part : expected) {
        if (chain.parts[i++] != part) return false;
    }
    return true;
}

bool isAssignmentToChain(const std::vector<Token>& tokens, size_t pos, std::initializer_list<const char*> chainParts) {
    const auto chain = parseTokenChain(tokens, pos);
    return tokenChainEquals(chain, chainParts) &&
           chain.end < tokens.size() &&
           tokens[chain.end].type == TokenType::Bind;
}

bool isNameStartToken(TokenType type) {
    return type == TokenType::Ident || type == TokenType::BuiltinFunction;
}

bool isOperatorAnchorToken(const Token& token) {
    return token.type == TokenType::CustomOperator ||
           (token.type == TokenType::Ident && token.virtualTokenId != 0);
}

bool isExpressionStartToken(TokenType type) {
    switch (type) {
        case TokenType::Ident:
        case TokenType::BuiltinFunction:
        case TokenType::String:
        case TokenType::Number:
        case TokenType::True:
        case TokenType::False:
        case TokenType::Nil:
        case TokenType::LParen:
        case TokenType::LBrace:
        case TokenType::LBracket:
        case TokenType::Minus:
        case TokenType::Plus:
        case TokenType::Not:
            return true;
        default:
            return false;
    }
}

bool isSystemResultExpr(const std::shared_ptr<Expr>& expr) {
    if (auto var = std::dynamic_pointer_cast<VarExpr>(expr)) {
        return var->nameId == InternalSymbol::SystemResultId;
    }
    auto access = std::dynamic_pointer_cast<AccessExpr>(expr);
    if (!access || access->keyId != InternalSymbol::ResultId) return false;
    auto target = std::dynamic_pointer_cast<VarExpr>(access->target);
    return target && target->nameId == InternalSymbol::SystemId;
}

bool containsValueReturnGoal(const std::shared_ptr<Goal>& goal) {
    if (!goal) return false;
    if (auto returned = std::dynamic_pointer_cast<ReturnGoal>(goal)) {
        return !returned->fields.empty();
    }
    if (auto conditional = std::dynamic_pointer_cast<IfGoal>(goal)) {
        for (const auto& nested : conditional->thenBranch) {
            if (containsValueReturnGoal(nested)) return true;
        }
        for (const auto& nested : conditional->elseBranch) {
            if (containsValueReturnGoal(nested)) return true;
        }
    } else if (auto group = std::dynamic_pointer_cast<GroupGoal>(goal)) {
        for (const auto& nested : group->goals) {
            if (containsValueReturnGoal(nested)) return true;
        }
    } else if (auto alternatives = std::dynamic_pointer_cast<OrGoal>(goal)) {
        for (const auto& branch : alternatives->branches) {
            for (const auto& nested : branch) {
                if (containsValueReturnGoal(nested)) return true;
            }
        }
    }
    return false;
}
}

void Parser::ensureToken(size_t index) const {
    while (tokens_.size() <= index && lexer_) {
        syncLexerVirtualAnchors();
        Token token = lexer_->nextToken();
        ++metrics_.tokensLexed;
        const bool end = token.type == TokenType::End;
        rejectUnsupportedToken(token);
        tokens_.push_back(std::move(token));
        if (end) break;
    }
}

void Parser::syncLexerVirtualAnchors() const {
    if (!lexer_) return;
    const std::size_t available = operators_->virtualAnchorCount();
    if (available <= virtualAnchorCount_) return;
    const auto additions = operators_->virtualTokensSince(virtualAnchorCount_);
    lexer_->registerVirtualTokens(additions);
    ++metrics_.virtualTokenSynchronizations;
    metrics_.virtualTokensRegistered += additions.size();
    virtualAnchorCount_ = available;
}

const Token& Parser::tokenAt(size_t index) const {
    ensureToken(index);
    return tokens_[std::min(index, tokens_.size() - 1)];
}

bool Parser::hasToken(size_t index) const {
    ensureToken(index);
    return index < tokens_.size() && tokens_[index].type != TokenType::End;
}

const Token& Parser::peek() const { return tokenAt(pos_); }
const Token& Parser::previous() const { return tokens_[pos_ - 1]; }
bool Parser::check(TokenType type) const { return peek().type == type; }
bool Parser::isAtEnd() const { return peek().type == TokenType::End; }

const Token& Parser::advance() {
    if (!isAtEnd()) pos_++;
    return previous();
}

bool Parser::match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

bool Parser::matchDesignationKeyword() {
    if (!check(TokenType::Ident) || peek().text != "as") return false;
    advance();
    return true;
}

void Parser::consumeLogicalNewline() {
    match(TokenType::Newline);
}

bool Parser::matchGoalSeparator() {
    if (match(TokenType::Comma)) {
        consumeLogicalNewline();
        return true;
    }
    if (match(TokenType::Newline)) {
        consumeLogicalNewline();
        return true;
    }
    return false;
}

bool Parser::isGoalListTerminator() const {
    return check(TokenType::End) ||
           check(TokenType::Dot) ||
           check(TokenType::Pipe) ||
           check(TokenType::RParen) ||
           checkElse();
}

bool Parser::checkElse() const {
    return check(TokenType::Else);
}

void Parser::stampNode(const std::shared_ptr<AstNode>& node,
                       int startLine,
                       int startColumn) {
    if (!node) return;
    const Token& end = pos_ > 0 ? previous() : peek();
    node->sourceSpan = SourceSpan{
        startLine,
        startColumn,
        end.line,
        end.column + static_cast<int>(end.text.size())};
    node->nodeId = ++nodeCounter_;
}

const Token& Parser::consume(TokenType type, const std::string& message) {
    if (check(type)) return advance();
    std::ostringstream oss;
    oss << message << " at " << peek().line << ":" << peek().column
        << ", found " << tokenTypeName(peek().type);
    throw ParserError(oss.str());
}

void Parser::rejectUnsupportedToken(const Token& token) const {
    (void)token;
}

Program Parser::parseProgram() {
    Program program;
    parseProgram([&](std::shared_ptr<Statement> statement) {
        program.addStatement(std::move(statement));
    });
    return program;
}

void Parser::parseProgram(const std::function<void(std::shared_ptr<Statement>)>& consume) {
    if (!lexer_) {
        for (const auto& token : tokens_) rejectUnsupportedToken(token);
    }
    consumeLogicalNewline();
    while (!isAtEnd()) {
        consume(parseStatement());
        consumeLogicalNewline();
        if (lexer_ && pos_ > 0) {
            tokens_.erase(tokens_.begin(), tokens_.begin() + static_cast<std::ptrdiff_t>(pos_));
            pos_ = 0;
        }
    }
}

std::vector<std::shared_ptr<Goal>> Parser::parseQuery() {
    if (!lexer_) {
        for (const auto& token : tokens_) rejectUnsupportedToken(token);
    }
    if (match(TokenType::Question)) {
        // optional query marker
    }
    consumeLogicalNewline();
    auto goals = parseGoalList();
    for (const auto& goal : goals) validateGoalSystemResultUsage(goal);
    consumeLogicalNewline();
    consume(TokenType::End, "Expected end of query");
    return goals;
}

std::shared_ptr<Expr> Parser::parseExpressionText() {
    if (!lexer_) {
        for (const auto& token : tokens_) rejectUnsupportedToken(token);
    }
    consumeLogicalNewline();
    auto expr = parseExpr();
    validateSystemResultUsage(expr, false);
    consumeLogicalNewline();
    consume(TokenType::End, "Expected end of expression");
    return expr;
}

void Parser::bootstrapOperatorPatterns() {
    while (!isAtEnd()) {
        if (!check(TokenType::At)) {
            advance();
            continue;
        }
        Call annotation = parseAnnotation();
        if (annotation.builtinId != BuiltinId::OverloadAnnotation &&
            annotation.builtinId != BuiltinId::MixfixAnnotation) continue;
        ParsedOperatorAnnotation parsed;
        try {
            parsed = decodeOperatorAnnotation(annotation);
        } catch (const std::runtime_error& error) {
            throw ParserError(error.what());
        }
        if (parsed.pattern.empty()) continue;
        OperatorPatternDefinition pattern;
        pattern.operatorName = parsed.operatorName;
        pattern.pattern = parsed.pattern;
        for (const auto& capture : parsed.captures) {
            pattern.captureTypeNames.push_back(capture.type);
        }
        pattern.precedence = parsed.precedence;
        pattern.associativity = parsed.associativity;
        pattern.fixity = parsed.fixity;
        pattern.hasDeclaredFixity = parsed.hasFixity;
        pattern.inferFixityFromPattern = parsed.kind == BuiltinId::MixfixAnnotation;
        pattern.isMixfixDeclaration = parsed.kind == BuiltinId::MixfixAnnotation;
        pattern.visibility = parsed.visibility;
        pattern.module = module_;
        try {
            registerOperatorPattern(std::move(pattern));
        } catch (const std::runtime_error& error) {
            throw ParserError(error.what());
        }
    }
}

const OperatorPatternDefinition& Parser::registerOperatorPattern(OperatorPatternDefinition pattern) {
    const auto& registered = operators_->registerPattern(std::move(pattern));
    if (lexer_) {
        for (const auto& anchor : registered.anchorLexemes) {
            for (const auto& lexeme : anchor) {
                if (lexeme.tokenType == TokenType::Ident) {
                    lexer_->registerVirtualToken({
                        lexeme.symbolId, operators_->virtualTokenId(lexeme.symbolId)});
                    ++metrics_.virtualTokensRegistered;
                }
            }
        }
    }
    virtualAnchorCount_ = operators_->virtualAnchorCount();
    markVirtualAnchorsInBufferedTokens();
    return registered;
}

void Parser::markVirtualAnchorsInBufferedTokens() {
    // Consumed tokens are immutable history. Only unread lookahead may need a new anchor id.
    for (size_t index = pos_; index < tokens_.size(); ++index) {
        auto& token = tokens_[index];
        if (token.type == TokenType::Ident) {
            token.virtualTokenId = operators_->virtualTokenId(token.symbolId);
            ++metrics_.virtualTokensRetagged;
        }
    }
}

std::shared_ptr<Statement> Parser::parseStatement() {
    const int startLine = peek().line;
    const int startColumn = peek().column;
    std::vector<Call> annotations;
    while (check(TokenType::At)) {
        annotations.push_back(parseAnnotation());
        consumeLogicalNewline();
    }
    ensureToken(pos_ + 3);
    if (isAssignmentToChain(tokens_, pos_, {"system", "result"})) {
        throw ParserError("system.result is read-only and can only be read inside a then pipeline");
    }
    if (checkElse()) throw ParserError("'else' is only valid inside method fallback branches");
    if (check(TokenType::Return)) {
        throw ParserError("'return' is only valid inside a method body");
    }
    if (check(TokenType::Where)) {
        throw ParserError("'where' is only valid inside a method body.");
    }
    if (!annotations.empty() &&
        (check(TokenType::Import) ||
         (check(TokenType::Ident) && hasToken(pos_ + 1) && tokenAt(pos_ + 1).type == TokenType::Bind))) {
        throw ParserError("Annotations can only be applied to method declarations");
    }
    std::shared_ptr<Statement> statement;
    if (check(TokenType::Import)) {
        statement = parseImport();
        stampNode(statement, startLine, startColumn);
        return statement;
    }
    if (check(TokenType::Ident) &&
        hasToken(pos_ + 1) &&
        tokenAt(pos_ + 1).type == TokenType::Bind) {
        statement = parseGlobalBinding();
        stampNode(statement, startLine, startColumn);
        return statement;
    }
    statement = parseClause(std::move(annotations));
    stampNode(statement, startLine, startColumn);
    return statement;
}

Call Parser::parseAnnotation() {
    consume(TokenType::At, "Expected '@'");
    const std::string name = parseQualifiedName();
    if (!check(TokenType::LParen)) {
        throw ParserError("Annotation method '" + name + "' requires an argument list");
    }
    const bool previous = parsingOperatorAnnotation_;
    const BuiltinId annotationId = builtinIdForName(name);
    parsingOperatorAnnotation_ =
        annotationId == BuiltinId::OverloadAnnotation ||
        annotationId == BuiltinId::MatcherAnnotation ||
        annotationId == BuiltinId::MixfixAnnotation;
    try {
        Call annotation = parseCallFromName(name, false);
        parsingOperatorAnnotation_ = previous;
        return annotation;
    } catch (...) {
        parsingOperatorAnnotation_ = previous;
        throw;
    }
}

std::shared_ptr<ImportStmt> Parser::parseImport() {
    consume(TokenType::Import, "Expected import");
    std::vector<std::string> paths;
    if (match(TokenType::LParen)) {
        if (!check(TokenType::RParen)) {
            paths.push_back(consume(TokenType::String, "Expected string path in import list").text);
            while (!check(TokenType::RParen) && !isAtEnd()) {
                if (check(TokenType::String)) {
                    throw ParserError("Deprecated import list syntax. Use comma-separated imports like import (\"flibrary\", \"file\", \"math\").");
                }
                consume(TokenType::Comma, "Expected ',' between import paths");
                if (check(TokenType::RParen)) {
                    throw ParserError("Expected string path after ',' in import list");
                }
                paths.push_back(consume(TokenType::String, "Expected string path in import list").text);
            }
        }
        consume(TokenType::RParen, "Expected ')' after import list");
    } else {
        paths.push_back(consume(TokenType::String, "Expected string path after import").text);
    }
    match(TokenType::Dot);
    if (!match(TokenType::Newline) && !isAtEnd()) {
        consume(TokenType::Newline, "Expected a newline after import");
    }
    return std::make_shared<ImportStmt>(std::move(paths));
}

std::shared_ptr<ClauseStmt> Parser::parseClause(std::vector<Call> annotations) {
    annotationBindings_.clear();
    for (const auto& annotation : annotations) prepareOperatorAnnotation(annotation);
    std::string name = parseQualifiedName();
    std::vector<std::string> parentNames;
    if (check(TokenType::Extend)) {
        advance();
        parentNames.push_back(consume(TokenType::Ident, "Expected parent fact/type name after extend").text);
        while (match(TokenType::Comma)) {
            parentNames.push_back(consume(TokenType::Ident, "Expected parent fact/type name after ',' in extend list").text);
        }
    }
    Call head = parseCallFromName(std::move(name), false);
    std::vector<std::string> designations;
    if (matchDesignationKeyword()) {
        std::unordered_set<SymbolId> seen;
        do {
            const auto designation = consume(TokenType::Ident,
                "Expected semantic designation after 'as'");
            if (!seen.insert(symbolIdForName(designation.text)).second) {
                throw ParserError("Duplicate semantic designation '" + designation.text + "'");
            }
            designations.push_back(designation.text);
        } while (match(TokenType::Comma));
    }
    knownTypes_.insert(head.name);
    auto& knownFields = predicateFields_[head.name];
    for (const auto& arg : head.args) {
        if (!arg.name.empty()) knownFields.insert(arg.name);
    }
    std::vector<std::shared_ptr<Goal>> body;
    std::vector<std::vector<std::shared_ptr<Goal>>> fallbackBranches;
    bool emptyDeclaration = false;
    if (match(TokenType::Arrow)) {
        consumeLogicalNewline();
        emptyDeclaration = parseEmptyDeclarationBody();
        if (!emptyDeclaration) parseRuleBody(head, body, fallbackBranches);
    }
    bool bodyHasValueReturn = false;
    for (const auto& goal : body) {
        bodyHasValueReturn = bodyHasValueReturn || containsValueReturnGoal(goal);
    }
    for (const auto& branch : fallbackBranches) {
        for (const auto& goal : branch) {
            bodyHasValueReturn = bodyHasValueReturn || containsValueReturnGoal(goal);
        }
    }
    if (emptyDeclaration || bodyHasValueReturn || !fallbackBranches.empty() ||
        (!body.empty() && (head.name == "main" || isMethodStyleHead(head)))) {
        methodPredicates_.insert(head.name);
    }
    validateClauseBody(head, body, fallbackBranches);
    ClauseKind clauseKind = ClauseKind::Rule;
    if (emptyDeclaration) {
        clauseKind = ClauseKind::NativeDeclaration;
    } else if (body.empty() && fallbackBranches.empty()) {
        clauseKind = head.args.empty() && methodPredicates_.count(head.name) > 0
            ? ClauseKind::EntryCall
            : ClauseKind::Fact;
    } else if (head.name == "main" || bodyHasValueReturn ||
               isMethodStyleHead(head) || !fallbackBranches.empty()) {
        clauseKind = ClauseKind::Method;
    }
    const bool endedByDot = match(TokenType::Dot);
    bool endedByNewline = pos_ > 0 && previous().type == TokenType::Newline;
    if (!endedByNewline) endedByNewline = match(TokenType::Newline);
    if (!endedByDot && !endedByNewline && !isAtEnd()) {
        std::ostringstream oss;
        oss << "Expected a newline after fact/rule at " << peek().line << ":" << peek().column
            << ", found " << tokenTypeName(peek().type);
        throw ParserError(oss.str());
    }
    auto clause = std::make_shared<ClauseStmt>(
        std::move(head),
        std::move(parentNames),
        std::move(body),
        std::move(fallbackBranches),
        emptyDeclaration,
        clauseKind);
    if (!annotations.empty() && clause->clauseKind != ClauseKind::Method) {
        throw ParserError("Annotations can only be applied to complete method declarations");
    }
    clause->annotations = std::move(annotations);
    if (!designations.empty() && !clause->isFact()) {
        throw ParserError("Semantic designations using 'as' are only valid on fact declarations");
    }
    clause->designations = std::move(designations);
    clause->designationIds.reserve(clause->designations.size());
    for (const auto& designation : clause->designations) {
        clause->designationIds.push_back(symbolIdForName(designation));
        knownDesignations_.insert(designation);
    }
    clause->module = module_;
    annotationBindings_.clear();
    return clause;
}

void Parser::prepareOperatorAnnotation(const Call& annotation) {
    if (annotation.builtinId != BuiltinId::OverloadAnnotation &&
        annotation.builtinId != BuiltinId::MixfixAnnotation &&
        annotation.builtinId != BuiltinId::MatcherAnnotation) {
        return;
    }
    ParsedOperatorAnnotation parsed;
    try {
        parsed = decodeOperatorAnnotation(annotation);
    } catch (const std::runtime_error& error) {
        throw ParserError(error.what());
    }
    const bool matcher = annotation.builtinId == BuiltinId::MatcherAnnotation;
    if (matcher && (parsed.hasPrecedence || parsed.hasAssociativity || parsed.hasCardinality ||
                    parsed.hasEffects || parsed.hasResult || parsed.hasFactor || parsed.hasFactors)) {
        throw ParserError(
            "@matcher may declare operator, pattern, type, captures, produces, and visibility only");
    }
    const OperatorPatternDefinition* registeredPtr = nullptr;
    if (parsed.pattern.empty()) {
        try {
            registeredPtr = operators_->findPatternByOperator(parsed.operatorName);
        } catch (const std::runtime_error& error) {
            throw ParserError(error.what());
        }
        if (!registeredPtr) {
            throw ParserError(matcher
                ? "@matcher requires an operator pattern declared by @overload"
                : "Initial operator overload requires 'pattern'");
        }
    } else {
        registeredPtr = operators_->findPattern(parsed.operatorName, parsed.pattern);
        if (!registeredPtr) {
            if (matcher) {
                throw ParserError("@matcher cannot define operator syntax; declare the pattern with @overload first");
            }
            OperatorPatternDefinition pattern;
            pattern.operatorName = parsed.operatorName;
            pattern.pattern = parsed.pattern;
            for (const auto& capture : parsed.captures) {
                pattern.captureTypeNames.push_back(capture.type);
            }
            pattern.precedence = parsed.precedence;
            pattern.associativity = parsed.associativity;
            pattern.fixity = parsed.fixity;
            pattern.hasDeclaredFixity = parsed.hasFixity;
            pattern.inferFixityFromPattern = parsed.kind == BuiltinId::MixfixAnnotation;
            pattern.isMixfixDeclaration = parsed.kind == BuiltinId::MixfixAnnotation;
            pattern.visibility = parsed.visibility;
            pattern.module = module_;
            registeredPtr = &registerOperatorPattern(std::move(pattern));
        } else {
            if ((parsed.hasPrecedence && parsed.precedence != registeredPtr->precedence) ||
                (parsed.hasAssociativity && parsed.associativity != registeredPtr->associativity) ||
                (parsed.hasFixity && parsed.fixity != registeredPtr->fixity)) {
                throw ParserError(
                    "Operator pattern cannot redefine precedence, associativity, or type");
            }
        }
    }
    const auto& registered = *registeredPtr;

    if (matcher) {
        if (parsed.produces.empty()) throw ParserError("@matcher requires 'produces'");
        for (const auto& binding : parsed.captures) annotationBindings_.insert(binding.name);
        annotationBindings_.insert("context");
    } else {
        for (const auto& binding : parsed.captures) annotationBindings_.insert(binding.name);
        for (const auto& binding : parsed.factors) annotationBindings_.insert(binding.name);
    }
    if (parsed.captures.size() != registered.captureNames.size()) {
        throw ParserError("Operator annotation captures must exactly match the pattern captures");
    }
    for (size_t i = 0; i < parsed.captures.size(); ++i) {
        if (parsed.captures[i].name != registered.captureNames[i]) {
            throw ParserError("Operator annotation capture '" + parsed.captures[i].name +
                              "' does not match pattern capture '" + registered.captureNames[i] + "'");
        }
    }
}

bool Parser::parseEmptyDeclarationBody() {
    if (check(TokenType::LParen) &&
        hasToken(pos_ + 1) &&
        tokenAt(pos_ + 1).type == TokenType::RParen) {
        advance();
        consume(TokenType::RParen, "Expected ')' after empty native declaration body");
        return true;
    }
    if (check(TokenType::LBrace) &&
        hasToken(pos_ + 1) &&
        tokenAt(pos_ + 1).type == TokenType::RBrace) {
        advance();
        consume(TokenType::RBrace, "Expected '}' after empty declaration body");
        return true;
    }
    return false;
}

void Parser::parseRuleBody(const Call& head,
                           std::vector<std::shared_ptr<Goal>>& body,
                           std::vector<std::vector<std::shared_ptr<Goal>>>& fallbackBranches) {
    body = parseGoalList();
    consumeLogicalNewline();
    while (checkElse()) {
        advance();
        consumeLogicalNewline();
        if (isAtEnd()) {
            throw ParserError("'else' must be followed by a fallback branch");
        }
        fallbackBranches.push_back(parseGoalList());
        consumeLogicalNewline();
    }
    if (!fallbackBranches.empty()) {
        if (!isMethodStyleHead(head) && head.name != "main") {
            throw ParserError("'else' fallback branches are only supported in method-style rules");
        }
        splitFallbackPrelude(body, fallbackBranches);
    }
}

void Parser::validateClauseBody(const Call& head,
                                const std::vector<std::shared_ptr<Goal>>& body,
                                const std::vector<std::vector<std::shared_ptr<Goal>>>& fallbackBranches) const {
    if (body.empty() && fallbackBranches.empty()) return;
    for (const auto& arg : head.args) {
        if (head.name != "main" && containsAccessExpr(arg.value)) {
            throw ParserError("Rule head fields cannot use member access. Bind a head variable in the body, e.g. Name == e.name");
        }
    }
    validateRuleVars(head, body, fallbackBranches);
    validateExceptionFlow(body);
    for (const auto& goal : body) validateGoalSystemResultUsage(goal);
    for (const auto& branch : fallbackBranches) {
        validateExceptionFlow(branch);
        for (const auto& goal : branch) validateGoalSystemResultUsage(goal);
    }
}

void Parser::validateExceptionFlow(const std::vector<std::shared_ptr<Goal>>& goals) const {
    for (size_t i = 0; i < goals.size(); ++i) {
        const auto& goal = goals[i];
        bool isThrow = false;
        if (auto call = std::dynamic_pointer_cast<CallGoal>(goal)) {
            isThrow = call->call.builtinId == BuiltinId::Throw;
        } else if (auto conditional = std::dynamic_pointer_cast<IfGoal>(goal)) {
            validateExceptionFlow(conditional->thenBranch);
            validateExceptionFlow(conditional->elseBranch);
        } else if (auto group = std::dynamic_pointer_cast<GroupGoal>(goal)) {
            validateExceptionFlow(group->goals);
        } else if (auto alternatives = std::dynamic_pointer_cast<OrGoal>(goal)) {
            for (const auto& branch : alternatives->branches) validateExceptionFlow(branch);
        }
        if (!isThrow) continue;
        if (i + 1 < goals.size() && !std::dynamic_pointer_cast<ReturnGoal>(goals[i + 1])) {
            throw ParserError(
                "Unreachable goal after throw. Route with a callable 'target:' and "
                "end the branch with return");
        }
        if (i + 2 < goals.size()) throw ParserError("Unreachable goals after throw");
    }
}

Call Parser::parseCall() {
    return parseCall(false);
}

Call Parser::parseCall(bool allowPositional) {
    return parseCallFromName(parseQualifiedName(), allowPositional);
}

Call Parser::parseCallFromName(std::string name, bool /*allowPositional*/) {
    consume(TokenType::LParen, "Expected '(' after predicate name");
    std::vector<Arg> args;
    if (!check(TokenType::RParen)) {
        args = parseArgList();
    }
    consume(TokenType::RParen, "Expected ')' after arguments");
    BuiltinId builtinId = builtinIdForName(name);
    if (builtinId == BuiltinId::SystemPrint && args.size() == 1 && args.front().name.empty()) {
        args.front().name = "value";
    }
    return Call{name, std::move(args), builtinId};
}

std::shared_ptr<GlobalBindingStmt> Parser::parseGlobalBinding() {
    std::string name = consume(TokenType::Ident, "Expected binding name").text;
    consume(TokenType::Bind, "Expected ':=' after binding name");
    auto expr = parseExpr();
    validateSystemResultUsage(expr, false);
    match(TokenType::Dot);
    if (!match(TokenType::Newline) && !isAtEnd()) {
        consume(TokenType::Newline, "Expected a newline after global binding");
    }
    globals_.insert(name);
    return std::make_shared<GlobalBindingStmt>(std::move(name), std::move(expr));
}

std::string Parser::parseQualifiedName() {
    size_t scan = pos_;
    while (hasToken(scan + 2) &&
           (tokenAt(scan + 1).type == TokenType::Colon ||
            tokenAt(scan + 1).type == TokenType::Dot ||
            tokenAt(scan + 1).type == TokenType::DoubleColon) &&
           tokenAt(scan + 2).type == TokenType::Ident) {
        scan += 2;
    }
    const auto chain = parseTokenChain(tokens_, pos_);
    if (chain.parts.empty()) consume(TokenType::Ident, "Expected name");
    pos_ = chain.end;
    std::string name = chain.parts.front();
    for (size_t i = 1; i < chain.parts.size(); ++i) name += ":" + chain.parts[i];
    return name;
}

std::vector<Arg> Parser::parseArgList() {
    std::vector<Arg> args;
    do {
        args.push_back(parseArg());
    } while (match(TokenType::Comma));
    return args;
}

Arg Parser::parseArg() {
    // Parsing is incremental, so the separator can still be unread when an
    // argument begins. Ensure enough lookahead before inspecting a qualified
    // field name (for example fx.provenance: "observed").
    ensureToken(pos_ + 3);
    if (isNameStartToken(peek().type)) {
        // Field labels use dot or :: namespaces. A colon is always the
        // label/value delimiter here, otherwise `answer: result` is
        // indistinguishable from a callable-style `answer:result` chain.
        std::vector<std::string> parts{peek().text};
        size_t end = pos_ + 1;
        while (true) {
            ensureToken(end + 1);
            if (tokenAt(end).type != TokenType::Dot &&
                tokenAt(end).type != TokenType::DoubleColon) {
                break;
            }
            if (!isNameStartToken(tokenAt(end + 1).type) ||
                (tokenAt(end).type == TokenType::Dot &&
                 tokenAt(end).line != tokenAt(end + 1).line)) {
                break;
            }
            parts.push_back(tokenAt(end + 1).text);
            end += 2;
        }
        ensureToken(end);
        if (tokenAt(end).type == TokenType::Colon) {
            pos_ = end;
            std::string name = parts.front();
            for (size_t i = 1; i < parts.size(); ++i) name += ":" + parts[i];
            consume(TokenType::Colon, "Expected ':' after argument name");
            if (name == "factor" && isNameStartToken(peek().type) && hasToken(pos_ + 1) &&
                tokenAt(pos_ + 1).type == TokenType::Colon) {
                std::string binding = advance().text;
                consume(TokenType::Colon, "Expected ':' after nested binding name");
                return Arg{name, std::make_shared<MapExpr>(std::vector<MapEntry>{
                    MapEntry{std::move(binding), parseExpr()}})};
            }
            return Arg{name, parseExpr()};
        }
    }
    return Arg{"", parseExpr()};
}

std::shared_ptr<Goal> Parser::parseIfGoal() {
    consume(TokenType::If, "Expected if");
    auto conditionExpr = parseOperatorExpr(
        static_cast<int>(OperatorPrecedence::Control), true);
    std::shared_ptr<Goal> condition;
    if (const auto comparison = std::dynamic_pointer_cast<OperatorExpression>(conditionExpr);
        comparison && isComparisonOperator(comparison->coreOperator) &&
        comparison->captureCount() == 2) {
        condition = std::make_shared<BinaryGoal>(
            comparison->capture(0),
            coreOperatorDefinition(comparison->coreOperator).token,
            comparison->capture(1));
    } else {
        // A boolean expression is a valid condition.  Keep the existing
        // BinaryGoal runtime path by making the truth test explicit.
        condition = std::make_shared<BinaryGoal>(
            std::move(conditionExpr), TokenType::EqEq,
            std::make_shared<BoolExpr>(true));
    }
    consume(TokenType::Then, "Expected 'then' after if condition");
    if (!matchGoalSeparator()) {
        throw ParserError("Expected a newline after 'then' in if condition");
    }
    std::vector<std::shared_ptr<Goal>> thenBranch = parseGoalList();
    std::vector<std::shared_ptr<Goal>> elseBranch;
    consumeLogicalNewline();
    if (checkElse()) {
        advance();
        consumeLogicalNewline();
        if (check(TokenType::Dot) || isAtEnd()) {
            throw ParserError("if else must be followed by a branch");
        }
        elseBranch = parseGoalList();
    }
    return std::make_shared<IfGoal>(
        std::move(condition),
        std::move(thenBranch),
        std::move(elseBranch));
}

std::shared_ptr<BinaryGoal> Parser::comparisonGoal(const std::shared_ptr<Expr>& expr,
                                                   const std::string& message) const {
    const auto op = std::dynamic_pointer_cast<OperatorExpression>(expr);
    if (!op || !isComparisonOperator(op->coreOperator) || op->captureCount() != 2) {
        throw ParserError(message);
    }
    return std::make_shared<BinaryGoal>(
        op->capture(0),
        coreOperatorDefinition(op->coreOperator).token,
        op->capture(1));
}

std::shared_ptr<Goal> Parser::parseGoal() {
    const int startLine = peek().line;
    const int startColumn = peek().column;
    const auto finish = [&](std::shared_ptr<Goal> goal) {
        stampNode(goal, startLine, startColumn);
        return goal;
    };
    ensureToken(pos_ + 8);
    if (isAssignmentToChain(tokens_, pos_, {"system", "result"})) {
        throw ParserError("system.result is read-only and can only be read inside a then pipeline");
    }
    if (check(TokenType::If)) {
        return finish(parseIfGoal());
    }
    if (match(TokenType::Not)) {
        size_t lookahead = pos_;
        if (hasToken(lookahead) && isNameStartToken(tokenAt(lookahead).type)) {
            lookahead = parseTokenChain(tokens_, lookahead).end;
        }
        if (!hasToken(lookahead) || tokenAt(lookahead).type != TokenType::LParen) {
            throw ParserError("Expected predicate call after 'not'");
        }
        return finish(std::make_shared<NotGoal>(parseCall(false)));
    }
    if (match(TokenType::LParen)) {
        auto grouped = parseGoalList();
        consume(TokenType::RParen, "Expected ')' after grouped goals");
        if (grouped.size() == 1) return finish(grouped.front());
        return finish(std::make_shared<GroupGoal>(std::move(grouped)));
    }

    if (check(TokenType::Where)) {
        advance();
        return finish(std::make_shared<WhereGoal>(comparisonGoal(
            parseExpr(), "Expected comparison operator after where expression")));
    }

    if (check(TokenType::Return)) {
        advance();
        std::vector<Arg> fields;
        if (match(TokenType::LParen)) {
            if (!check(TokenType::RParen)) fields = parseArgList();
            consume(TokenType::RParen, "Expected ')' after return fields");
        } else if (!check(TokenType::Newline) && !isAtEnd() && !checkElse()) {
            fields.push_back(Arg{"", parseExpr()});
        }
        return finish(std::make_shared<ReturnGoal>(std::move(fields)));
    }

    if (isMultiAssignmentStart()) {
        auto targets = parseAssignmentTargets();
        consume(TokenType::Bind, "Expected ':=' after assignment targets");
        return finish(std::make_shared<MultiAssignGoal>(std::move(targets), parseExpr()));
    }

    if (check(TokenType::Ident) &&
        hasToken(pos_ + 1) &&
        tokenAt(pos_ + 1).type == TokenType::Bind) {
        std::string name = advance().text;
        consume(TokenType::Bind, "Expected ':=' after assignment variable");
        return finish(std::make_shared<AssignGoal>(std::move(name), parseExpr()));
    }

    // A goal can be a predicate call or an expression comparison.
    size_t lookahead = pos_;
    if (hasToken(lookahead) && isNameStartToken(tokenAt(lookahead).type)) {
        ensureToken(lookahead + 16);
        lookahead = parseTokenChain(tokens_, lookahead).end;
    }
    if (hasToken(lookahead) && tokenAt(lookahead).type == TokenType::LParen) {
        auto expr = parseExpr();
        if (auto op = std::dynamic_pointer_cast<OperatorExpression>(expr);
            op && isComparisonOperator(op->coreOperator)) {
            return finish(comparisonGoal(expr, "Expected comparison expression in goal"));
        }
        auto term = std::dynamic_pointer_cast<TermExpr>(expr);
        if (!term) throw ParserError("Expected predicate call or comparison in goal");
        Call call{term->name, term->args, term->builtinId};
        if (matchDesignationKeyword()) {
            std::unordered_set<SymbolId> seen;
            do {
                const auto designation = consume(TokenType::Ident,
                    "Expected semantic designation after 'as'");
                const SymbolId designationId = symbolIdForName(designation.text);
                if (!seen.insert(designationId).second) {
                    throw ParserError("Duplicate semantic designation '" + designation.text + "'");
                }
                call.designations.push_back(designation.text);
                call.designationIds.push_back(designationId);
            } while (match(TokenType::Comma));
        }
        return finish(std::make_shared<CallGoal>(std::move(call)));
    }

    // A bare type name followed by `as` is a designation-only fact matcher.
    if (hasToken(lookahead) && tokenAt(lookahead).type == TokenType::Ident &&
        tokenAt(lookahead).text == "as") {
        const std::string type = parseQualifiedName();
        Call call{type, {}, builtinIdForName(type)};
        if (!matchDesignationKeyword()) {
            throw ParserError("Expected 'as' after fact type");
        }
        std::unordered_set<SymbolId> seen;
        do {
            const auto designation = consume(TokenType::Ident,
                "Expected semantic designation after 'as'");
            const SymbolId designationId = symbolIdForName(designation.text);
            if (!seen.insert(designationId).second) {
                throw ParserError("Duplicate semantic designation '" + designation.text + "'");
            }
            call.designations.push_back(designation.text);
            call.designationIds.push_back(designationId);
        } while (match(TokenType::Comma));
        return finish(std::make_shared<CallGoal>(std::move(call)));
    }

    return finish(comparisonGoal(parseExpr(), "Expected comparison operator in goal"));
}

bool Parser::isMultiAssignmentStart() const {
    size_t lookahead = pos_;
    if (!hasToken(lookahead) || tokenAt(lookahead).type != TokenType::Ident) return false;
    size_t targetCount = 0;
    while (hasToken(lookahead) && tokenAt(lookahead).type == TokenType::Ident) {
        targetCount++;
        lookahead++;
        if (hasToken(lookahead) && tokenAt(lookahead).type == TokenType::Colon) {
            lookahead++;
            if (!hasToken(lookahead) || tokenAt(lookahead).type != TokenType::Ident) return false;
            lookahead++;
        }
        if (hasToken(lookahead) && tokenAt(lookahead).type == TokenType::Comma) {
            lookahead++;
            continue;
        }
        break;
    }
    return targetCount > 1 && hasToken(lookahead) && tokenAt(lookahead).type == TokenType::Bind;
}

std::vector<AssignmentTarget> Parser::parseAssignmentTargets() {
    std::vector<AssignmentTarget> targets;
    std::set<std::string> seen;
    do {
        std::string name = consume(TokenType::Ident, "Expected assignment target name").text;
        if (!seen.insert(name).second) {
            throw ParserError("Duplicate tuple assignment target '" + name + "'");
        }
        std::string type;
        if (match(TokenType::Colon)) {
            type = consume(TokenType::Ident, "Expected type name after ':' in assignment target").text;
        }
        targets.push_back(AssignmentTarget{std::move(name), std::move(type)});
    } while (match(TokenType::Comma));
    return targets;
}

std::vector<std::shared_ptr<Goal>> Parser::parseGoalConjunction() {
    std::vector<std::shared_ptr<Goal>> goals;
    consumeLogicalNewline();
    while (!isGoalListTerminator()) {
        if (checkElse()) throw ParserError("Dangling 'else' without a preceding method branch");
        auto goal = parseGoal();
        const bool endsMethod = std::dynamic_pointer_cast<ReturnGoal>(goal) != nullptr;
        goals.push_back(std::move(goal));
        if (endsMethod) break;
        if (check(TokenType::Newline)) {
            size_t next = pos_;
            while (hasToken(next) && tokenAt(next).type == TokenType::Newline) ++next;
            size_t cursor = next;
            if (hasToken(cursor) && isNameStartToken(tokenAt(cursor).type)) {
                ++cursor;
                while (hasToken(cursor + 1) &&
                       (tokenAt(cursor).type == TokenType::Dot ||
                        tokenAt(cursor).type == TokenType::Colon ||
                        tokenAt(cursor).type == TokenType::DoubleColon) &&
                       isNameStartToken(tokenAt(cursor + 1).type)) {
                    cursor += 2;
                }
                if (hasToken(cursor) && tokenAt(cursor).type == TokenType::Extend) {
                    ++cursor;
                    if (hasToken(cursor) && isNameStartToken(tokenAt(cursor).type)) ++cursor;
                    while (hasToken(cursor + 1) && tokenAt(cursor).type == TokenType::Comma &&
                           isNameStartToken(tokenAt(cursor + 1).type)) {
                        cursor += 2;
                    }
                }
                if (hasToken(cursor) && tokenAt(cursor).type == TokenType::LParen) {
                    int depth = 0;
                    do {
                        if (tokenAt(cursor).type == TokenType::LParen) ++depth;
                        if (tokenAt(cursor).type == TokenType::RParen) --depth;
                        ++cursor;
                    } while (hasToken(cursor) && depth > 0);
                    if (depth == 0 && hasToken(cursor) &&
                        tokenAt(cursor).type == TokenType::Arrow) {
                        break;
                    }
                }
            }
        }
        if (!matchGoalSeparator()) break;
    }
    if (goals.empty()) {
        std::ostringstream oss;
        oss << "Expected goal at " << peek().line << ":" << peek().column;
        throw ParserError(oss.str());
    }
    return goals;
}

std::vector<std::shared_ptr<Goal>> Parser::parseGoalList() {
    std::vector<std::vector<std::shared_ptr<Goal>>> branches;
    consumeLogicalNewline();
    branches.push_back(parseGoalConjunction());
    consumeLogicalNewline();

    while (match(TokenType::Pipe)) {
        consumeLogicalNewline();
        if (checkElse()) throw ParserError("Dangling 'else' without a preceding method branch");
        branches.push_back(parseGoalConjunction());
        consumeLogicalNewline();
    }

    if (branches.size() == 1) return std::move(branches.front());
    std::vector<std::shared_ptr<Goal>> goals;
    goals.push_back(std::make_shared<OrGoal>(std::move(branches)));
    return goals;
}

void Parser::splitFallbackPrelude(std::vector<std::shared_ptr<Goal>>& primary,
                                  std::vector<std::vector<std::shared_ptr<Goal>>>& fallbackBranches) const {
    size_t firstReturn = primary.size();
    for (size_t i = 0; i < primary.size(); ++i) {
        if (std::dynamic_pointer_cast<ReturnGoal>(primary[i])) {
            firstReturn = i;
            break;
        }
    }

    size_t branchStart = primary.size();
    for (size_t i = firstReturn; i > 0; --i) {
        const auto& goal = primary[i - 1];
        if (std::dynamic_pointer_cast<WhereGoal>(goal) ||
            std::dynamic_pointer_cast<BinaryGoal>(goal)) {
            branchStart = i - 1;
            break;
        }
    }
    if (branchStart == primary.size()) branchStart = 0;

    std::vector<std::shared_ptr<Goal>> firstBranch(primary.begin() + static_cast<std::ptrdiff_t>(branchStart), primary.end());
    primary.erase(primary.begin() + static_cast<std::ptrdiff_t>(branchStart), primary.end());
    fallbackBranches.insert(fallbackBranches.begin(), std::move(firstBranch));
}

std::shared_ptr<Expr> Parser::parseExpr() {
    auto expr = parseOperatorExpr(static_cast<int>(OperatorPrecedence::Control));
    if (check(TokenType::Dot) &&
        hasToken(pos_ + 1) &&
        tokenAt(pos_ + 1).type == TokenType::Then) {
        throw ParserError(
            "Unexpected token after statement terminator: use 'left then right', not 'left.then right'");
    }
    return expr;
}

bool Parser::patternLexemeMatches(size_t position, const PatternLexeme& lexeme) const {
    if (!hasToken(position)) return false;
    const Token& token = tokenAt(position);
    if (token.type != lexeme.tokenType) return false;
    if (lexeme.tokenType != TokenType::Ident) return true;
    const VirtualTokenId expected = operators_->virtualTokenId(lexeme.symbolId);
    return expected != 0 && token.virtualTokenId == expected;
}

namespace {
bool patternTypeCanStart(TokenType tokenType, std::string_view typeName) {
    if (typeName.empty() || typeName == "any" || typeName == "expr" ||
        typeName == "mixfix" || typeName == "stmts") return true;
    // Grouped expressions are typed after their inner expression is parsed.
    // Keep them viable for candidate selection instead of rejecting a valid
    // pattern before the recursive parser can inspect the value.
    if (tokenType == TokenType::LParen) return true;
    if (typeName == "string") return tokenType == TokenType::String;
    if (typeName == "number" || typeName == "int" || typeName == "float" ||
        typeName == "decimal" || typeName == "double") {
        return tokenType == TokenType::Number || tokenType == TokenType::Minus || tokenType == TokenType::Plus;
    }
    if (typeName == "bool" || typeName == "boolean") {
        return tokenType == TokenType::True || tokenType == TokenType::False;
    }
    if (typeName == "array") return tokenType == TokenType::LBracket;
    return tokenType == TokenType::Ident || tokenType == TokenType::BuiltinFunction;
}
}

const OperatorPatternDefinition* Parser::selectScoredPattern(
    const std::vector<const OperatorPatternDefinition*>& candidates,
    size_t start) const {
    ++metrics_.operatorCandidateLookups;
    metrics_.operatorCandidatesScored += candidates.size();
    if (candidates.empty()) return nullptr;
    if (candidates.size() == 1) return candidates.front();

    const OperatorPatternDefinition* best = nullptr;
    int bestScore = -1;
    bool tied = false;
    for (const auto* candidate : candidates) {
        if (!candidate || candidate->anchorLexemes.empty()) continue;
        size_t cursor = start;
        int score = 0;
        bool viable = true;

        auto consumeAnchor = [&](size_t anchorIndex) {
            if (anchorIndex >= candidate->anchorLexemes.size()) return false;
            for (const auto& lexeme : candidate->anchorLexemes[anchorIndex]) {
                if (!patternLexemeMatches(cursor, lexeme)) return false;
                ++cursor;
                score += 10;
            }
            return true;
        };
        auto captureStartsCorrectly = [&](size_t captureIndex) {
            if (!hasToken(cursor)) return false;
            if (captureIndex >= candidate->captureTypeNames.size()) return true;
            return patternTypeCanStart(tokenAt(cursor).type,
                                       candidate->captureTypeNames[captureIndex]);
        };

        // A trailing pattern is selected after its first capture has already
        // been parsed as the left operand. Its first literal anchor is at the
        // current cursor, so do not try to type-check that capture against
        // the anchor token. Runtime overload dispatch validates the complete
        // typed capture set after evaluation.
        size_t firstCapture = 0;
        if (candidate->startsWithCapture) {
            if (!consumeAnchor(0)) viable = false;
            firstCapture = 1;
        } else if (!consumeAnchor(0)) {
            viable = false;
        }
        for (size_t captureIndex = firstCapture;
             viable && captureIndex < candidate->captureNames.size(); ++captureIndex) {
            if (!captureStartsCorrectly(captureIndex)) {
                viable = false;
                break;
            }
            score += 2;
            const auto following = captureIndex < candidate->followingAnchorIndices.size()
                ? candidate->followingAnchorIndices[captureIndex]
                : std::nullopt;
            if (!following) {
                if (hasToken(cursor)) ++cursor;
                continue;
            }
            bool found = false;
            int nesting = 0;
            for (size_t probe = cursor; hasToken(probe); ++probe) {
                const auto type = tokenAt(probe).type;
                if (type == TokenType::Newline && nesting == 0) break;
                if (type == TokenType::LParen || type == TokenType::LBracket || type == TokenType::LBrace) ++nesting;
                else if (type == TokenType::RParen || type == TokenType::RBracket || type == TokenType::RBrace) {
                    if (nesting > 0) --nesting;
                }
                if (nesting == 0) {
                    cursor = probe;
                    if (consumeAnchor(*following)) {
                        found = true;
                        break;
                    }
                }
            }
            if (!found) viable = false;
        }
        if (!viable) continue;
        score += static_cast<int>(candidate->captureNames.size());
        if (best == nullptr || score > bestScore) {
            best = candidate;
            bestScore = score;
            tied = false;
        } else if (score == bestScore) {
            tied = true;
        }
    }
    if (tied && best != nullptr) {
        throw ParserError("Ambiguous operator syntax at '" + peek().text +
                          "'; add a distinguishing anchor or capture type");
    }
    return best;
}

std::shared_ptr<Expr> Parser::tryParseDeferredTrailingCapturePattern(
    const std::shared_ptr<Expr>& firstCapture,
    int minimumPrecedence,
    bool stopAtThen) {
    struct ParsedCandidate {
        const OperatorPatternDefinition* pattern = nullptr;
        std::shared_ptr<Expr> expression;
        size_t end = 0;
    };
    const size_t start = pos_;
    std::vector<ParsedCandidate> parsed;
    for (const auto* pattern : operators_->deferredTrailingCapturePatterns(module_)) {
        if (!pattern) continue;
        pos_ = start;
        try {
            std::vector<OperatorCapture> captures;
            captures.reserve(pattern->captureNames.size());
            captures.emplace_back(pattern->captureNames.front(), firstCapture->clone());
            for (size_t captureIndex = 1; captureIndex < pattern->captureNames.size(); ++captureIndex) {
                const auto following = captureIndex < pattern->followingAnchorIndices.size()
                    ? pattern->followingAnchorIndices[captureIndex]
                    : std::nullopt;
                const PatternLexeme* stopAnchor = nullptr;
                if (following && *following < pattern->anchorLexemes.size() &&
                    !pattern->anchorLexemes[*following].empty()) {
                    stopAnchor = &pattern->anchorLexemes[*following].front();
                }
                const bool adjacentCapture = stopAnchor == nullptr &&
                    captureIndex + 1 < pattern->captureNames.size();
                auto captured = adjacentCapture
                    ? parseUnaryExpr()
                    : parseOperatorExpr(
                        std::max(minimumPrecedence, static_cast<int>(pattern->precedence)),
                        stopAtThen, stopAnchor);
                captures.emplace_back(pattern->captureNames[captureIndex], std::move(captured));
                if (stopAnchor) consumePatternAnchor(pattern->anchorLexemes[*following]);
            }
            auto expression = std::make_shared<OperatorExpression>(
                pattern->operatorId, pattern->patternId, std::move(captures));
            expression->module = module_;
            parsed.push_back(ParsedCandidate{pattern, std::move(expression), pos_});
        } catch (const ParserError&) {
            // This candidate did not consume its required literal anchors.
        }
    }
    pos_ = start;
    if (parsed.empty()) return {};
    std::vector<const OperatorPatternDefinition*> parsedPatterns;
    parsedPatterns.reserve(parsed.size());
    for (const auto& candidate : parsed) parsedPatterns.push_back(candidate.pattern);
    const auto* selected = selectScoredPattern(parsedPatterns, start);
    if (!selected) {
        throw ParserError("Unable to resolve deferred operator syntax at '" + peek().text + "'");
    }
    const auto selectedIt = std::find_if(parsed.begin(), parsed.end(),
        [&](const ParsedCandidate& candidate) { return candidate.pattern == selected; });
    if (selectedIt == parsed.end()) {
        throw ParserError("Deferred operator resolver selected an unavailable pattern");
    }
    pos_ = selectedIt->end;
    return std::move(selectedIt->expression);
}

std::shared_ptr<Expr> Parser::parseOperatorExpr(int minimumPrecedence,
                                               bool stopAtThen,
                                               const PatternLexeme* stopAnchor) {
    const int startLine = peek().line;
    const int startColumn = peek().column;
    auto expr = parseUnaryExpr();
    while (true) {
        const size_t beforeSeparator = pos_;
        consumeLogicalNewline();
        if (stopAnchor && patternLexemeMatches(pos_, *stopAnchor)) {
            pos_ = beforeSeparator;
            break;
        }
        auto definition = infixOperatorForToken(peek().type);
        const auto customPatterns = definition || parsingOperatorAnnotation_ ||
            !isOperatorAnchorToken(peek())
            ? std::vector<const OperatorPatternDefinition*>{}
            : operators_->trailingPatternsForAnchor(peek(), module_);
        const auto* selectedPattern = selectScoredPattern(customPatterns, pos_);
        if (definition && stopAtThen && definition->id == CoreOperator::Then) {
            pos_ = beforeSeparator;
            break;
        }
        const int precedence = definition
            ? static_cast<int>(definition->precedence)
            : selectedPattern == nullptr ? -1 : static_cast<int>(selectedPattern->precedence);
        if (!definition && selectedPattern == nullptr &&
            isExpressionStartToken(peek().type)) {
            if (auto deferred = tryParseDeferredTrailingCapturePattern(
                    expr, minimumPrecedence, stopAtThen)) {
                expr = std::move(deferred);
                continue;
            }
        }
        if (!definition && customPatterns.empty() && stopAnchor == nullptr &&
            peek().type == TokenType::Ident &&
            pos_ > 0 && previous().line == peek().line) {
            throw ParserError("Unknown or inaccessible operator '" + peek().text +
                              "'");
        }
        if (precedence < minimumPrecedence) {
            pos_ = beforeSeparator;
            break;
        }

        advance();
        const auto associativity = definition
            ? definition->associativity : selectedPattern->associativity;
        const int rightMinimum = associativity == OperatorAssociativity::Right
            ? precedence
            : precedence + 1;
        if (definition) {
            auto right = parseOperatorExpr(rightMinimum, stopAtThen, stopAnchor);
            expr = makeOperatorExpr(definition->id, std::move(expr), std::move(right));
        } else {
            const auto& pattern = *selectedPattern;
            consumePatternAnchor(pattern.anchorLexemes.front(), 1);
            std::vector<OperatorCapture> captures;
            captures.emplace_back(pattern.captureNames[0], std::move(expr));
            if (pattern.fixity == OperatorFixity::Postfix) {
                expr = std::make_shared<OperatorExpression>(
                    pattern.operatorId, pattern.patternId, std::move(captures));
                std::static_pointer_cast<OperatorExpression>(expr)->module = module_;
                continue;
            }
            for (size_t captureIndex = 1; captureIndex < pattern.captureNames.size(); ++captureIndex) {
                const PatternLexeme* nextStop = nullptr;
                const auto anchorIndex = captureIndex;
                if (anchorIndex < pattern.anchorLexemes.size() &&
                    !pattern.anchorLexemes[anchorIndex].empty()) {
                    nextStop = &pattern.anchorLexemes[anchorIndex].front();
                }
                const bool adjacentCapture = nextStop == nullptr &&
                    captureIndex + 1 < pattern.captureNames.size();
                auto captured = adjacentCapture
                    ? parseUnaryExpr()
                    : parseOperatorExpr(rightMinimum, stopAtThen, nextStop);
                captures.emplace_back(pattern.captureNames[captureIndex], std::move(captured));
                if (nextStop) {
                    consumePatternAnchor(pattern.anchorLexemes[captureIndex]);
                }
            }
            expr = std::make_shared<OperatorExpression>(
                pattern.operatorId, pattern.patternId, std::move(captures));
            std::static_pointer_cast<OperatorExpression>(expr)->module = module_;
        }
    }
    stampNode(expr, startLine, startColumn);
    return expr;
}

void Parser::consumePatternAnchor(const std::vector<PatternLexeme>& lexemes,
                                  size_t offset) {
    if (offset > lexemes.size()) {
        throw ParserError("Invalid compiled mixfix anchor offset");
    }
    for (size_t index = offset; index < lexemes.size(); ++index) {
        if (!patternLexemeMatches(pos_, lexemes[index])) {
            throw ParserError("Expected operator anchor '" + lexemes[index].spelling + "'");
        }
        advance();
    }
}

std::shared_ptr<Expr> Parser::parseAccessExpr() {
    auto expr = parsePrimaryExpr();
    while ((check(TokenType::Colon) || check(TokenType::Dot)) &&
        hasToken(pos_ + 1) &&
        isNameStartToken(tokenAt(pos_ + 1).type) &&
        (!check(TokenType::Dot) || tokenAt(pos_).line == tokenAt(pos_ + 1).line) &&
        !(hasToken(pos_ + 2) && tokenAt(pos_ + 2).type == TokenType::LParen)) {
        bool dotAccess = check(TokenType::Dot);
        advance(); // ':' or '.'
        if (!isNameStartToken(peek().type)) {
            consume(TokenType::Ident, dotAccess ? "Expected field name after '.'" : "Expected map field name after ':'");
        }
        std::string key = advance().text;
        expr = std::make_shared<AccessExpr>(std::move(expr), std::move(key));
    }
    return expr;
}

std::shared_ptr<Expr> Parser::parseUnaryExpr() {
    auto leadingPatterns = parsingOperatorAnnotation_ ||
        !isOperatorAnchorToken(peek())
        ? std::vector<const OperatorPatternDefinition*>{}
        : operators_->leadingPatternsForAnchor(peek(), module_);
    const auto* selectedPattern = selectScoredPattern(leadingPatterns, pos_);
    if (selectedPattern != nullptr && !selectedPattern->captureNames.empty()) {
        const std::size_t operandStart = pos_ + selectedPattern->anchorLexemes.front().size();
        if (!hasToken(operandStart) || !isExpressionStartToken(tokenAt(operandStart).type)) {
            selectedPattern = nullptr;
        }
    }
    if (selectedPattern != nullptr) {
        const auto& pattern = *selectedPattern;
        consumePatternAnchor(pattern.anchorLexemes.front());
        std::vector<OperatorCapture> captures;
        captures.reserve(pattern.captureNames.size());
        for (size_t captureIndex = 0; captureIndex < pattern.captureNames.size(); ++captureIndex) {
            const PatternLexeme* nextStop = nullptr;
            const auto following = captureIndex < pattern.followingAnchorIndices.size()
                ? pattern.followingAnchorIndices[captureIndex]
                : std::nullopt;
            if (following && *following < pattern.anchorLexemes.size() &&
                !pattern.anchorLexemes[*following].empty()) {
                nextStop = &pattern.anchorLexemes[*following].front();
            }
            const bool adjacentCapture = nextStop == nullptr &&
                captureIndex + 1 < pattern.captureNames.size();
            captures.emplace_back(
                pattern.captureNames[captureIndex],
                adjacentCapture
                    ? parseUnaryExpr()
                    : parseOperatorExpr(
                        static_cast<int>(pattern.precedence), false, nextStop));
            if (nextStop) {
                consumePatternAnchor(pattern.anchorLexemes[*following]);
            }
        }
        auto expression = std::make_shared<OperatorExpression>(
            pattern.operatorId, pattern.patternId, std::move(captures));
        expression->module = module_;
        return expression;
    }
    if (match(TokenType::Not)) {
        auto logical = std::make_shared<OperatorExpression>(
            CoreOperator::LogicalNot, parseUnaryExpr());
        logical->module = module_;
        return logical;
    }
    if (match(TokenType::Minus)) {
        auto operand = parseUnaryExpr();
        if (auto number = std::dynamic_pointer_cast<NumberExpr>(operand)) {
            return std::make_shared<NumberExpr>(-number->value);
        }
        auto unary = std::make_shared<OperatorExpression>(CoreOperator::UnaryMinus, std::move(operand));
        unary->module = module_;
        return unary;
    }
    if (match(TokenType::Plus)) {
        return parseUnaryExpr();
    }
    return parseAccessExpr();
}

std::shared_ptr<Expr> Parser::parsePrimaryExpr() {
    if (match(TokenType::String)) return std::make_shared<StringExpr>(previous().text);
    if (match(TokenType::Number)) return std::make_shared<NumberExpr>(std::stod(previous().text));
    if (match(TokenType::True)) return std::make_shared<BoolExpr>(true);
    if (match(TokenType::False)) return std::make_shared<BoolExpr>(false);
    if (match(TokenType::Nil)) return std::make_shared<NilExpr>();
    if (match(TokenType::LParen)) {
        auto expr = parseExpr();
        consume(TokenType::RParen, "Expected ')' after grouped expression");
        if (auto op = std::dynamic_pointer_cast<OperatorExpression>(expr)) {
            op->explicitlyGrouped = true;
        }
        return expr;
    }
    if (check(TokenType::LBrace)) return parseMapExpr();
    if (check(TokenType::LBracket)) return parseArrayExpr();
    if (check(TokenType::Lambda)) {
        advance();
        if (check(TokenType::LParen)) {
            return parseLambdaExpr();
        }
        return std::make_shared<VarExpr>("lambda");
    }
    if (isNameStartToken(peek().type)) {
        const Token firstNameToken = advance();
        std::string name = firstNameToken.text;
        LanguageTypeId languageTypeId = firstNameToken.languageTypeId;
        size_t nameEnd = pos_;
        while (hasToken(nameEnd + 1) &&
               (tokenAt(nameEnd).type == TokenType::Colon ||
                tokenAt(nameEnd).type == TokenType::Dot ||
                tokenAt(nameEnd).type == TokenType::DoubleColon) &&
               isNameStartToken(tokenAt(nameEnd + 1).type) &&
               (tokenAt(nameEnd).type != TokenType::Dot ||
                tokenAt(nameEnd).line == tokenAt(nameEnd + 1).line)) {
            nameEnd += 2;
        }
        const bool callableReference =
            pos_ < nameEnd && tokenAt(pos_).type == TokenType::DoubleColon;
        if (callableReference) {
            if (nameEnd != pos_ + 2 || tokenAt(nameEnd).type == TokenType::LParen) {
                throw ParserError(
                    "'::' is only valid for a two-part callable reference such as someFunction::Function");
            }
        }
        if ((hasToken(nameEnd) && tokenAt(nameEnd).type == TokenType::LParen) ||
            callableReference) {
            while (pos_ < nameEnd) {
                const TokenType separator = advance().type;
                name += separator == TokenType::DoubleColon ? "::" : ":";
                name += advance().text;
            }
            languageTypeId = LanguageTypeId::Unknown;
        }
        if (match(TokenType::LParen)) {
            std::vector<Arg> args;
            if (!check(TokenType::RParen)) {
                do {
                    args.push_back(parseArg());
                } while (match(TokenType::Comma));
            }
            consume(TokenType::RParen, "Expected ')' after term arguments");
            BuiltinId builtinId = builtinIdForName(name);
            if (builtinId == BuiltinId::SystemPrint && args.size() == 1 && args.front().name.empty()) {
                args.front().name = "value";
            }
            return std::make_shared<TermExpr>(std::move(name), std::move(args), builtinId);
        }
        if (name == "_") {
            return std::make_shared<VarExpr>(makeAnonymousSymbolName(++anonymousCounter_));
        }
        return std::make_shared<VarExpr>(std::move(name), languageTypeId);
    }

    std::ostringstream oss;
    oss << "Expected expression at " << peek().line << ":" << peek().column;
    throw ParserError(oss.str());
}

std::shared_ptr<Expr> Parser::parseLambdaExpr() {
    consume(TokenType::LParen, "Expected '(' after lambda");
    auto source = parseExpr();
    consume(TokenType::Comma, "Expected ',' after lambda source");
    std::string variable = consume(TokenType::Ident, "Expected lambda variable").text;
    consume(TokenType::Arrow, "Expected '=>' after lambda variable");
    auto body = parseExpr();
    consume(TokenType::RParen, "Expected ')' after lambda");
    if (auto comparison = std::dynamic_pointer_cast<OperatorExpression>(body);
        comparison && comparison->captureCount() == 2 &&
        isComparisonOperator(comparison->coreOperator)) {
        return std::make_shared<LambdaExpr>(
            std::move(source),
            std::move(variable),
            comparison->capture(0),
            coreOperatorDefinition(comparison->coreOperator).token,
            comparison->capture(1));
    }
    return std::make_shared<LambdaExpr>(std::move(source), std::move(variable), std::move(body));
}

std::shared_ptr<Expr> Parser::parseMapExpr() {
    consume(TokenType::LBrace, "Expected '{'");
    std::vector<MapEntry> entries;
    if (!check(TokenType::RBrace)) {
        do {
            if (!isNameStartToken(peek().type)) {
                consume(TokenType::Ident, "Expected map key");
            }
            std::string key = advance().text;
            consume(TokenType::Colon, "Expected ':' after map key");
            entries.push_back(MapEntry{std::move(key), parseExpr()});
        } while (match(TokenType::Comma));
    }
    consume(TokenType::RBrace, "Expected '}' after map");
    return std::make_shared<MapExpr>(std::move(entries));
}

std::shared_ptr<Expr> Parser::parseArrayExpr() {
    consume(TokenType::LBracket, "Expected '['");
    std::vector<std::shared_ptr<Expr>> items;
    if (!check(TokenType::RBracket)) {
        do {
            if (isNameStartToken(peek().type) && hasToken(pos_ + 1) &&
                tokenAt(pos_ + 1).type == TokenType::Colon) {
                std::string name = advance().text;
                consume(TokenType::Colon, "Expected ':' after array binding name");
                items.push_back(std::make_shared<MapExpr>(std::vector<MapEntry>{
                    MapEntry{std::move(name), parseExpr()}}));
            } else {
                items.push_back(parseExpr());
            }
        } while (match(TokenType::Comma));
    }
    consume(TokenType::RBracket, "Expected ']' after array");
    return std::make_shared<ArrayExpr>(std::move(items));
}

std::shared_ptr<Expr> Parser::makeOperatorExpr(CoreOperator op,
                                               std::shared_ptr<Expr> left,
                                               std::shared_ptr<Expr> right) const {
    auto leftNumber = std::dynamic_pointer_cast<NumberExpr>(left);
    auto rightNumber = std::dynamic_pointer_cast<NumberExpr>(right);
    if (!leftNumber || !rightNumber || operators_->hasVisiblePattern(corePatternId(op), module_)) {
        auto expression = std::make_shared<OperatorExpression>(op, std::move(left), std::move(right));
        expression->module = module_;
        return expression;
    }
    switch (op) {
        case CoreOperator::Add:
            return std::make_shared<NumberExpr>(leftNumber->value + rightNumber->value);
        case CoreOperator::Subtract:
            return std::make_shared<NumberExpr>(leftNumber->value - rightNumber->value);
        case CoreOperator::Multiply:
            return std::make_shared<NumberExpr>(leftNumber->value * rightNumber->value);
        case CoreOperator::Divide:
            if (std::fabs(rightNumber->value) < 1e-12) {
                throw ParserError("Division by zero in constant expression");
            }
            return std::make_shared<NumberExpr>(leftNumber->value / rightNumber->value);
        case CoreOperator::Modulo:
            if (std::fabs(rightNumber->value) < 1e-12) {
                throw ParserError("Division by zero in constant modulo expression");
            }
            return std::make_shared<NumberExpr>(std::fmod(leftNumber->value, rightNumber->value));
        default:
            break;
    }
    auto expression = std::make_shared<OperatorExpression>(op, std::move(left), std::move(right));
    expression->module = module_;
    return expression;
}

bool Parser::containsAccessExpr(const std::shared_ptr<Expr>& expr) const {
    if (std::dynamic_pointer_cast<AccessExpr>(expr)) return true;
    if (auto op = std::dynamic_pointer_cast<OperatorExpression>(expr)) {
        for (size_t i = 0; i < op->captureCount(); ++i) {
            if (containsAccessExpr(op->capture(i))) return true;
        }
        return false;
    }
    if (auto term = std::dynamic_pointer_cast<TermExpr>(expr)) {
        for (const auto& arg : term->args) {
            if (containsAccessExpr(arg.value)) return true;
        }
    }
    if (auto array = std::dynamic_pointer_cast<ArrayExpr>(expr)) {
        for (const auto& item : array->items) {
            if (containsAccessExpr(item)) return true;
        }
    }
    if (auto lambda = std::dynamic_pointer_cast<LambdaExpr>(expr)) {
        return containsAccessExpr(lambda->source) || containsAccessExpr(lambda->body) ||
               (lambda->right && containsAccessExpr(lambda->right));
    }
    if (auto map = std::dynamic_pointer_cast<MapExpr>(expr)) {
        for (const auto& entry : map->entries) {
            if (containsAccessExpr(entry.value)) return true;
        }
    }
    return false;
}

void Parser::validateSystemResultUsage(const std::shared_ptr<Expr>& expr, bool allowed) const {
    if (!expr) return;
    if (isSystemResultExpr(expr)) {
        if (!allowed) {
            throw ParserError("system.result is only available inside the right side of a then pipeline");
        }
        return;
    }
    if (auto op = std::dynamic_pointer_cast<OperatorExpression>(expr)) {
        const bool isThenOperator =
            op->coreOperator == CoreOperator::Then ||
            op->patternId == corePatternId(CoreOperator::Then);
        for (size_t i = 0; i < op->captureCount(); ++i) {
            const bool captureAllowed = isThenOperator && i == 1;
            validateSystemResultUsage(op->capture(i), captureAllowed ? true : allowed);
        }
        return;
    }
    if (auto term = std::dynamic_pointer_cast<TermExpr>(expr)) {
        for (const auto& arg : term->args) validateSystemResultUsage(arg.value, allowed);
        return;
    }
    if (auto array = std::dynamic_pointer_cast<ArrayExpr>(expr)) {
        for (const auto& item : array->items) validateSystemResultUsage(item, allowed);
        return;
    }
    if (auto map = std::dynamic_pointer_cast<MapExpr>(expr)) {
        for (const auto& entry : map->entries) validateSystemResultUsage(entry.value, allowed);
        return;
    }
    if (auto access = std::dynamic_pointer_cast<AccessExpr>(expr)) {
        validateSystemResultUsage(access->target, allowed);
        return;
    }
    if (auto lambda = std::dynamic_pointer_cast<LambdaExpr>(expr)) {
        validateSystemResultUsage(lambda->source, allowed);
        validateSystemResultUsage(lambda->body, false);
        if (lambda->right) validateSystemResultUsage(lambda->right, false);
    }
}

void Parser::validateGoalSystemResultUsage(const std::shared_ptr<Goal>& goal) const {
    if (!goal) return;
    if (auto call = std::dynamic_pointer_cast<CallGoal>(goal)) {
        for (const auto& arg : call->call.args) validateSystemResultUsage(arg.value, false);
    } else if (auto assign = std::dynamic_pointer_cast<AssignGoal>(goal)) {
        validateSystemResultUsage(assign->expr, false);
    } else if (auto multi = std::dynamic_pointer_cast<MultiAssignGoal>(goal)) {
        validateSystemResultUsage(multi->expr, false);
    } else if (auto binary = std::dynamic_pointer_cast<BinaryGoal>(goal)) {
        validateSystemResultUsage(binary->left, false);
        validateSystemResultUsage(binary->right, false);
    } else if (auto where = std::dynamic_pointer_cast<WhereGoal>(goal)) {
        validateGoalSystemResultUsage(where->condition);
    } else if (auto ifGoal = std::dynamic_pointer_cast<IfGoal>(goal)) {
        validateGoalSystemResultUsage(ifGoal->condition);
        for (const auto& nested : ifGoal->thenBranch) validateGoalSystemResultUsage(nested);
        for (const auto& nested : ifGoal->elseBranch) validateGoalSystemResultUsage(nested);
    } else if (auto ret = std::dynamic_pointer_cast<ReturnGoal>(goal)) {
        for (const auto& field : ret->fields) validateSystemResultUsage(field.value, false);
    } else if (auto group = std::dynamic_pointer_cast<GroupGoal>(goal)) {
        for (const auto& nested : group->goals) validateGoalSystemResultUsage(nested);
    } else if (auto orGoal = std::dynamic_pointer_cast<OrGoal>(goal)) {
        for (const auto& branch : orGoal->branches) {
            for (const auto& nested : branch) validateGoalSystemResultUsage(nested);
        }
    }
}

bool Parser::operatorCaptureAcceptsExpressionData(const OperatorExpression& expression,
                                                  size_t captureIndex) const {
    if (expression.coreOperator != CoreOperator::Unknown) return false;

    const auto overloads = operators_->overloadsForPattern(expression.patternId);
    return std::any_of(overloads.begin(), overloads.end(), [&](const auto* overload) {
        const bool visible = overload->module == module_ ||
                             overload->visibility == OperatorVisibility::Public;
        return visible && captureIndex < overload->captures.size() &&
               overload->captures[captureIndex].languageTypeId == LanguageTypeId::Expr;
    });
}

void Parser::collectExprVars(const std::shared_ptr<Expr>& expr, std::set<std::string>& vars) const {
    if (isSystemResultExpr(expr)) return;
    if (auto var = std::dynamic_pointer_cast<VarExpr>(expr)) {
        if (!isAnonymousSymbolName(var->name)) vars.insert(var->name);
        return;
    }
    if (auto op = std::dynamic_pointer_cast<OperatorExpression>(expr)) {
        // `designation.field > value and otherField == value` keeps the
        // right-hand bare identifier as a field of the same lazy selection,
        // not a local variable. Restrict this to a known designation-led
        // comparison so ordinary undeclared variables still fail validation.
        const auto designationFieldComparison = [&](const std::shared_ptr<Expr>& candidate) {
            const auto comparison = std::dynamic_pointer_cast<OperatorExpression>(candidate);
            if (!comparison || comparison->captureCount() != 2 ||
                !isComparisonOperator(comparison->coreOperator)) return false;
            const auto access = std::dynamic_pointer_cast<AccessExpr>(comparison->capture(0));
            const auto source = access
                ? std::dynamic_pointer_cast<VarExpr>(access->target) : nullptr;
            return source && knownDesignations_.count(source->name) > 0;
        };
        if (op->coreOperator == CoreOperator::LogicalAnd && op->captureCount() == 2 &&
            designationFieldComparison(op->capture(0))) {
            collectExprVars(op->capture(0), vars);
            const auto comparison = std::dynamic_pointer_cast<OperatorExpression>(op->capture(1));
            if (comparison && comparison->captureCount() == 2 &&
                isComparisonOperator(comparison->coreOperator) &&
                std::dynamic_pointer_cast<VarExpr>(comparison->capture(0))) {
                collectExprVars(comparison->capture(1), vars);
                return;
            }
        }
        for (size_t i = 0; i < op->captureCount(); ++i) {
            if (!operatorCaptureAcceptsExpressionData(*op, i)) {
                collectExprVars(op->capture(i), vars);
            }
        }
        return;
    }
    if (auto term = std::dynamic_pointer_cast<TermExpr>(expr)) {
        for (const auto& arg : term->args) {
            auto var = std::dynamic_pointer_cast<VarExpr>(arg.value);
            if (term->builtinId != BuiltinId::Unknown && var && !var->name.empty() &&
                std::isupper(static_cast<unsigned char>(var->name.front()))) {
                continue;
            }
            collectExprVars(arg.value, vars);
        }
        return;
    }
    if (auto array = std::dynamic_pointer_cast<ArrayExpr>(expr)) {
        for (const auto& item : array->items) collectExprVars(item, vars);
        return;
    }
    if (auto map = std::dynamic_pointer_cast<MapExpr>(expr)) {
        for (const auto& entry : map->entries) collectExprVars(entry.value, vars);
        return;
    }
    if (auto access = std::dynamic_pointer_cast<AccessExpr>(expr)) {
        collectExprVars(access->target, vars);
        return;
    }
    if (auto lambda = std::dynamic_pointer_cast<LambdaExpr>(expr)) {
        auto sourceVar = std::dynamic_pointer_cast<VarExpr>(lambda->source);
        if (!sourceVar || sourceVar->name.empty() ||
            !std::isupper(static_cast<unsigned char>(sourceVar->name.front()))) {
            collectExprVars(lambda->source, vars);
        }
        std::set<std::string> nested;
        collectExprVars(lambda->body, nested);
        if (lambda->right) collectExprVars(lambda->right, nested);
        nested.erase(lambda->variable);
        vars.insert(nested.begin(), nested.end());
        return;
    }
}

void Parser::validateGoalVars(const std::shared_ptr<Goal>& goal, std::set<std::string>& declared) const {
    std::set<std::string> used;

    if (auto call = std::dynamic_pointer_cast<CallGoal>(goal)) {
        validateCallFields(call->call);
        if (call->call.args.size() == 1 && call->call.args.front().name.empty() &&
            predicateFields_.count(call->call.name) > 0 &&
            methodPredicates_.count(call->call.name) == 0) {
            throw ParserError(
                "Fact type '" + call->call.name + "' is not implicitly iterable in a single positional method-body goal. " +
                call->call.name + "(...) declarations and named fact queries are supported; use lambda(" +
                call->call.name + ", item => ...) for fact iteration, or iterate an explicit list/array");
        }
        std::set<std::string> directVars;
        for (const auto& arg : call->call.args) {
            if (call->call.name == "throw" && arg.name == "target") continue;
            if (call->call.name == "instanceof" &&
                (arg.name == "type" || arg.name == "parent" || arg.name == "of")) {
                auto typeName = std::dynamic_pointer_cast<VarExpr>(arg.value);
                if (typeName && !typeName->name.empty() &&
                    std::isupper(static_cast<unsigned char>(typeName->name.front()))) {
                    continue;
                }
            }
            if (auto var = std::dynamic_pointer_cast<VarExpr>(arg.value)) {
                if (!isAnonymousSymbolName(var->name)) {
                    directVars.insert(var->name);
                }
            } else {
                collectExprVars(arg.value, used);
            }
        }
        for (const auto& name : used) {
            if (!isDeclaredName(name, declared)) {
                throw ParserError("Variable '" + name + "' is used before declaration. Declare it in the rule head or assign it before use");
            }
        }
        if (call->call.builtinId != BuiltinId::Unknown) {
            for (const auto& name : directVars) {
                if (!isDeclaredName(name, declared)) {
                    throw ParserError("Variable '" + name + "' is used before declaration. Declare it in the rule head or assign it before use");
                }
            }
        } else {
            declared.insert(directVars.begin(), directVars.end());
        }
        return;
    } else if (auto binary = std::dynamic_pointer_cast<BinaryGoal>(goal)) {
        collectExprVars(binary->left, used);
        collectExprVars(binary->right, used);
    } else if (auto where = std::dynamic_pointer_cast<WhereGoal>(goal)) {
        validateGoalVars(where->condition, declared);
        return;
    } else if (auto ifGoal = std::dynamic_pointer_cast<IfGoal>(goal)) {
        validateGoalVars(ifGoal->condition, declared);
        std::set<std::string> thenDeclared = declared;
        for (const auto& nested : ifGoal->thenBranch) validateGoalVars(nested, thenDeclared);
        std::set<std::string> elseDeclared = declared;
        for (const auto& nested : ifGoal->elseBranch) validateGoalVars(nested, elseDeclared);
        return;
    } else if (auto ret = std::dynamic_pointer_cast<ReturnGoal>(goal)) {
        for (const auto& field : ret->fields) collectExprVars(field.value, used);
    } else if (auto assign = std::dynamic_pointer_cast<AssignGoal>(goal)) {
        if (assign->expr) collectExprVars(assign->expr, used);
        for (const auto& name : used) {
            if (!isDeclaredName(name, declared)) {
                throw ParserError("Variable '" + name + "' is used before declaration. Declare it in the rule head or assign it before use");
            }
        }
        declared.insert(assign->name);
        return;
    } else if (auto multi = std::dynamic_pointer_cast<MultiAssignGoal>(goal)) {
        collectExprVars(multi->expr, used);
        for (const auto& name : used) {
            if (!isDeclaredName(name, declared)) {
                throw ParserError("Variable '" + name + "' is used before declaration. Declare it in the rule head or assign it before use");
            }
        }
        for (const auto& target : multi->targets) {
            if (!target.type.empty() && !isFelidaeBuiltinTypeName(target.type)) {
                throw ParserError("Tuple assignment target '" + target.name + "' uses unsupported type annotation '" + target.type + "'");
            }
            declared.insert(target.name);
        }
        return;
    } else if (auto group = std::dynamic_pointer_cast<GroupGoal>(goal)) {
        for (const auto& nested : group->goals) validateGoalVars(nested, declared);
        return;
    } else if (auto orGoal = std::dynamic_pointer_cast<OrGoal>(goal)) {
        for (const auto& branch : orGoal->branches) {
            std::set<std::string> branchDeclared = declared;
            for (const auto& nested : branch) validateGoalVars(nested, branchDeclared);
        }
        return;
    }

    for (const auto& name : used) {
        if (!isDeclaredName(name, declared)) {
            throw ParserError("Variable '" + name + "' is used before declaration. Declare it in the rule head or assign it before use");
        }
    }
}

bool Parser::isDeclaredName(const std::string& name, const std::set<std::string>& declared) const {
    return declared.count(name) > 0 || globals_.count(name) > 0 ||
        annotationBindings_.count(name) > 0 || knownDesignations_.count(name) > 0;
}

void Parser::validateRuleVars(const Call& head,
                              const std::vector<std::shared_ptr<Goal>>& body,
                              const std::vector<std::vector<std::shared_ptr<Goal>>>& fallbackBranches) const {
    std::set<std::string> declared;
    declared.insert(annotationBindings_.begin(), annotationBindings_.end());
    const bool methodStyle = head.name == "main" || isMethodStyleHead(head);
    for (const auto& arg : head.args) {
        if (methodStyle && !arg.name.empty()) declared.insert(arg.name);
        auto var = std::dynamic_pointer_cast<VarExpr>(arg.value);
        if (var && !var->name.empty() && !isAnonymousSymbolName(var->name)) {
            if (isFelidaeLikelyTypeName(var->name)) {
                if (!isTypeNameKnown(var->name)) {
                    throw ParserError("Unknown type annotation '" + var->name + "' in rule head");
                }
            } else {
                declared.insert(var->name);
            }
        } else if (!methodStyle) {
            collectExprVars(arg.value, declared);
        }
    }
    for (const auto& goal : body) validateGoalVars(goal, declared);
    for (const auto& branch : fallbackBranches) {
        std::set<std::string> branchDeclared = declared;
        for (const auto& goal : branch) validateGoalVars(goal, branchDeclared);
    }
}

bool Parser::isMethodStyleHead(const Call& head) const {
    if (head.args.empty()) return false;
    for (const auto& arg : head.args) {
        if (!arg.name.empty() && !typeAnnotationName(arg.value).empty()) {
            return true;
        }
    }
    for (const auto& arg : head.args) {
        if (typeAnnotationName(arg.value).empty()) return false;
    }
    return true;
}

std::string Parser::typeAnnotationName(const std::shared_ptr<Expr>& expression) const {
    if (auto variable = std::dynamic_pointer_cast<VarExpr>(expression)) {
        return isFelidaeTypeAnnotationName(variable->name) ? variable->name : std::string{};
    }
    return {};
}

bool Parser::isTypeNameKnown(const std::string& name) const {
    // Comparison is the built-in fact-compatible result type of
    // Relation.compare.  It is deliberately not a scalar/builtin value type:
    // normal methods such as Comparison.membership dispatch through the
    // ordinary named-fact type path.
    // Core result values are available through core/exception.fx. Treat their
    // names as standard annotations during parsing so a source can write a
    // typed Result handler without declaring a duplicate local fact solely to
    // satisfy ordered import parsing.
    return name == "Comparison" || name == "Exception" || name == "Result" ||
           isFelidaeBuiltinTypeName(name) || knownTypes_.count(name) > 0;
}

void Parser::validateCallFields(const Call& call) const {
    if (call.builtinId != BuiltinId::Unknown) return;
    if (methodPredicates_.count(call.name) > 0) return;
    auto it = predicateFields_.find(call.name);
    if (it == predicateFields_.end()) return;
    for (const auto& arg : call.args) {
        if (!arg.name.empty() && it->second.count(arg.name) == 0) {
            throw ParserError("Unknown field '" + arg.name + "' for " + call.name);
        }
    }
}

bool Parser::isComparison(TokenType type) const {
    return type == TokenType::EqEq || type == TokenType::NotEq ||
           type == TokenType::LT || type == TokenType::LTE ||
           type == TokenType::GT || type == TokenType::GTE;
}

} // namespace Felidae
