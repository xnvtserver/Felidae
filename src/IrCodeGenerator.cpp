#include "IrCodeGenerator.h"

#include "IntegerParser.h"
#include "Symbol.h"

#include <algorithm>
#include <unordered_set>

namespace Felidae {
namespace {

struct DirectCompileContext {
    const std::unordered_set<SymbolId>& procedures;
    const std::unordered_set<SymbolId>& factTypes;
};

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
        const bool fuzzyIntrinsic = term->nameId == symbolIdForName("similarity") || term->nameId == symbolIdForName("membership");
        if (fuzzyIntrinsic) {
            const auto expected = term->nameId == symbolIdForName("similarity") ? 2u : 2u;
            return term->args.size() == expected && std::all_of(term->args.begin(), term->args.end(), [&](const Arg& argument) {
                return isDirectDeterministicExpression(argument.value, definedSymbols, context);
            });
        }
        if (term->nameId == symbolIdForName("for_each_fact")) {
            if (term->args.size() != 2) return false;
            const auto type = std::dynamic_pointer_cast<VarExpr>(term->args[0].value);
            const auto callback = std::dynamic_pointer_cast<VarExpr>(term->args[1].value);
            return type && callback && context.factTypes.contains(type->nameId) &&
                context.procedures.contains(callback->nameId);
        }
        const bool isProcedure = context.procedures.contains(term->nameId);
        const bool isFactType = context.factTypes.contains(term->nameId);
        return (isProcedure || isFactType) &&
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
        if (operation->coreOperator == CoreOperator::Unknown &&
            (operation->resolvedMethodId == 0 ||
             !context.procedures.contains(operation->resolvedMethodId))) return expression->sourceSpan;
        for (std::size_t index = 0; index < operation->captureCount(); ++index) {
            const auto captured = operation->capture(index);
            if (!isDirectDeterministicExpression(captured, definedSymbols, context))
                return firstUnsupportedExpression(captured, definedSymbols, context);
        }
    } else if (const auto term = std::dynamic_pointer_cast<TermExpr>(expression)) {
        if ((term->nameId == symbolIdForName("similarity") || term->nameId == symbolIdForName("membership")) &&
            term->args.size() == 2) {
            for (const auto& argument : term->args) {
                if (!isDirectDeterministicExpression(argument.value, definedSymbols, context))
                    return firstUnsupportedExpression(argument.value, definedSymbols, context);
            }
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
    for (const auto& parameter : method.head.args) {
        if (parameter.name.empty() || parameter.nameId == 0 || !visible.insert(parameter.nameId).second) return false;
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
            reg(fragment.words[pc + 1]); reg(fragment.words[pc + 2]); reg(fragment.words[pc + 3]); width = 4; break;
        case IrOpcode::Membership:
            for (std::size_t index = 1; index < 6; ++index) reg(fragment.words[pc + index]); width = 6; break;
        case IrOpcode::Compare:
            reg(fragment.words[pc + 1]); reg(fragment.words[pc + 2]); reg(fragment.words[pc + 3]); width = 5; break;
        case IrOpcode::GetField:
            reg(fragment.words[pc + 1]); reg(fragment.words[pc + 2]); symbol(fragment.words[pc + 3]); width = 4; break;
        case IrOpcode::SetField:
            reg(fragment.words[pc + 1]); symbol(fragment.words[pc + 2]); reg(fragment.words[pc + 3]); width = 4; break;
        case IrOpcode::ForEachFact:
            reg(fragment.words[pc + 1]); symbol(fragment.words[pc + 2]); symbol(fragment.words[pc + 3]); width = 4; break;
        case IrOpcode::Call: case IrOpcode::SemanticEval: case IrOpcode::SsmProcess: case IrOpcode::MakeArray: {
            reg(fragment.words[pc + 1]);
            if (opcode != IrOpcode::MakeArray) symbol(fragment.words[pc + 2]);
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
                                 const std::unordered_set<SymbolId>& factTypes);

FelidaeIr compileDirectConditionalEntry(const ClauseStmt& method,
                                        const std::unordered_set<SymbolId>& factTypes) {
    const auto conditional = std::dynamic_pointer_cast<IfGoal>(method.body.front());
    const auto comparison = std::dynamic_pointer_cast<BinaryGoal>(conditional->condition);
    auto expression = std::make_shared<OperatorExpression>(directConditionOperator(comparison->op),
                                                            comparison->left, comparison->right);
    expression->sourceSpan = comparison->sourceSpan;
    auto conditionIr = IntegerParser::compileAstExpressionIr(expression, factTypes);
    const auto conditionRegister = conditionIr.words.at(conditionIr.words.size() - 3);
    FelidaeIr result;
    appendFragment(result, std::move(conditionIr), true);
    const auto elseJump = result.words.size();
    result.words.insert(result.words.end(), {static_cast<IrWord>(IrOpcode::JumpIfFalse),
                                             conditionRegister, 0});
    ClauseStmt thenMethod(method.head, conditional->thenBranch);
    appendFragment(result, compileDirectProcedure(thenMethod, factTypes), false, true);
    const auto endJump = result.words.size();
    result.words.insert(result.words.end(), {static_cast<IrWord>(IrOpcode::Jump), 0});
    const auto elseTarget = result.words.size();
    ClauseStmt elseMethod(method.head, conditional->elseBranch);
    appendFragment(result, compileDirectProcedure(elseMethod, factTypes), false);
    result.words[elseJump + 2] = elseTarget;
    result.words[endJump + 1] = result.words.size() - 1; // the final END boundary
    IrVerifier::verify(result);
    return result;
}

FelidaeIr compileDirectSequentialEntry(const ClauseStmt& method,
                                       const std::unordered_set<SymbolId>& factTypes) {
    FelidaeIr result;
    for (std::size_t index = 0; index + 1 < method.body.size(); ++index) {
        const auto assignment = std::dynamic_pointer_cast<AssignGoal>(method.body[index]);
        GlobalBindingStmt binding(assignment->name, assignment->expr);
        binding.sourceSpan = assignment->sourceSpan;
        appendFragment(result, IntegerParser::compileAstGlobalBindingIr(binding, factTypes), true);
    }
    ClauseStmt returned(method.head, {method.body.back()});
    appendFragment(result, IntegerParser::compileAstEntryMethodIr(returned, factTypes), false);
    IrVerifier::verify(result);
    return result;
}

FelidaeIr compileDirectProcedure(const ClauseStmt& method,
                                 const std::unordered_set<SymbolId>& factTypes) {
    if (method.body.size() == 1) {
        if (std::dynamic_pointer_cast<IfGoal>(method.body.front())) {
            return compileDirectConditionalEntry(method, factTypes);
        }
        return IntegerParser::compileAstEntryMethodIr(method, factTypes);
    }
    return compileDirectSequentialEntry(method, factTypes);
}

} // namespace

IrModule IrCodeGenerator::compile(Program program) const {
    IrModule module;
    // First production compiler slice: an isolated simple main can execute
    // as real IR without crossing into Interpreter. Unsupported constructs
    // remain on the compatibility path until their own lowering is complete.
    std::unordered_set<SymbolId> directSymbols;
    std::unordered_set<SymbolId> procedureSymbols;
    std::unordered_set<SymbolId> factTypes;
    for (const auto& clause : program.clauses) {
        if (!clause) continue;
        auto& declarations = clause->isFact() ? factTypes : procedureSymbols;
        if (!declarations.insert(clause->head.nameId).second ||
            (clause->isFact() ? procedureSymbols.contains(clause->head.nameId)
                              : factTypes.contains(clause->head.nameId))) {
            throw IntegerParserError("not yet lowered to IR: duplicate procedure declaration at " +
                std::to_string(clause->sourceSpan.startLine) + ":" +
                std::to_string(clause->sourceSpan.startColumn));
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
    const DirectCompileContext context{procedureSymbols, factTypes};
    for (const auto& clause : program.clauses) {
        if (!clause || !clause->isFact()) continue;
        IrFactType type;
        type.symbol = clause->head.nameId;
        for (const auto& parent : clause->parentNames) {
            const auto parentSymbol = symbolIdForName(parent);
            if (!factTypes.contains(parentSymbol)) {
                throw IntegerParserError("not yet lowered to IR: unknown fact parent at " +
                    std::to_string(clause->sourceSpan.startLine) + ":" +
                    std::to_string(clause->sourceSpan.startColumn));
            }
            type.parents.push_back(parentSymbol);
        }
        type.sourceSpan = {clause->sourceSpan.startLine, clause->sourceSpan.startColumn,
                           clause->sourceSpan.endLine, clause->sourceSpan.endColumn};
        module.factTypes.push_back(std::move(type));
    }
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
        for (const auto& binding : program.globals) {
            appendFragment(module.ir, IntegerParser::compileAstGlobalBindingIr(*binding, factTypes), true);
        }
        for (const auto& clause : program.clauses) {
                if (clause->isFact()) continue;
                std::vector<IrSymbolRef> parameters;
                parameters.reserve(clause->head.args.size());
                for (const auto& parameter : clause->head.args) parameters.push_back(parameter.nameId);
                const auto [_, inserted] = module.procedures.emplace(clause->head.nameId, IrProcedure{
                    compileDirectProcedure(*clause, factTypes), parameters, parameters,
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
        verifyIrModule(module);
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
                span = clause ? clause->sourceSpan : SourceSpan{};
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
    
