#include "Parser.h"
#include <cctype>
#include <sstream>

namespace Felidae {

namespace {
bool isBuiltinTypeName(const std::string& name) {
    static const std::set<std::string> names = {
        "any", "array", "bool", "boolean", "decimal", "double", "float", "int", "number", "string"
    };
    return names.count(name) > 0;
}

bool isTypeAnnotationName(const std::string& name) {
    return !name.empty() &&
           (std::isupper(static_cast<unsigned char>(name.front())) || isBuiltinTypeName(name));
}
}

const Token& Parser::peek() const { return tokens_[pos_]; }
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

bool Parser::checkElse() const {
    return check(TokenType::Ident) && peek().text == "else";
}

const Token& Parser::consume(TokenType type, const std::string& message) {
    if (check(type)) return advance();
    std::ostringstream oss;
    oss << message << " at " << peek().line << ":" << peek().column
        << ", found " << tokenTypeName(peek().type);
    throw ParserError(oss.str());
}

void Parser::rejectUnsupportedTokens() const {
    for (const auto& token : tokens_) {
        if (token.type == TokenType::DoubleColon) {
            std::ostringstream oss;
            oss << "'::' is not supported in Felidae. Use '.' for top-level package/module calls at "
                << token.line << ":" << token.column;
            throw ParserError(oss.str());
        }
    }
}

Program Parser::parseProgram() {
    rejectUnsupportedTokens();
    Program program;
    while (!isAtEnd()) {
        program.statements.push_back(parseStatement());
    }
    return program;
}

std::vector<std::shared_ptr<Goal>> Parser::parseQuery() {
    rejectUnsupportedTokens();
    if (match(TokenType::Question)) {
        // optional query marker
    }
    auto goals = parseGoalList();
    if (match(TokenType::Dot)) {
        // optional ending dot
    }
    consume(TokenType::End, "Expected end of query");
    return goals;
}

std::shared_ptr<Expr> Parser::parseExpressionText() {
    rejectUnsupportedTokens();
    auto expr = parseExpr();
    if (match(TokenType::Dot)) {
        // optional expression terminator for REPL convenience
    }
    consume(TokenType::End, "Expected end of expression");
    return expr;
}

std::shared_ptr<Statement> Parser::parseStatement() {
    if (checkElse()) throw ParserError("'else' is only valid inside method fallback branches");
    if (check(TokenType::Ident) && peek().text == "return") {
        throw ParserError("'return' is only valid inside a method body. If this follows another goal, separate the previous goal with ',' instead of ending it with '.'.");
    }
    if (check(TokenType::Ident) && peek().text == "where") {
        throw ParserError("'where' is only valid inside a method body.");
    }
    if (check(TokenType::Import)) return parseImport();
    if (check(TokenType::Ident) &&
        pos_ + 1 < tokens_.size() &&
        tokens_[pos_ + 1].type == TokenType::Bind) {
        return parseGlobalBinding();
    }
    return parseClause();
}

std::shared_ptr<ImportStmt> Parser::parseImport() {
    consume(TokenType::Import, "Expected import");
    std::vector<std::string> paths;
    if (match(TokenType::LParen)) {
        do {
            paths.push_back(consume(TokenType::String, "Expected string path in import list").text);
        } while (!check(TokenType::RParen) && !isAtEnd());
        consume(TokenType::RParen, "Expected ')' after import list");
    } else {
        paths.push_back(consume(TokenType::String, "Expected string path after import").text);
    }
    consume(TokenType::Dot, "Expected '.' after import");
    return std::make_shared<ImportStmt>(std::move(paths));
}

std::shared_ptr<ClauseStmt> Parser::parseClause() {
    std::string name = parseQualifiedName();
    std::string parentName;
    if (check(TokenType::Ident) && peek().text == "extend") {
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
        if (check(TokenType::LParen) &&
            pos_ + 1 < tokens_.size() &&
            tokens_[pos_ + 1].type == TokenType::RParen) {
            advance();
            consume(TokenType::RParen, "Expected ')' after empty native declaration body");
            emptyDeclaration = true;
        } else if (check(TokenType::LBrace) &&
                   pos_ + 1 < tokens_.size() &&
                   tokens_[pos_ + 1].type == TokenType::RBrace) {
            advance();
            consume(TokenType::RBrace, "Expected '}' after empty declaration body");
            emptyDeclaration = true;
        } else {
            body = parseGoalList();
            while (checkElse()) {
                advance();
                if (check(TokenType::Dot) || isAtEnd()) {
                    throw ParserError("'else' must be followed by a fallback branch");
                }
                fallbackBranches.push_back(parseGoalList());
            }
            if (!fallbackBranches.empty()) {
                if (!isMethodStyleHead(head) && head.name != "main") {
                    throw ParserError("'else' fallback branches are only supported in method-style rules");
                }
                splitFallbackPrelude(body, fallbackBranches);
            }
        }
    }
    if (emptyDeclaration || (!body.empty() && (head.name == "main" || isMethodStyleHead(head)))) {
        methodPredicates_.insert(head.name);
    }
    if (!body.empty() || !fallbackBranches.empty()) {
        for (const auto& arg : head.args) {
            if (head.name != "main" && containsAccessExpr(arg.value)) {
                throw ParserError("Rule head fields cannot use member access. Bind a head variable in the body, e.g. Name == e.name");
            }
        }
        validateRuleVars(head, body, fallbackBranches);
    }
    const Token& terminator = consume(TokenType::Dot, "Expected '.' after fact/rule");
    if (!isAtEnd() && peek().line == terminator.line) {
        throw ParserError("Unexpected token after statement terminator '.' on the same line");
    }
    return std::make_shared<ClauseStmt>(std::move(head), std::move(parentName), std::move(body), std::move(fallbackBranches), emptyDeclaration);
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
    if (name == "system:print" && args.size() == 1 && args.front().name.empty()) {
        args.front().name = "value";
    }
    return Call{name, std::move(args)};
}

std::shared_ptr<GlobalBindingStmt> Parser::parseGlobalBinding() {
    std::string name = consume(TokenType::Ident, "Expected binding name").text;
    consume(TokenType::Bind, "Expected ':=' after binding name");
    auto expr = parseExpr();
    const Token& terminator = consume(TokenType::Dot, "Expected '.' after global binding");
    if (!isAtEnd() && peek().line == terminator.line) {
        throw ParserError("Unexpected token after statement terminator '.' on the same line");
    }
    globals_.insert(name);
    return std::make_shared<GlobalBindingStmt>(std::move(name), std::move(expr));
}

std::string Parser::parseQualifiedName() {
    std::string name = consume(TokenType::Ident, "Expected name").text;
    while ((check(TokenType::Colon) || check(TokenType::Dot)) &&
           pos_ + 1 < tokens_.size() &&
           tokens_[pos_ + 1].type == TokenType::Ident &&
           (!check(TokenType::Dot) || tokens_[pos_].line == tokens_[pos_ + 1].line)) {
        advance(); // ':' or '.'
        name += ":" + advance().text;
    }
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
    if (check(TokenType::Ident)) {
        // named argument: name: expr
        if (pos_ + 1 < tokens_.size() &&
            tokens_[pos_ + 1].type == TokenType::Colon) {
            std::string name = advance().text;
            consume(TokenType::Colon, "Expected ':' after argument name");
            return Arg{name, parseExpr()};
        }
    }
    return Arg{"", parseExpr()};
}

std::shared_ptr<Goal> Parser::parseGoal() {
    if (match(TokenType::LParen)) {
        auto grouped = parseGoalList();
        consume(TokenType::RParen, "Expected ')' after grouped goals");
        if (grouped.size() == 1) return grouped.front();
        return std::make_shared<GroupGoal>(std::move(grouped));
    }

    if (check(TokenType::Ident) && peek().text == "where") {
        advance();
        auto left = parseExpr();
        if (!isComparison(peek().type)) {
            throw ParserError("Expected comparison operator after where expression");
        }
        std::string op = comparisonText(advance().type);
        auto right = parseExpr();
        return std::make_shared<WhereGoal>(
            std::make_shared<BinaryGoal>(std::move(left), std::move(op), std::move(right)));
    }

    if (check(TokenType::Ident) && peek().text == "return") {
        advance();
        std::vector<Arg> fields;
        if (match(TokenType::LParen)) {
            if (!check(TokenType::RParen)) fields = parseArgList();
            consume(TokenType::RParen, "Expected ')' after return fields");
        } else {
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
        pos_ + 1 < tokens_.size() &&
        tokens_[pos_ + 1].type == TokenType::Bind) {
        std::string name = advance().text;
        consume(TokenType::Bind, "Expected ':=' after assignment variable");
        size_t lookahead = pos_;
        if (lookahead < tokens_.size() && tokens_[lookahead].type == TokenType::Ident) {
            lookahead++;
            while (lookahead + 1 < tokens_.size() &&
                   (tokens_[lookahead].type == TokenType::Colon ||
                    tokens_[lookahead].type == TokenType::Dot) &&
                   tokens_[lookahead + 1].type == TokenType::Ident &&
                   (tokens_[lookahead].type != TokenType::Dot ||
                    tokens_[lookahead].line == tokens_[lookahead + 1].line)) {
                lookahead += 2;
            }
        }
        if (lookahead < tokens_.size() && tokens_[lookahead].type == TokenType::LParen &&
            (pos_ + 1 < tokens_.size() && tokens_[pos_ + 1].type == TokenType::Dot)) {
            return std::make_shared<AssignGoal>(std::move(name), parseGoal());
        }
        return std::make_shared<AssignGoal>(std::move(name), parseExpr());
    }

    // A goal can be a predicate call or an expression comparison.
    size_t lookahead = pos_;
    if (lookahead < tokens_.size() && tokens_[lookahead].type == TokenType::Ident) {
        lookahead++;
        while (lookahead + 1 < tokens_.size() &&
               (tokens_[lookahead].type == TokenType::Colon ||
                tokens_[lookahead].type == TokenType::Dot) &&
               tokens_[lookahead + 1].type == TokenType::Ident &&
               (tokens_[lookahead].type != TokenType::Dot ||
                tokens_[lookahead].line == tokens_[lookahead + 1].line)) {
            lookahead += 2;
        }
    }
    if (lookahead < tokens_.size() && tokens_[lookahead].type == TokenType::LParen) {
        size_t saved = pos_;
        auto expr = parseExpr();
        if (isComparison(peek().type)) {
            std::string op = comparisonText(advance().type);
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
    std::string op = comparisonText(advance().type);
    auto right = parseExpr();
    return std::make_shared<BinaryGoal>(std::move(left), std::move(op), std::move(right));
}

bool Parser::isMultiAssignmentStart() const {
    size_t lookahead = pos_;
    if (lookahead >= tokens_.size() || tokens_[lookahead].type != TokenType::Ident) return false;
    size_t targetCount = 0;
    while (lookahead < tokens_.size() && tokens_[lookahead].type == TokenType::Ident) {
        targetCount++;
        lookahead++;
        if (lookahead < tokens_.size() && tokens_[lookahead].type == TokenType::Colon) {
            lookahead++;
            if (lookahead >= tokens_.size() || tokens_[lookahead].type != TokenType::Ident) return false;
            lookahead++;
        }
        if (lookahead < tokens_.size() && tokens_[lookahead].type == TokenType::Comma) {
            lookahead++;
            continue;
        }
        break;
    }
    return targetCount > 1 && lookahead < tokens_.size() && tokens_[lookahead].type == TokenType::Bind;
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
    do {
        if (checkElse()) throw ParserError("Dangling 'else' without a preceding method branch");
        goals.push_back(parseGoal());
    } while (match(TokenType::Comma) && !check(TokenType::Pipe) && !checkElse());
    return goals;
}

std::vector<std::shared_ptr<Goal>> Parser::parseGoalList() {
    std::vector<std::vector<std::shared_ptr<Goal>>> branches;
    branches.push_back(parseGoalConjunction());

    while (match(TokenType::Pipe)) {
        if (checkElse()) throw ParserError("Dangling 'else' without a preceding method branch");
        branches.push_back(parseGoalConjunction());
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
    auto expr = parseAdditiveExpr();
    while ((check(TokenType::Colon) || check(TokenType::Dot)) &&
           pos_ + 1 < tokens_.size() &&
           tokens_[pos_ + 1].type == TokenType::Ident &&
           (!check(TokenType::Dot) || tokens_[pos_].line == tokens_[pos_ + 1].line) &&
           !(pos_ + 2 < tokens_.size() && tokens_[pos_ + 2].type == TokenType::LParen)) {
        bool dotAccess = check(TokenType::Dot);
        advance(); // ':' or '.'
        std::string key = consume(TokenType::Ident, dotAccess ? "Expected field name after '.'" : "Expected map field name after ':'").text;
        expr = std::make_shared<AccessExpr>(std::move(expr), std::move(key));
    }
    return expr;
}

std::shared_ptr<Expr> Parser::parseAdditiveExpr() {
    auto expr = parseMultiplicativeExpr();
    while (check(TokenType::Plus) || check(TokenType::Minus)) {
        std::string op = advance().text;
        expr = std::make_shared<BinaryExpr>(std::move(expr), op, parseMultiplicativeExpr());
    }
    return expr;
}

std::shared_ptr<Expr> Parser::parseMultiplicativeExpr() {
    auto expr = parsePrimaryExpr();
    while (check(TokenType::Star) || check(TokenType::Slash)) {
        std::string op = advance().text;
        expr = std::make_shared<BinaryExpr>(std::move(expr), op, parsePrimaryExpr());
    }
    return expr;
}

std::shared_ptr<Expr> Parser::parsePrimaryExpr() {
    if (match(TokenType::String)) return std::make_shared<StringExpr>(previous().text);
    if (match(TokenType::Number)) return std::make_shared<NumberExpr>(std::stod(previous().text));
    if (match(TokenType::LParen)) {
        auto expr = parseExpr();
        consume(TokenType::RParen, "Expected ')' after grouped expression");
        return expr;
    }
    if (check(TokenType::LBrace)) return parseMapExpr();
    if (check(TokenType::LBracket)) return parseArrayExpr();
    if (check(TokenType::Ident)) {
        std::string name = consume(TokenType::Ident, "Expected name").text;
        if (name == "lambda" && check(TokenType::LParen)) {
            return parseLambdaExpr();
        }
        size_t nameEnd = pos_;
        while (nameEnd + 1 < tokens_.size() &&
               (tokens_[nameEnd].type == TokenType::Colon || tokens_[nameEnd].type == TokenType::Dot) &&
               tokens_[nameEnd + 1].type == TokenType::Ident &&
               (tokens_[nameEnd].type != TokenType::Dot ||
                tokens_[nameEnd].line == tokens_[nameEnd + 1].line)) {
            nameEnd += 2;
        }
        if (nameEnd < tokens_.size() && tokens_[nameEnd].type == TokenType::LParen) {
            while (pos_ < nameEnd) {
                advance(); // ':' or '.'
                name += ":" + advance().text;
            }
        }
        if (match(TokenType::LParen)) {
            std::vector<Arg> args;
            if (!check(TokenType::RParen)) {
                do {
                    args.push_back(parseArg());
                } while (match(TokenType::Comma));
            }
            consume(TokenType::RParen, "Expected ')' after term arguments");
            if (name == "system:print" && args.size() == 1 && args.front().name.empty()) {
                args.front().name = "value";
            }
            return std::make_shared<TermExpr>(std::move(name), std::move(args));
        }
        if (name == "_") {
            return std::make_shared<VarExpr>("__anon" + std::to_string(++anonymousCounter_));
        }
        if (name == "nil") {
            return std::make_shared<NilExpr>();
        }
        if (name == "true" || name == "false") {
            return std::make_shared<StringExpr>(name);
        }
        return std::make_shared<VarExpr>(std::move(name));
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
        std::string op = comparisonText(advance().type);
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
            std::string key = consume(TokenType::Ident, "Expected map key").text;
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

bool Parser::containsAccessExpr(const std::shared_ptr<Expr>& expr) const {
    if (std::dynamic_pointer_cast<AccessExpr>(expr)) return true;
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

void Parser::collectExprVars(const std::shared_ptr<Expr>& expr, std::set<std::string>& vars) const {
    if (auto var = std::dynamic_pointer_cast<VarExpr>(expr)) {
        if (var->name != "nil" && var->name.rfind("__anon", 0) != 0) vars.insert(var->name);
        return;
    }
    if (auto term = std::dynamic_pointer_cast<TermExpr>(expr)) {
        static const std::set<std::string> builtinExprs = {
            "count", "sum", "average", "min", "max", "sort", "search", "contains",
            "lower", "upper", "length", "ParseDoc"
        };
        for (const auto& arg : term->args) {
            auto var = std::dynamic_pointer_cast<VarExpr>(arg.value);
            if (builtinExprs.count(term->name) && var && !var->name.empty() &&
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
            collectExprVars(arg.value, used);
        }
    } else if (auto binary = std::dynamic_pointer_cast<BinaryGoal>(goal)) {
        collectExprVars(binary->left, used);
        collectExprVars(binary->right, used);
    } else if (auto where = std::dynamic_pointer_cast<WhereGoal>(goal)) {
        validateGoalVars(where->condition, declared);
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
            if (!target.type.empty() && !isBuiltinTypeName(target.type)) {
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
        if (var && !var->name.empty() && var->name.rfind("__anon", 0) != 0) {
            if (isBuiltinTypeName(var->name) ||
                std::isupper(static_cast<unsigned char>(var->name.front()))) {
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
        if (!arg.name.empty() && var && (isTypeAnnotationName(var->name) || var->name != arg.name)) {
            return true;
        }
    }
    for (const auto& arg : head.args) {
        auto var = std::dynamic_pointer_cast<VarExpr>(arg.value);
        if (!var || !isTypeAnnotationName(var->name)) return false;
    }
    return true;
}

bool Parser::isTypeNameKnown(const std::string& name) const {
    return isBuiltinTypeName(name) || knownTypes_.count(name) > 0;
}

bool Parser::isBuiltinCallName(const std::string& name) const {
    static const std::set<std::string> names = {
        "throw", "type", "instanceof", "count", "sum", "average", "min", "max", "sort",
        "search", "contains", "lower", "upper", "length", "ParseDoc",
        "math:add", "math:sub", "math:mul", "math:div", "math:mod",
        "str:len", "str:contains", "str:concat", "str:lower", "str:upper",
        "str:trim", "str:split", "str:replace", "str:startsWith", "str:endsWith",
        "array:get", "array:len", "array:push",
        "fn:array", "fn:pair", "fn:tuple",
        "pair:first", "pair:second",
        "json:parse", "json:get", "json:has", "json:keys", "json:set", "json:remove", "json:toText",
        "visualize:dataJson", "visualize:dataHtml", "visualize:graphJson",
        "console:readLine", "console:writeLine", "console:write", "system:print",
        "file:readFile", "file:readLines", "file:readLine", "file:writeFile", "file:writeLines", "file:appendFile", "file:exists", "file:deleteFile",
        "csv:parse", "csv:toFacts", "csv:toText", "csv:toFelidaeFacts",
        "csv:addRow", "csv:findRows", "csv:updateRows", "csv:deleteRows",
        "db:all", "db:find", "db:count", "db:first", "db:types", "db:fields",
        "thread:createThread", "thread:start", "thread:pause", "thread:stop", "thread:status", "thread:result",
        "http:get", "http:post", "http:put", "http:delete", "http:serveStatic",
        "process:platform", "process:exec", "process:sleep",
        "math:pi", "math:e", "math:random", "math:pow", "math:atan2",
        "math:sqrt", "math:sin", "math:cos", "math:tan", "math:asin", "math:acos", "math:atan",
        "math:log", "math:log10", "math:exp", "math:abs", "math:floor", "math:ceil", "math:round",
        "probability:mean", "probability:variance", "probability:stddev", "probability:normalize",
        "probability:entropy", "probability:covariance", "probability:correlation",
        "probability:bernoulli", "probability:binomialPmf", "probability:binomialCdf",
        "probability:poissonPmf", "probability:poissonCdf", "probability:normalPdf",
        "probability:normalCdf", "probability:uniformPdf", "probability:uniformCdf",
        "probability:sample", "probability:weightedChoice",
        "ml:sigmoid", "ml:relu", "ml:dot", "ml:meanSquaredError"
    };
    return names.count(name) > 0;
}

void Parser::validateCallFields(const Call& call) const {
    if (isBuiltinCallName(call.name)) return;
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

std::string Parser::comparisonText(TokenType type) const {
    switch (type) {
        case TokenType::EqEq: return "==";
        case TokenType::NotEq: return "!=";
        case TokenType::LT: return "<";
        case TokenType::LTE: return "<=";
        case TokenType::GT: return ">";
        case TokenType::GTE: return ">=";
        default: throw ParserError("Not a comparison operator");
    }
}

} // namespace Felidae
