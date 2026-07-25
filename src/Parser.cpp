#include "Parser.h"
#include "BuiltinRegistry.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

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
            tokens[chain.end].type == TokenType::Dot) &&
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
        Token token = lexer_->nextToken();
        const bool end = token.type == TokenType::End;
        rejectUnsupportedToken(token);
        tokens_.push_back(std::move(token));
        if (end) break;
    }
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

const Token& Parser::consume(TokenType type, const std::string& message) {
    if (check(type)) return advance();
    std::ostringstream oss;
    oss << message << " at " << peek().line << ":" << peek().column
        << ", found " << tokenTypeName(peek().type);
    throw ParserError(oss.str());
}

void Parser::rejectUnsupportedToken(const Token& token) const {
    if (token.type == TokenType::DoubleColon) {
        std::ostringstream oss;
        oss << "'::' is not supported in Felidae. Use '.' for top-level package/module calls at "
            << token.line << ":" << token.column;
        throw ParserError(oss.str());
    }
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

std::shared_ptr<Statement> Parser::parseStatement() {
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
    if (check(TokenType::Import)) return parseImport();
    if (check(TokenType::Ident) &&
        hasToken(pos_ + 1) &&
        tokenAt(pos_ + 1).type == TokenType::Bind) {
        return parseGlobalBinding();
    }
    return parseClause();
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

std::shared_ptr<ClauseStmt> Parser::parseClause() {
    std::string name = parseQualifiedName();
    std::string parentName;
    if (check(TokenType::Extend)) {
        advance();
        parentName = consume(TokenType::Ident, "Expected parent fact/type name after extend").text;
    }
    Call head = parseCallFromName(std::move(name), false);
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
    return std::make_shared<ClauseStmt>(
        std::move(head),
        std::move(parentName),
        std::move(body),
        std::move(fallbackBranches),
        emptyDeclaration,
        clauseKind);
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
    for (const auto& goal : body) validateGoalSystemResultUsage(goal);
    for (const auto& branch : fallbackBranches) {
        for (const auto& goal : branch) validateGoalSystemResultUsage(goal);
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
           (tokenAt(scan + 1).type == TokenType::Colon || tokenAt(scan + 1).type == TokenType::Dot) &&
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
    if (isNameStartToken(peek().type)) {
        // named argument: name: expr
        if (hasToken(pos_ + 1) &&
            tokenAt(pos_ + 1).type == TokenType::Colon) {
            std::string name = advance().text;
            consume(TokenType::Colon, "Expected ':' after argument name");
            return Arg{name, parseExpr()};
        }
    }
    return Arg{"", parseExpr()};
}

std::shared_ptr<Goal> Parser::parseIfGoal() {
    consume(TokenType::If, "Expected if");
    auto left = parseExpr();
    if (!isComparison(peek().type)) {
        throw ParserError("Expected comparison operator after if expression");
    }
    TokenType op = advance().type;
    auto right = parseExpr();
    matchGoalSeparator();
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
        std::make_shared<BinaryGoal>(std::move(left), std::move(op), std::move(right)),
        std::move(thenBranch),
        std::move(elseBranch));
}

std::shared_ptr<Goal> Parser::parseGoal() {
    ensureToken(pos_ + 8);
    if (isAssignmentToChain(tokens_, pos_, {"system", "result"})) {
        throw ParserError("system.result is read-only and can only be read inside a then pipeline");
    }
    if (check(TokenType::If)) {
        return parseIfGoal();
    }
    if (match(TokenType::LParen)) {
        auto grouped = parseGoalList();
        consume(TokenType::RParen, "Expected ')' after grouped goals");
        if (grouped.size() == 1) return grouped.front();
        return std::make_shared<GroupGoal>(std::move(grouped));
    }

    if (check(TokenType::Where)) {
        advance();
        auto left = parseExpr();
        if (!isComparison(peek().type)) {
            throw ParserError("Expected comparison operator after where expression");
        }
        TokenType op = advance().type;
        auto right = parseExpr();
        return std::make_shared<WhereGoal>(
            std::make_shared<BinaryGoal>(std::move(left), std::move(op), std::move(right)));
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
        return std::make_shared<ReturnGoal>(std::move(fields));
    }

    if (isMultiAssignmentStart()) {
        auto targets = parseAssignmentTargets();
        consume(TokenType::Bind, "Expected ':=' after assignment targets");
        return std::make_shared<MultiAssignGoal>(std::move(targets), parseExpr());
    }

    if (check(TokenType::Ident) &&
        hasToken(pos_ + 1) &&
        tokenAt(pos_ + 1).type == TokenType::Bind) {
        std::string name = advance().text;
        consume(TokenType::Bind, "Expected ':=' after assignment variable");
        size_t lookahead = pos_;
    if (hasToken(lookahead) && isNameStartToken(tokenAt(lookahead).type)) {
        ensureToken(lookahead + 16);
        lookahead = parseTokenChain(tokens_, lookahead).end;
    }
    if (hasToken(lookahead) && tokenAt(lookahead).type == TokenType::LParen &&
        (hasToken(pos_ + 1) && tokenAt(pos_ + 1).type == TokenType::Dot)) {
            return std::make_shared<AssignGoal>(std::move(name), parseGoal());
        }
        return std::make_shared<AssignGoal>(std::move(name), parseExpr());
    }

    // A goal can be a predicate call or an expression comparison.
    size_t lookahead = pos_;
    if (hasToken(lookahead) && isNameStartToken(tokenAt(lookahead).type)) {
        ensureToken(lookahead + 16);
        lookahead = parseTokenChain(tokens_, lookahead).end;
    }
    if (hasToken(lookahead) && tokenAt(lookahead).type == TokenType::LParen) {
        size_t saved = pos_;
        auto expr = parseExpr();
        if (isComparison(peek().type)) {
            TokenType op = advance().type;
            auto right = parseExpr();
            return std::make_shared<BinaryGoal>(std::move(expr), std::move(op), std::move(right));
        }
        pos_ = saved;
        return std::make_shared<CallGoal>(parseCall(false));
    }

    auto left = parseExpr();
    if (!isComparison(peek().type)) {
        throw ParserError("Expected comparison operator in goal");
    }
    TokenType op = advance().type;
    auto right = parseExpr();
    return std::make_shared<BinaryGoal>(std::move(left), std::move(op), std::move(right));
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
                        tokenAt(cursor).type == TokenType::Colon) &&
                       isNameStartToken(tokenAt(cursor + 1).type)) {
                    cursor += 2;
                }
                if (hasToken(cursor) && tokenAt(cursor).type == TokenType::Extend) {
                    cursor += 2;
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
    auto expr = parseAccessExpr();
    if (check(TokenType::Dot) &&
        hasToken(pos_ + 1) &&
        tokenAt(pos_ + 1).type == TokenType::Then) {
        throw ParserError(
            "Unexpected token after statement terminator: use 'left then right', not 'left.then right'");
    }
    while (true) {
        size_t beforeThen = pos_;
        consumeLogicalNewline();
        if (!match(TokenType::Then)) {
            pos_ = beforeThen;
            break;
        }
        expr = std::make_shared<PipelineExpr>(std::move(expr), parseAccessExpr());
    }
    return expr;
}

std::shared_ptr<Expr> Parser::parseAccessExpr() {
    auto expr = parseAdditiveExpr();
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

std::shared_ptr<Expr> Parser::parseAdditiveExpr() {
    auto expr = parseMultiplicativeExpr();
    while (check(TokenType::Plus) || check(TokenType::Minus)) {
        TokenType op = advance().type;
        expr = foldConstantBinary(std::move(expr), op, parseMultiplicativeExpr());
    }
    return expr;
}

std::shared_ptr<Expr> Parser::parseMultiplicativeExpr() {
    auto expr = parseUnaryExpr();
    while (check(TokenType::Star) || check(TokenType::Slash)) {
        TokenType op = advance().type;
        expr = foldConstantBinary(std::move(expr), op, parseUnaryExpr());
    }
    return expr;
}

std::shared_ptr<Expr> Parser::parseUnaryExpr() {
    if (match(TokenType::Minus)) {
        auto operand = parseUnaryExpr();
        if (auto number = std::dynamic_pointer_cast<NumberExpr>(operand)) {
            return std::make_shared<NumberExpr>(-number->value);
        }
        return std::make_shared<BinaryExpr>(std::make_shared<NumberExpr>(0.0), TokenType::Minus, std::move(operand));
    }
    if (match(TokenType::Plus)) {
        return parseUnaryExpr();
    }
    return parsePrimaryExpr();
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
               (tokenAt(nameEnd).type == TokenType::Colon || tokenAt(nameEnd).type == TokenType::Dot) &&
               isNameStartToken(tokenAt(nameEnd + 1).type) &&
               (tokenAt(nameEnd).type != TokenType::Dot ||
                tokenAt(nameEnd).line == tokenAt(nameEnd + 1).line)) {
            nameEnd += 2;
        }
        if (hasToken(nameEnd) && tokenAt(nameEnd).type == TokenType::LParen) {
            while (pos_ < nameEnd) {
                advance(); // ':' or '.'
                name += ":" + advance().text;
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
    if (isComparison(peek().type)) {
        TokenType op = advance().type;
        auto right = parseExpr();
        consume(TokenType::RParen, "Expected ')' after lambda");
        return std::make_shared<LambdaExpr>(std::move(source), std::move(variable), std::move(body), std::move(op), std::move(right));
    }
    consume(TokenType::RParen, "Expected ')' after lambda");
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
            items.push_back(parseExpr());
        } while (match(TokenType::Comma));
    }
    consume(TokenType::RBracket, "Expected ']' after array");
    return std::make_shared<ArrayExpr>(std::move(items));
}

std::shared_ptr<Expr> Parser::foldConstantBinary(std::shared_ptr<Expr> left,
                                                 TokenType op,
                                                 std::shared_ptr<Expr> right) const {
    auto leftNumber = std::dynamic_pointer_cast<NumberExpr>(left);
    auto rightNumber = std::dynamic_pointer_cast<NumberExpr>(right);
    if (!leftNumber || !rightNumber) {
        return std::make_shared<BinaryExpr>(std::move(left), op, std::move(right));
    }
    switch (op) {
        case TokenType::Plus:
            return std::make_shared<NumberExpr>(leftNumber->value + rightNumber->value);
        case TokenType::Minus:
            return std::make_shared<NumberExpr>(leftNumber->value - rightNumber->value);
        case TokenType::Star:
            return std::make_shared<NumberExpr>(leftNumber->value * rightNumber->value);
        case TokenType::Slash:
            if (std::fabs(rightNumber->value) < 1e-12) {
                throw ParserError("Division by zero in constant expression");
            }
            return std::make_shared<NumberExpr>(leftNumber->value / rightNumber->value);
        default:
            break;
    }
    return std::make_shared<BinaryExpr>(std::move(left), op, std::move(right));
}

bool Parser::containsAccessExpr(const std::shared_ptr<Expr>& expr) const {
    if (std::dynamic_pointer_cast<AccessExpr>(expr)) return true;
    if (auto pipeline = std::dynamic_pointer_cast<PipelineExpr>(expr)) {
        return containsAccessExpr(pipeline->left) || containsAccessExpr(pipeline->right);
    }
    if (auto binary = std::dynamic_pointer_cast<BinaryExpr>(expr)) {
        return containsAccessExpr(binary->left) || containsAccessExpr(binary->right);
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
    if (auto pipeline = std::dynamic_pointer_cast<PipelineExpr>(expr)) {
        validateSystemResultUsage(pipeline->left, allowed);
        validateSystemResultUsage(pipeline->right, true);
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
    if (auto binary = std::dynamic_pointer_cast<BinaryExpr>(expr)) {
        validateSystemResultUsage(binary->left, allowed);
        validateSystemResultUsage(binary->right, allowed);
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
        validateGoalSystemResultUsage(assign->goal);
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

void Parser::collectExprVars(const std::shared_ptr<Expr>& expr, std::set<std::string>& vars) const {
    if (isSystemResultExpr(expr)) return;
    if (auto var = std::dynamic_pointer_cast<VarExpr>(expr)) {
        if (!isAnonymousSymbolName(var->name)) vars.insert(var->name);
        return;
    }
    if (auto pipeline = std::dynamic_pointer_cast<PipelineExpr>(expr)) {
        collectExprVars(pipeline->left, vars);
        collectExprVars(pipeline->right, vars);
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
    if (auto binary = std::dynamic_pointer_cast<BinaryExpr>(expr)) {
        collectExprVars(binary->left, vars);
        collectExprVars(binary->right, vars);
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
        if (assign->goal) validateGoalVars(assign->goal, declared);
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
    return declared.count(name) > 0 || globals_.count(name) > 0;
}

void Parser::validateRuleVars(const Call& head,
                              const std::vector<std::shared_ptr<Goal>>& body,
                              const std::vector<std::vector<std::shared_ptr<Goal>>>& fallbackBranches) const {
    std::set<std::string> declared;
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
        auto var = std::dynamic_pointer_cast<VarExpr>(arg.value);
        if (!arg.name.empty() && var && isFelidaeTypeAnnotationName(var->name)) {
            return true;
        }
    }
    for (const auto& arg : head.args) {
        auto var = std::dynamic_pointer_cast<VarExpr>(arg.value);
        if (!var || !isFelidaeTypeAnnotationName(var->name)) return false;
    }
    return true;
}

bool Parser::isTypeNameKnown(const std::string& name) const {
    return isFelidaeBuiltinTypeName(name) || knownTypes_.count(name) > 0;
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
