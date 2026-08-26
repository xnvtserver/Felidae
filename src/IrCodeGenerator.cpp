#include "IrCodeGenerator.h"

#include "IntegerParser.h"
#include "OperatorAnnotation.h"
#include "Symbol.h"
#include "form/SemanticOperation.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <unordered_set>

namespace Felidae {
namespace {

struct DirectCompileContext {
    const std::unordered_set<SymbolId>& procedures;
    const std::unordered_set<SymbolId>& factTypes;
    const std::unordered_map<SymbolId, SymbolId>& factDesignations;
};

std::optional<SemanticOperationId> semanticOperation(SymbolId name) {
    if (name == symbolIdForName("semantic_identity")) return SemanticOperationId::Identity;
    if (name == symbolIdForName("semantic_select_fact")) return SemanticOperationId::SelectFact;
    if (name == symbolIdForName("semantic_derive_fact")) return SemanticOperationId::DeriveFact;
    if (name == symbolIdForName("semantic_evaluate_degree")) return SemanticOperationId::EvaluateDegree;
    return std::nullopt;
}

// The integer parser preserves a dotted identifier as one symbol because the
// same SentencePiece sequence is also used for qualified calls and `fx.`
// keys.  The compiler is the first phase with lexical scope information, so
// it can safely reinterpret only `local.field` spellings whose first segment
// is a visible binding.  Unscoped qualified symbols stay untouched.
std::shared_ptr<Expr> resolveScopedAccess(const std::shared_ptr<Expr>& expression,
                                          const std::unordered_set<SymbolId>& visible) {
    if (!expression) return expression;
    if (const auto variable = std::dynamic_pointer_cast<VarExpr>(expression)) {
        const auto dot = variable->name.find('.');
        if (dot == std::string::npos) return expression;
        const auto baseName = variable->name.substr(0, dot);
        if (!visible.contains(symbolIdForName(baseName))) return expression;
        std::shared_ptr<Expr> result = std::make_shared<VarExpr>(baseName, symbolIdForName(baseName));
        result->sourceSpan = variable->sourceSpan;
        std::size_t begin = dot + 1;
        while (begin < variable->name.size()) {
            const auto next = variable->name.find('.', begin);
            const auto key = variable->name.substr(begin, next == std::string::npos
                ? std::string::npos : next - begin);
            if (key.empty()) return expression;
            auto access = std::make_shared<AccessExpr>(std::move(result), key);
            access->sourceSpan = variable->sourceSpan;
            result = std::move(access);
            if (next == std::string::npos) break;
            begin = next + 1;
        }
        return result;
    }
    if (const auto array = std::dynamic_pointer_cast<ArrayExpr>(expression)) {
        for (auto& item : array->items) item = resolveScopedAccess(item, visible);
    } else if (const auto map = std::dynamic_pointer_cast<MapExpr>(expression)) {
        for (auto& item : map->entries) item.value = resolveScopedAccess(item.value, visible);
    } else if (const auto access = std::dynamic_pointer_cast<AccessExpr>(expression)) {
        access->target = resolveScopedAccess(access->target, visible);
    } else if (const auto term = std::dynamic_pointer_cast<TermExpr>(expression)) {
        for (auto& argument : term->args) argument.value = resolveScopedAccess(argument.value, visible);
    } else if (const auto operation = std::dynamic_pointer_cast<OperatorExpression>(expression)) {
        std::shared_ptr<OperatorExpression> rewritten;
        if (operation->coreOperator == CoreOperator::Unknown) {
            std::vector<OperatorCapture> captures;
            captures.reserve(operation->captureCount());
            for (std::size_t index = 0; index < operation->captureCount(); ++index) {
                captures.emplace_back(std::string(operation->captureName(index)),
                                      resolveScopedAccess(operation->capture(index), visible));
            }
            rewritten = std::make_shared<OperatorExpression>(operation->operatorId, operation->patternId,
                std::move(captures), operation->explicitlyGrouped, operation->resolvedMethodId);
        } else if (operation->captureCount() == 1) {
            rewritten = std::make_shared<OperatorExpression>(operation->coreOperator,
                resolveScopedAccess(operation->capture(0), visible));
        } else {
            rewritten = std::make_shared<OperatorExpression>(operation->coreOperator,
                resolveScopedAccess(operation->capture(0), visible),
                resolveScopedAccess(operation->capture(1), visible));
        }
        rewritten->module = operation->module;
        rewritten->sourceSpan = operation->sourceSpan;
        return rewritten;
    }
    return expression;
}

void resolveScopedAccessesInGoals(std::vector<std::shared_ptr<Goal>>& goals,
                                  std::unordered_set<SymbolId> visible) {
    for (auto& goal : goals) {
        if (const auto assignment = std::dynamic_pointer_cast<AssignGoal>(goal)) {
            assignment->expr = resolveScopedAccess(assignment->expr, visible);
            visible.insert(assignment->nameId);
        } else if (const auto returned = std::dynamic_pointer_cast<ReturnGoal>(goal)) {
            for (auto& field : returned->fields) field.value = resolveScopedAccess(field.value, visible);
        } else if (const auto binary = std::dynamic_pointer_cast<BinaryGoal>(goal)) {
            binary->left = resolveScopedAccess(binary->left, visible);
            binary->right = resolveScopedAccess(binary->right, visible);
        } else if (const auto conditional = std::dynamic_pointer_cast<IfGoal>(goal)) {
            std::vector<std::shared_ptr<Goal>> condition{conditional->condition};
            resolveScopedAccessesInGoals(condition, visible);
            conditional->condition = condition.front();
            resolveScopedAccessesInGoals(conditional->thenBranch, visible);
            resolveScopedAccessesInGoals(conditional->elseBranch, visible);
        }
    }
}

// Annotated mixfix/overload methods may declare their capture bindings in the
// operator annotation rather than repeat them in the callable head.  They are
// still ordinary procedure parameters in IR: this helper gives lowering one
// canonical parameter list for both forms.
std::vector<SymbolId> procedureParameters(const ClauseStmt& method) {
    std::vector<SymbolId> parameters;
    parameters.reserve(method.head.args.size());
    for (const auto& parameter : method.head.args) {
        if (parameter.name.empty() || parameter.nameId == 0) {
            throw IntegerParserError("not yet lowered to IR: invalid procedure parameter");
        }
        if (std::find(parameters.begin(), parameters.end(), parameter.nameId) != parameters.end()) {
            throw IntegerParserError("not yet lowered to IR: duplicate procedure parameter");
        }
        parameters.push_back(parameter.nameId);
    }
    if (!parameters.empty()) return parameters;

    for (const auto& annotation : method.annotations) {
        if (annotation.builtinId != BuiltinId::MixfixAnnotation &&
            annotation.builtinId != BuiltinId::OverloadAnnotation) continue;
        const auto parsed = decodeOperatorAnnotation(annotation);
        if (parsed.captures.empty()) continue;
        for (const auto& capture : parsed.captures) {
            if (capture.nameId == 0 ||
                std::find(parameters.begin(), parameters.end(), capture.nameId) != parameters.end()) {
                throw IntegerParserError("not yet lowered to IR: invalid annotated capture parameter");
            }
            parameters.push_back(capture.nameId);
        }
        return parameters;
    }
    return parameters;
}

// The strict module compiler is intentionally closed: every emitted
// LoadSymbol/Call must resolve through typed module globals or procedures.
// No instruction can reconstruct an AST value or invoke Interpreter services.
bool isDirectDeterministicExpression(const std::shared_ptr<Expr>& expression,
                                     const std::unordered_set<SymbolId>& definedSymbols,
                                     const DirectCompileContext& context) {
    if (!expression) return false;
    if (std::dynamic_pointer_cast<NumberExpr>(expression) ||
        std::dynamic_pointer_cast<BoolExpr>(expression) ||
        std::dynamic_pointer_cast<StringExpr>(expression) ||
        std::dynamic_pointer_cast<NilExpr>(expression)) {
        return true;
    }
    if (const auto array = std::dynamic_pointer_cast<ArrayExpr>(expression)) {
        return std::all_of(array->items.begin(), array->items.end(), [&](const auto& item) {
            return isDirectDeterministicExpression(item, definedSymbols, context);
        });
    }
    if (const auto map = std::dynamic_pointer_cast<MapExpr>(expression)) {
        return std::all_of(map->entries.begin(), map->entries.end(), [&](const MapEntry& entry) {
            return isDirectDeterministicExpression(entry.value, definedSymbols, context);
        });
    }
    if (const auto access = std::dynamic_pointer_cast<AccessExpr>(expression)) {
        return isDirectDeterministicExpression(access->target, definedSymbols, context);
    }
    if (const auto operation = std::dynamic_pointer_cast<OperatorExpression>(expression)) {
        if (operation->coreOperator == CoreOperator::Then) {
            if (operation->captureCount() != 2 ||
                !isDirectDeterministicExpression(operation->capture(0), definedSymbols, context)) {
                return false;
            }
            auto pipelineSymbols = definedSymbols;
            pipelineSymbols.insert(symbolIdForName("system.result"));
            return isDirectDeterministicExpression(operation->capture(1), pipelineSymbols, context);
        }
        if (operation->coreOperator == CoreOperator::Unknown &&
            (operation->resolvedMethodId == 0 ||
             !context.procedures.contains(operation->resolvedMethodId))) return false;
        for (std::size_t index = 0; index < operation->captureCount(); ++index) {
            if (!isDirectDeterministicExpression(operation->capture(index), definedSymbols, context)) return false;
        }
        return true;
    }
    if (const auto variable = std::dynamic_pointer_cast<VarExpr>(expression)) {
        return definedSymbols.contains(variable->nameId);
    }
    if (const auto term = std::dynamic_pointer_cast<TermExpr>(expression)) {
        if (semanticOperation(term->nameId)) {
            return term->args.size() <= 255 &&
                std::all_of(term->args.begin(), term->args.end(), [&](const Arg& argument) {
                    return isDirectDeterministicExpression(argument.value, definedSymbols, context);
                });
        }
        const bool fuzzyIntrinsic = term->nameId == symbolIdForName("similarity") ||
            term->nameId == symbolIdForName("membership") ||
            term->nameId == symbolIdForName("isA") ||
            term->nameId == symbolIdForName("commonAncestors") ||
            term->nameId == symbolIdForName("lowestCommonAncestor") ||
            term->nameId == symbolIdForName("highestCommonAncestor");
        if (fuzzyIntrinsic) {
            const auto expected = term->nameId == symbolIdForName("similarity") ? 2u : 2u;
            return term->args.size() == expected && std::all_of(term->args.begin(), term->args.end(), [&](const Arg& argument) {
                return isDirectDeterministicExpression(argument.value, definedSymbols, context);
            });
        }
        if (term->nameId == symbolIdForName("temporalRank")) {
            return term->args.size() == 2 &&
                std::all_of(term->args.begin(), term->args.end(), [](const Arg& argument) {
                    if (std::dynamic_pointer_cast<VarExpr>(argument.value)) return true;
                    const auto access = std::dynamic_pointer_cast<AccessExpr>(argument.value);
                    return access && static_cast<bool>(std::dynamic_pointer_cast<VarExpr>(access->target));
                });
        }
        if (term->nameId == symbolIdForName("for_each_fact")) {
            if (term->args.size() != 2) return false;
            const auto type = std::dynamic_pointer_cast<VarExpr>(term->args[0].value);
            const auto callback = std::dynamic_pointer_cast<VarExpr>(term->args[1].value);
            const bool declaredType = type && context.factTypes.contains(type->nameId);
            const bool declaredDesignation = type && context.factDesignations.contains(type->nameId);
            return (declaredType || declaredDesignation) && callback &&
                context.procedures.contains(callback->nameId);
        }
        const bool hasNamedFields = !term->args.empty() && std::all_of(term->args.begin(), term->args.end(),
            [](const Arg& argument) { return !argument.name.empty(); });
        // A capitalized, named term is an ordinary fact value even when its
        // schema was not declared in this module. The parser preserves that
        // distinction in TermExpr::isCapitalized; it is not a string heuristic
        // or an alternate source parser.
        const bool isFactValue = context.factTypes.contains(term->nameId) ||
            (term->isCapitalized && term->name.find('.') == std::string::npos && hasNamedFields);
        const bool isProcedure = context.procedures.contains(term->nameId);
        return (isProcedure || isFactValue) &&
            std::all_of(term->args.begin(), term->args.end(), [&](const Arg& argument) {
            return isDirectDeterministicExpression(argument.value, definedSymbols, context);
        });
    }
    return false; // calls, lambdas, fact selections, and AST-only values
}

const SourceSpan& firstUnsupportedExpression(const std::shared_ptr<Expr>& expression,
                                             const std::unordered_set<SymbolId>& definedSymbols,
                                             const DirectCompileContext& context) {
    if (!expression) {
        static const SourceSpan unknown{};
        return unknown;
    }
    if (const auto array = std::dynamic_pointer_cast<ArrayExpr>(expression)) {
        for (const auto& item : array->items) {
            if (!isDirectDeterministicExpression(item, definedSymbols, context))
                return firstUnsupportedExpression(item, definedSymbols, context);
        }
    } else if (const auto map = std::dynamic_pointer_cast<MapExpr>(expression)) {
        for (const auto& item : map->entries) {
            if (!isDirectDeterministicExpression(item.value, definedSymbols, context))
                return firstUnsupportedExpression(item.value, definedSymbols, context);
        }
    } else if (const auto access = std::dynamic_pointer_cast<AccessExpr>(expression)) {
        if (!isDirectDeterministicExpression(access->target, definedSymbols, context))
            return firstUnsupportedExpression(access->target, definedSymbols, context);
    } else if (const auto operation = std::dynamic_pointer_cast<OperatorExpression>(expression)) {
        if (operation->coreOperator == CoreOperator::Then && operation->captureCount() == 2) {
            if (!isDirectDeterministicExpression(operation->capture(0), definedSymbols, context)) {
                return firstUnsupportedExpression(operation->capture(0), definedSymbols, context);
            }
            auto pipelineSymbols = definedSymbols;
            pipelineSymbols.insert(symbolIdForName("system.result"));
            if (!isDirectDeterministicExpression(operation->capture(1), pipelineSymbols, context)) {
                return firstUnsupportedExpression(operation->capture(1), pipelineSymbols, context);
            }
            return expression->sourceSpan;
        }
        if (operation->coreOperator == CoreOperator::Unknown &&
            (operation->resolvedMethodId == 0 ||
             !context.procedures.contains(operation->resolvedMethodId))) return expression->sourceSpan;
        for (std::size_t index = 0; index < operation->captureCount(); ++index) {
            const auto captured = operation->capture(index);
            if (!isDirectDeterministicExpression(captured, definedSymbols, context))
                return firstUnsupportedExpression(captured, definedSymbols, context);
        }
    } else if (const auto term = std::dynamic_pointer_cast<TermExpr>(expression)) {
        if (semanticOperation(term->nameId)) {
            for (const auto& argument : term->args) {
                if (!isDirectDeterministicExpression(argument.value, definedSymbols, context))
                    return firstUnsupportedExpression(argument.value, definedSymbols, context);
            }
            return expression->sourceSpan;
        }
        if ((term->nameId == symbolIdForName("similarity") || term->nameId == symbolIdForName("membership") ||
             term->nameId == symbolIdForName("isA") ||
             term->nameId == symbolIdForName("commonAncestors") ||
             term->nameId == symbolIdForName("lowestCommonAncestor") ||
             term->nameId == symbolIdForName("highestCommonAncestor")) &&
            term->args.size() == 2) {
            for (const auto& argument : term->args) {
                if (!isDirectDeterministicExpression(argument.value, definedSymbols, context))
                    return firstUnsupportedExpression(argument.value, definedSymbols, context);
            }
            return expression->sourceSpan;
        }
        if (term->nameId == symbolIdForName("temporalRank") && term->args.size() == 2 &&
            isDirectDeterministicExpression(expression, definedSymbols, context)) {
            return expression->sourceSpan;
        }
        if (term->nameId == symbolIdForName("for_each_fact") && term->args.size() == 2 &&
            isDirectDeterministicExpression(expression, definedSymbols, context)) return expression->sourceSpan;
        const bool isProcedure = context.procedures.contains(term->nameId);
        const bool isFactType = context.factTypes.contains(term->nameId);
        if (!isProcedure && !isFactType) {
            return expression->sourceSpan;
        }
        for (const auto& argument : term->args) {
            if (!isDirectDeterministicExpression(argument.value, definedSymbols, context))
                return firstUnsupportedExpression(argument.value, definedSymbols, context);
        }
    }
    return expression->sourceSpan;
}

bool isDirectReturn(const std::shared_ptr<Goal>& goal,
                    const std::unordered_set<SymbolId>& definedSymbols,
                    const DirectCompileContext& context) {
    const auto returned = std::dynamic_pointer_cast<ReturnGoal>(goal);
    return returned && std::all_of(returned->fields.begin(), returned->fields.end(), [&](const Arg& field) {
        return isDirectDeterministicExpression(field.value, definedSymbols, context);
    });
}

bool isDirectCondition(const std::shared_ptr<Goal>& goal,
                       const std::unordered_set<SymbolId>& definedSymbols,
                       const DirectCompileContext& context) {
    const auto comparison = std::dynamic_pointer_cast<BinaryGoal>(goal);
    if (!comparison || !isDirectDeterministicExpression(comparison->left, definedSymbols, context) ||
        !isDirectDeterministicExpression(comparison->right, definedSymbols, context)) return false;
    switch (comparison->op) {
    case TokenId::EQUAL: case TokenId::NOT_EQUAL: case TokenId::LESS: case TokenId::LESS_EQUAL:
    case TokenId::GREATER: case TokenId::GREATER_EQUAL:
        return true;
    default:
        return false;
    }
}

bool isDirectGoalSequence(const std::vector<std::shared_ptr<Goal>>& goals,
                          std::unordered_set<SymbolId> visible,
                          const DirectCompileContext& context) {
    if (goals.empty()) return false;
    for (std::size_t index = 0; index + 1 < goals.size(); ++index) {
        const auto assignment = std::dynamic_pointer_cast<AssignGoal>(goals[index]);
        if (!assignment || !assignment->expr || visible.contains(assignment->nameId) ||
            !isDirectDeterministicExpression(assignment->expr, visible, context)) return false;
        visible.insert(assignment->nameId);
    }
    return isDirectReturn(goals.back(), visible, context);
}

const SourceSpan& firstUnsupportedGoalSequence(const std::vector<std::shared_ptr<Goal>>& goals,
                                                std::unordered_set<SymbolId> visible,
                                                const DirectCompileContext& context) {
    static const SourceSpan unknown{};
    if (goals.empty()) return unknown;
    for (std::size_t index = 0; index + 1 < goals.size(); ++index) {
        const auto assignment = std::dynamic_pointer_cast<AssignGoal>(goals[index]);
        if (!assignment || !assignment->expr || visible.contains(assignment->nameId)) {
            return goals[index] ? goals[index]->sourceSpan : unknown;
        }
        if (!isDirectDeterministicExpression(assignment->expr, visible, context)) {
            return firstUnsupportedExpression(assignment->expr, visible, context);
        }
        visible.insert(assignment->nameId);
    }
    const auto returned = std::dynamic_pointer_cast<ReturnGoal>(goals.back());
    if (!returned) return goals.back() ? goals.back()->sourceSpan : unknown;
    for (const auto& field : returned->fields) {
        if (!isDirectDeterministicExpression(field.value, visible, context)) {
            return firstUnsupportedExpression(field.value, visible, context);
        }
    }
    return returned->sourceSpan;
}

const SourceSpan& firstUnsupportedProcedure(const ClauseStmt& method,
                                            const std::unordered_set<SymbolId>& globals,
                                            const DirectCompileContext& context) {
    auto visible = globals;
    for (const auto parameter : procedureParameters(method)) visible.insert(parameter);
    if (method.body.size() == 1) {
        if (const auto conditional = std::dynamic_pointer_cast<IfGoal>(method.body.front())) {
            if (!isDirectCondition(conditional->condition, visible, context)) {
                const auto comparison = std::dynamic_pointer_cast<BinaryGoal>(conditional->condition);
                if (comparison) {
                    if (!isDirectDeterministicExpression(comparison->left, visible, context))
                        return firstUnsupportedExpression(comparison->left, visible, context);
                    if (!isDirectDeterministicExpression(comparison->right, visible, context))
                        return firstUnsupportedExpression(comparison->right, visible, context);
                }
                return conditional->condition ? conditional->condition->sourceSpan : method.sourceSpan;
            }
            if (!isDirectGoalSequence(conditional->thenBranch, visible, context))
                return firstUnsupportedGoalSequence(conditional->thenBranch, visible, context);
            return firstUnsupportedGoalSequence(conditional->elseBranch, visible, context);
        }
    }
    return firstUnsupportedGoalSequence(method.body, std::move(visible), context);
}

bool isDirectEntry(const ClauseStmt& method, const std::unordered_set<SymbolId>& definedSymbols,
                   const DirectCompileContext& context) {
    if (method.head.nameId != symbolIdForName("main") || !method.head.args.empty() ||
        method.body.empty() || !method.fallbackBranches.empty()) return false;
    if (method.body.size() == 1 && isDirectReturn(method.body.front(), definedSymbols, context)) return true;
    if (method.body.size() == 1) {
        const auto conditional = std::dynamic_pointer_cast<IfGoal>(method.body.front());
        return conditional && isDirectCondition(conditional->condition, definedSymbols, context) &&
            isDirectGoalSequence(conditional->thenBranch, definedSymbols, context) &&
            isDirectGoalSequence(conditional->elseBranch, definedSymbols, context);
    }
    return isDirectGoalSequence(method.body, definedSymbols, context);
}

bool isDirectProcedure(const ClauseStmt& method, const std::unordered_set<SymbolId>& globals,
                       const DirectCompileContext& context) {
    if (method.body.empty() || !method.fallbackBranches.empty()) return false;
    auto visible = globals;
    for (const auto parameter : procedureParameters(method)) {
        if (!visible.insert(parameter).second) return false;
    }
    if (method.body.size() == 1 && isDirectReturn(method.body.front(), visible, context)) return true;
    if (method.body.size() == 1) {
        const auto conditional = std::dynamic_pointer_cast<IfGoal>(method.body.front());
        return conditional && isDirectCondition(conditional->condition, visible, context) &&
            isDirectGoalSequence(conditional->thenBranch, visible, context) &&
            isDirectGoalSequence(conditional->elseBranch, visible, context);
    }
    return isDirectGoalSequence(method.body, visible, context);
}

// Each frontend lowering fragment owns its tables and registers.  This linker
// preserves canonical integer IR while rebasing every table/register/jump
// operand; it never reuses AST nodes at runtime.
void appendFragment(FelidaeIr& target, FelidaeIr fragment, bool dropTerminalReturn,
                    bool dropTerminalEnd = false) {
    IrVerifier::verify(fragment);
    if (dropTerminalReturn) {
        if (fragment.words.size() < 4 ||
            fragment.words[fragment.words.size() - 4] != static_cast<IrWord>(IrOpcode::Return) ||
            fragment.words.back() != static_cast<IrWord>(IrOpcode::End)) {
            throw IntegerParserError("IR fragment does not have a terminal return");
        }
        fragment.words.resize(fragment.words.size() - 4);
    } else if (dropTerminalEnd) {
        if (fragment.words.empty() || fragment.words.back() != static_cast<IrWord>(IrOpcode::End)) {
            throw IntegerParserError("IR fragment is missing END");
        }
        fragment.words.pop_back();
    }
    const IrWord registerBase = target.registerCount;
    const IrWord constantBase = target.constants.size();
    const IrWord textBase = target.texts.size();
    const IrWord symbolBase = target.symbols.size();
    const IrWord wordBase = target.words.size();
    for (std::size_t index = 0; index < fragment.constants.size(); ++index) {
        auto constant = fragment.constants[index];
        if (!fragment.constantKinds.empty() && fragment.constantKinds[index] == IrConstantKind::Text) {
            constant += textBase;
        }
        target.constants.push_back(constant);
    }
    target.constantKinds.insert(target.constantKinds.end(), fragment.constantKinds.begin(), fragment.constantKinds.end());
    target.texts.insert(target.texts.end(), fragment.texts.begin(), fragment.texts.end());
    target.symbols.insert(target.symbols.end(), fragment.symbols.begin(), fragment.symbols.end());
    target.programs.insert(target.programs.end(), fragment.programs.begin(), fragment.programs.end());
    auto reg = [&](IrWord& value) { value += registerBase; };
    auto symbol = [&](IrWord& value) { value += symbolBase; };
    for (std::size_t pc = 0; pc < fragment.words.size();) {
        const auto opcode = static_cast<IrOpcode>(fragment.words[pc]);
        std::size_t width = 0;
        switch (opcode) {
        case IrOpcode::End: width = 1; break;
        case IrOpcode::Jump: fragment.words[pc + 1] += wordBase; width = 2; break;
        case IrOpcode::LoadConst: reg(fragment.words[pc + 1]); fragment.words[pc + 2] += constantBase; width = 3; break;
        case IrOpcode::LoadSymbol: reg(fragment.words[pc + 1]); symbol(fragment.words[pc + 2]); width = 3; break;
        case IrOpcode::StoreSymbol: symbol(fragment.words[pc + 1]); reg(fragment.words[pc + 2]); width = 3; break;
        case IrOpcode::Move: reg(fragment.words[pc + 1]); reg(fragment.words[pc + 2]); width = 3; break;
        case IrOpcode::JumpIfFalse: reg(fragment.words[pc + 1]); fragment.words[pc + 2] += wordBase; width = 3; break;
        case IrOpcode::CallNative: case IrOpcode::MakeFact: reg(fragment.words[pc + 1]); symbol(fragment.words[pc + 2]); width = 3; break;
        case IrOpcode::Return: reg(fragment.words[pc + 1]); width = 3; break;
        case IrOpcode::Add: case IrOpcode::Sub: case IrOpcode::Mul: case IrOpcode::Div: case IrOpcode::Mod:
        case IrOpcode::Similarity:
        case IrOpcode::HierarchyIsA:
        case IrOpcode::HierarchyCommonAncestors:
        case IrOpcode::HierarchyLeastCommonAncestors:
        case IrOpcode::HierarchyMostGeneralAncestors:
            reg(fragment.words[pc + 1]); reg(fragment.words[pc + 2]); reg(fragment.words[pc + 3]); width = 4; break;
        case IrOpcode::TemporalRank:
            reg(fragment.words[pc + 1]); symbol(fragment.words[pc + 2]); symbol(fragment.words[pc + 3]); width = 4; break;
        case IrOpcode::Membership:
            for (std::size_t index = 1; index < 6; ++index) {
                reg(fragment.words[pc + index]);
            }
            width = 6;
            break;
        case IrOpcode::Compare:
            reg(fragment.words[pc + 1]); reg(fragment.words[pc + 2]); reg(fragment.words[pc + 3]); width = 5; break;
        case IrOpcode::GetField:
            reg(fragment.words[pc + 1]); reg(fragment.words[pc + 2]); symbol(fragment.words[pc + 3]); width = 4; break;
        case IrOpcode::SetField:
            reg(fragment.words[pc + 1]); symbol(fragment.words[pc + 2]); reg(fragment.words[pc + 3]); width = 4; break;
        case IrOpcode::ForEachFact:
            reg(fragment.words[pc + 1]); symbol(fragment.words[pc + 2]); symbol(fragment.words[pc + 3]); width = 4; break;
        case IrOpcode::Call: case IrOpcode::SemanticEval: case IrOpcode::MakeArray: {
            reg(fragment.words[pc + 1]);
            if (opcode == IrOpcode::Call) symbol(fragment.words[pc + 2]);
            const auto count = fragment.words[pc + 3];
            for (std::size_t index = 0; index < count; ++index) reg(fragment.words[pc + 4 + index]);
            width = 4 + count;
            break;
        }
        case IrOpcode::CallNamed: case IrOpcode::MakeMap: {
            reg(fragment.words[pc + 1]);
            if (opcode == IrOpcode::CallNamed) symbol(fragment.words[pc + 2]);
            const auto count = fragment.words[pc + 3];
            for (std::size_t index = 0; index < count; ++index) {
                auto& key = fragment.words[pc + 4 + index * 2];
                if (opcode == IrOpcode::CallNamed) { if (key != 0) key += symbolBase; }
                else symbol(key);
                reg(fragment.words[pc + 5 + index * 2]);
            }
            width = 4 + count * 2;
            break;
        }
        case IrOpcode::Count: throw IntegerParserError("IR fragment has an invalid opcode");
        }
        pc += width;
    }
    for (auto entry : fragment.sourceMap) {
        entry.instructionWord += wordBase;
        target.sourceMap.push_back(std::move(entry));
    }
    target.words.insert(target.words.end(), fragment.words.begin(), fragment.words.end());
    target.registerCount += fragment.registerCount;
}

CoreOperator directConditionOperator(TokenId::Id token) {
    switch (token) {
    case TokenId::EQUAL: return CoreOperator::StrictEqual;
    case TokenId::NOT_EQUAL: return CoreOperator::StrictNotEqual;
    case TokenId::LESS: return CoreOperator::Less;
    case TokenId::LESS_EQUAL: return CoreOperator::LessEqual;
    case TokenId::GREATER: return CoreOperator::Greater;
    case TokenId::GREATER_EQUAL: return CoreOperator::GreaterEqual;
    default: throw IntegerParserError("condition has no direct IR comparison");
    }
}

FelidaeIr compileDirectProcedure(const ClauseStmt& method,
                                 const std::unordered_set<SymbolId>& factTypes,
                                 const std::unordered_map<SymbolId, SymbolId>& factDesignations);

FelidaeIr compileDirectConditionalEntry(const ClauseStmt& method,
                                        const std::unordered_set<SymbolId>& factTypes,
                                        const std::unordered_map<SymbolId, SymbolId>& factDesignations) {
    const auto conditional = std::dynamic_pointer_cast<IfGoal>(method.body.front());
    const auto comparison = std::dynamic_pointer_cast<BinaryGoal>(conditional->condition);
    auto expression = std::make_shared<OperatorExpression>(directConditionOperator(comparison->op),
                                                            comparison->left, comparison->right);
    expression->sourceSpan = comparison->sourceSpan;
    auto conditionIr = IntegerParser::compileAstExpressionIr(expression, factTypes, factDesignations);
    const auto conditionRegister = conditionIr.words.at(conditionIr.words.size() - 3);
    FelidaeIr result;
    appendFragment(result, std::move(conditionIr), true);
    const auto elseJump = result.words.size();
    result.words.insert(result.words.end(), {static_cast<IrWord>(IrOpcode::JumpIfFalse),
                                             conditionRegister, 0});
    ClauseStmt thenMethod(method.head, conditional->thenBranch);
    appendFragment(result, compileDirectProcedure(thenMethod, factTypes, factDesignations), false, true);
    const auto endJump = result.words.size();
    result.words.insert(result.words.end(), {static_cast<IrWord>(IrOpcode::Jump), 0});
    const auto elseTarget = result.words.size();
    ClauseStmt elseMethod(method.head, conditional->elseBranch);
    appendFragment(result, compileDirectProcedure(elseMethod, factTypes, factDesignations), false);
    result.words[elseJump + 2] = elseTarget;
    result.words[endJump + 1] = result.words.size() - 1; // the final END boundary
    IrVerifier::verify(result);
    return result;
}

FelidaeIr compileDirectSequentialEntry(const ClauseStmt& method,
                                       const std::unordered_set<SymbolId>& factTypes,
                                       const std::unordered_map<SymbolId, SymbolId>& factDesignations) {
    FelidaeIr result;
    for (std::size_t index = 0; index + 1 < method.body.size(); ++index) {
        const auto assignment = std::dynamic_pointer_cast<AssignGoal>(method.body[index]);
        GlobalBindingStmt binding(assignment->name, assignment->expr);
        binding.sourceSpan = assignment->sourceSpan;
        appendFragment(result, IntegerParser::compileAstGlobalBindingIr(
            binding, factTypes, factDesignations), true);
    }
    ClauseStmt returned(method.head, {method.body.back()});
    appendFragment(result, IntegerParser::compileAstEntryMethodIr(
        returned, factTypes, factDesignations), false);
    IrVerifier::verify(result);
    return result;
}

FelidaeIr compileDirectProcedure(const ClauseStmt& method,
                                 const std::unordered_set<SymbolId>& factTypes,
                                 const std::unordered_map<SymbolId, SymbolId>& factDesignations) {
    if (method.body.size() == 1) {
        if (std::dynamic_pointer_cast<IfGoal>(method.body.front())) {
            return compileDirectConditionalEntry(method, factTypes, factDesignations);
        }
        return IntegerParser::compileAstEntryMethodIr(method, factTypes, factDesignations);
    }
    return compileDirectSequentialEntry(method, factTypes, factDesignations);
}

} // namespace

IrModule IrCodeGenerator::compile(Program program) const {
    IrModule module;
    // The strict compiler accepts only constructs that lower to verified IR.
    // Unsupported constructs are rejected with a source span; there is no
    // interpreter or compatibility execution path.
    std::unordered_set<SymbolId> directSymbols;
    std::unordered_set<SymbolId> procedureSymbols;
    std::unordered_set<SymbolId> factTypes;
    for (const auto& clause : program.clauses) {
        if (!clause) continue;
        const bool conflict = clause->isFact()
            ? procedureSymbols.contains(clause->head.nameId)
            : factTypes.contains(clause->head.nameId) ||
              !procedureSymbols.insert(clause->head.nameId).second;
        if (conflict) {
            throw IntegerParserError("not yet lowered to IR: duplicate procedure declaration at " +
                std::to_string(clause->sourceSpan.startLine) + ":" +
                std::to_string(clause->sourceSpan.startColumn));
        }
        // A fact predicate is a table and therefore may have any number of
        // source rows. Only callable procedures are single declarations.
        if (clause->isFact()) factTypes.insert(clause->head.nameId);
    }
    for (const auto& clause : program.clauses) {
        if (!clause) continue;
        for (const auto& annotation : clause->annotations) {
            const bool operatorAnnotation =
                annotation.builtinId == BuiltinId::OverloadAnnotation ||
                annotation.builtinId == BuiltinId::MixfixAnnotation ||
                annotation.builtinId == BuiltinId::MatcherAnnotation;
            if (!operatorAnnotation && !procedureSymbols.contains(annotation.nameId)) {
                throw IntegerParserError("unknown annotation '" + annotation.name + "' at " +
                    std::to_string(annotation.sourceSpan.startLine) + ":" +
                    std::to_string(annotation.sourceSpan.startColumn));
            }
        }
    }
    // Module bindings form one immutable namespace.  Reject collisions before
    // lowering so a valid-looking FELBIN cannot defer a scope error until VM
    // execution.  Procedure locals are checked separately against parameters
    // and globals by isDirectProcedure/isDirectGoalSequence.
    std::unordered_set<SymbolId> globalSymbols;
    for (const auto& binding : program.globals) {
        if (!binding || binding->name.empty()) {
            throw IntegerParserError("not yet lowered to IR: invalid global binding at " +
                std::to_string(binding ? binding->sourceSpan.startLine : 1) + ":" +
                std::to_string(binding ? binding->sourceSpan.startColumn : 1));
        }
        const auto symbol = symbolIdForName(binding->name);
        if (!globalSymbols.insert(symbol).second || procedureSymbols.contains(symbol) || factTypes.contains(symbol)) {
            throw IntegerParserError("not yet lowered to IR: duplicate or conflicting global binding at " +
                std::to_string(binding->sourceSpan.startLine) + ":" +
                std::to_string(binding->sourceSpan.startColumn));
        }
    }
    // Resolve fields after all module names and method parameters are known.
    // This is compiler-local AST normalization; Form receives only the
    // resulting GetField IR and retains no syntax dependency.
    std::unordered_set<SymbolId> lexicalGlobals;
    for (auto& binding : program.globals) {
        if (!binding) continue;
        binding->expr = resolveScopedAccess(binding->expr, lexicalGlobals);
        lexicalGlobals.insert(symbolIdForName(binding->name));
    }
    for (auto& clause : program.clauses) {
        if (!clause || clause->isFact()) continue;
        auto visible = globalSymbols;
        for (const auto parameter : procedureParameters(*clause)) visible.insert(parameter);
        resolveScopedAccessesInGoals(clause->body, std::move(visible));
    }
    std::unordered_map<SymbolId, SymbolId> factDesignations;
    std::unordered_map<IrSymbolRef, std::size_t> factTypeIndexes;
    for (const auto& clause : program.clauses) {
        if (!clause || !clause->isFact()) continue;
        for (const auto designation : clause->designationIds) {
            const auto [existingDesignation, insertedDesignation] =
                factDesignations.emplace(designation, clause->head.nameId);
            if (!insertedDesignation && existingDesignation->second != clause->head.nameId) {
                throw IntegerParserError("fact designation cannot name unrelated fact types at " +
                    std::to_string(clause->sourceSpan.startLine) + ":" +
                    std::to_string(clause->sourceSpan.startColumn));
            }
        }
        std::vector<IrSymbolRef> parents;
        for (const auto& parent : clause->parentNames) {
            const auto parentSymbol = symbolIdForName(parent);
            if (!factTypes.contains(parentSymbol)) {
                throw IntegerParserError("not yet lowered to IR: unknown fact parent at " +
                    std::to_string(clause->sourceSpan.startLine) + ":" +
                    std::to_string(clause->sourceSpan.startColumn));
            }
            parents.push_back(parentSymbol);
        }
        const auto existing = factTypeIndexes.find(clause->head.nameId);
        if (existing == factTypeIndexes.end()) {
            factTypeIndexes.emplace(clause->head.nameId, module.factTypes.size());
            module.factTypes.push_back({clause->head.nameId, std::move(parents),
                {clause->sourceSpan.startLine, clause->sourceSpan.startColumn,
                 clause->sourceSpan.endLine, clause->sourceSpan.endColumn}});
        } else if (module.factTypes[existing->second].parents != parents) {
            throw IntegerParserError("not yet lowered to IR: inconsistent fact hierarchy at " +
                std::to_string(clause->sourceSpan.startLine) + ":" +
                std::to_string(clause->sourceSpan.startColumn));
        }
    }
    const DirectCompileContext context{procedureSymbols, factTypes, factDesignations};
    bool directGlobals = program.imports.empty() && !program.clauses.empty();
    for (const auto& binding : program.globals) {
        if (!binding || !isDirectDeterministicExpression(binding->expr, directSymbols, context)) {
            directGlobals = false;
            break;
        }
        directSymbols.insert(symbolIdForName(binding->name));
    }
    const auto main = std::find_if(program.clauses.begin(), program.clauses.end(), [](const auto& clause) {
        return clause && !clause->isFact() && clause->head.nameId == symbolIdForName("main");
    });
    const bool eligibleMethods = std::all_of(program.clauses.begin(), program.clauses.end(),
        [&](const auto& clause) {
            return clause && (clause->isFact() || (clause->head.nameId == symbolIdForName("main")
                ? isDirectEntry(*clause, directSymbols, context) : isDirectProcedure(*clause, directSymbols, context)));
        });
    if (directGlobals && main != program.clauses.end() && eligibleMethods &&
        isDirectEntry(**main, directSymbols, context)) {
        for (const auto& clause : program.clauses) {
            if (!clause || !clause->isFact() || clause->emptyDeclaration) continue;
            auto fact = std::make_shared<TermExpr>(clause->head.name, clause->head.nameId,
                clause->head.args, clause->head.builtinId, true);
            fact->sourceSpan = clause->sourceSpan;
            if (!isDirectDeterministicExpression(fact, directSymbols, context)) {
                throw IntegerParserError("not yet lowered to IR: non-deterministic fact row at " +
                    std::to_string(clause->sourceSpan.startLine) + ":" +
                    std::to_string(clause->sourceSpan.startColumn));
            }
            appendFragment(module.ir,
                IntegerParser::compileAstExpressionIr(
                    std::move(fact), factTypes, factDesignations), true);
        }
        for (const auto& binding : program.globals) {
            appendFragment(module.ir, IntegerParser::compileAstGlobalBindingIr(
                *binding, factTypes, factDesignations), true);
        }
        for (const auto& clause : program.clauses) {
                if (clause->isFact()) continue;
                const auto parameters = procedureParameters(*clause);
                std::vector<IrSymbolRef> irParameters;
                irParameters.reserve(parameters.size());
                for (const auto parameter : parameters) {
                    if (parameter > std::numeric_limits<IrSymbolRef>::max()) {
                        throw IntegerParserError("not yet lowered to IR: procedure parameter symbol exceeds IR range");
                    }
                    irParameters.push_back(static_cast<IrSymbolRef>(parameter));
                }
                const auto [_, inserted] = module.procedures.emplace(clause->head.nameId, IrProcedure{
                    compileDirectProcedure(*clause, factTypes, factDesignations), irParameters, irParameters,
                    {clause->sourceSpan.startLine, clause->sourceSpan.startColumn,
                     clause->sourceSpan.endLine, clause->sourceSpan.endColumn}});
                if (!inserted) throw IntegerParserError("duplicate direct IR procedure");
        }
        const auto entryRegister = static_cast<IrWord>(module.ir.registerCount++);
        module.entryProcedure = (*main)->head.nameId;
        module.ir.symbols.push_back((*main)->head.nameId);
        const auto entrySymbol = static_cast<IrWord>(module.ir.symbols.size() - 1);
        module.ir.words.insert(module.ir.words.end(), {
            static_cast<IrWord>(IrOpcode::Call), entryRegister, entrySymbol, 0,
            static_cast<IrWord>(IrOpcode::Return), entryRegister, 0,
            static_cast<IrWord>(IrOpcode::End),
        });
        const auto& span = (*main)->sourceSpan;
        module.ir.sourceMap.push_back(IrSourceMapEntry{module.ir.words.size() - 8,
            {span.startLine, span.startColumn, span.endLine, span.endColumn}});
        return module;
    }
    SourceSpan span;
    bool foundUnsupportedSpan = false;
    for (const auto& binding : program.globals) {
        if (!binding || !isDirectDeterministicExpression(binding->expr, directSymbols, context)) {
            span = binding && binding->expr
                ? firstUnsupportedExpression(binding->expr, directSymbols, context) : SourceSpan{};
            foundUnsupportedSpan = true;
            break;
        }
    }
    if (!foundUnsupportedSpan) {
        for (const auto& clause : program.clauses) {
            const auto eligible = clause && (clause->isFact() || (clause->head.nameId == symbolIdForName("main")
                ? isDirectEntry(*clause, directSymbols, context) : isDirectProcedure(*clause, directSymbols, context)));
            if (!eligible) {
                span = clause ? firstUnsupportedProcedure(*clause, directSymbols, context) : SourceSpan{};
                foundUnsupportedSpan = true;
                break;
            }
        }
    }
    if (!foundUnsupportedSpan && !program.statements.empty() && program.statements.front()) {
        span = program.statements.front()->sourceSpan;
    }
    throw IntegerParserError("not yet lowered to IR: unsupported module construct at " +
        std::to_string(span.startLine) + ":" + std::to_string(span.startColumn));
}

} // namespace Felidae
    
