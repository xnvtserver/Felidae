#include "IrCodeGenerator.h"

#include "IntegerParser.h"
#include "Symbol.h"

#include <algorithm>
#include <unordered_set>

namespace Felidae {
namespace {

thread_local const std::unordered_set<SymbolId>* activeDirectProcedures = nullptr;

class DirectProcedureScope {
public:
    explicit DirectProcedureScope(const std::unordered_set<SymbolId>& procedures)
        : previous_(activeDirectProcedures) { activeDirectProcedures = &procedures; }
    ~DirectProcedureScope() { activeDirectProcedures = previous_; }
private:
    const std::unordered_set<SymbolId>* previous_;
};

// The strict module compiler is intentionally closed: every emitted
// LoadSymbol/Call must resolve through typed module globals or procedures.
// No instruction can reconstruct an AST value or invoke Interpreter services.
bool isDirectDeterministicExpression(const std::shared_ptr<Expr>& expression,
                                     const std::unordered_set<SymbolId>& definedSymbols) {
    if (!expression) return false;
    if (std::dynamic_pointer_cast<NumberExpr>(expression) ||
        std::dynamic_pointer_cast<BoolExpr>(expression) ||
        std::dynamic_pointer_cast<StringExpr>(expression) ||
        std::dynamic_pointer_cast<NilExpr>(expression)) {
        return true;
    }
    if (const auto array = std::dynamic_pointer_cast<ArrayExpr>(expression)) {
        return std::all_of(array->items.begin(), array->items.end(), [&](const auto& item) {
            return isDirectDeterministicExpression(item, definedSymbols);
        });
    }
    if (const auto map = std::dynamic_pointer_cast<MapExpr>(expression)) {
        return std::all_of(map->entries.begin(), map->entries.end(), [&](const MapEntry& entry) {
            return isDirectDeterministicExpression(entry.value, definedSymbols);
        });
    }
    if (const auto access = std::dynamic_pointer_cast<AccessExpr>(expression)) {
        return isDirectDeterministicExpression(access->target, definedSymbols);
    }
    if (const auto operation = std::dynamic_pointer_cast<OperatorExpression>(expression)) {
        if (operation->coreOperator == CoreOperator::Unknown) return false;
        for (std::size_t index = 0; index < operation->captureCount(); ++index) {
            if (!isDirectDeterministicExpression(operation->capture(index), definedSymbols)) return false;
        }
        return true;
    }
    if (const auto variable = std::dynamic_pointer_cast<VarExpr>(expression)) {
        return definedSymbols.contains(variable->nameId);
    }
    if (const auto term = std::dynamic_pointer_cast<TermExpr>(expression)) {
        return activeDirectProcedures && activeDirectProcedures->contains(term->nameId) &&
            std::all_of(term->args.begin(), term->args.end(), [&](const Arg& argument) {
            return isDirectDeterministicExpression(argument.value, definedSymbols);
        });
    }
    return false; // calls, lambdas, fact selections, and AST-only values
}

const SourceSpan& firstUnsupportedExpression(const std::shared_ptr<Expr>& expression,
                                             const std::unordered_set<SymbolId>& definedSymbols) {
    if (!expression) {
        static const SourceSpan unknown{};
        return unknown;
    }
    if (const auto array = std::dynamic_pointer_cast<ArrayExpr>(expression)) {
        for (const auto& item : array->items) {
            if (!isDirectDeterministicExpression(item, definedSymbols))
                return firstUnsupportedExpression(item, definedSymbols);
        }
    } else if (const auto map = std::dynamic_pointer_cast<MapExpr>(expression)) {
        for (const auto& item : map->entries) {
            if (!isDirectDeterministicExpression(item.value, definedSymbols))
                return firstUnsupportedExpression(item.value, definedSymbols);
        }
    } else if (const auto access = std::dynamic_pointer_cast<AccessExpr>(expression)) {
        if (!isDirectDeterministicExpression(access->target, definedSymbols))
            return firstUnsupportedExpression(access->target, definedSymbols);
    } else if (const auto operation = std::dynamic_pointer_cast<OperatorExpression>(expression)) {
        if (operation->coreOperator == CoreOperator::Unknown) return expression->sourceSpan;
        for (std::size_t index = 0; index < operation->captureCount(); ++index) {
            const auto captured = operation->capture(index);
            if (!isDirectDeterministicExpression(captured, definedSymbols))
                return firstUnsupportedExpression(captured, definedSymbols);
        }
    } else if (const auto term = std::dynamic_pointer_cast<TermExpr>(expression)) {
        if (!activeDirectProcedures || !activeDirectProcedures->contains(term->nameId)) {
            return expression->sourceSpan;
        }
        for (const auto& argument : term->args) {
            if (!isDirectDeterministicExpression(argument.value, definedSymbols))
                return firstUnsupportedExpression(argument.value, definedSymbols);
        }
    }
    return expression->sourceSpan;
}

bool isDirectReturn(const std::shared_ptr<Goal>& goal,
                    const std::unordered_set<SymbolId>& definedSymbols) {
    const auto returned = std::dynamic_pointer_cast<ReturnGoal>(goal);
    return returned && std::all_of(returned->fields.begin(), returned->fields.end(), [&](const Arg& field) {
        return isDirectDeterministicExpression(field.value, definedSymbols);
    });
}

bool isDirectCondition(const std::shared_ptr<Goal>& goal,
                       const std::unordered_set<SymbolId>& definedSymbols) {
    const auto comparison = std::dynamic_pointer_cast<BinaryGoal>(goal);
    if (!comparison || !isDirectDeterministicExpression(comparison->left, definedSymbols) ||
        !isDirectDeterministicExpression(comparison->right, definedSymbols)) return false;
    switch (comparison->op) {
    case TokenId::EQUAL: case TokenId::NOT_EQUAL: case TokenId::LESS: case TokenId::LESS_EQUAL:
    case TokenId::GREATER: case TokenId::GREATER_EQUAL:
        return true;
    default:
        return false;
    }
}

bool isDirectEntry(const ClauseStmt& method, const std::unordered_set<SymbolId>& definedSymbols) {
    if (method.head.nameId != symbolIdForName("main") || !method.head.args.empty() ||
        method.body.empty() || !method.fallbackBranches.empty()) return false;
    if (method.body.size() == 1 && isDirectReturn(method.body.front(), definedSymbols)) return true;
    if (method.body.size() == 1) {
        const auto conditional = std::dynamic_pointer_cast<IfGoal>(method.body.front());
        return conditional && isDirectCondition(conditional->condition, definedSymbols) &&
            conditional->thenBranch.size() == 1 && isDirectReturn(conditional->thenBranch.front(), definedSymbols) &&
            conditional->elseBranch.size() == 1 && isDirectReturn(conditional->elseBranch.front(), definedSymbols);
    }
    auto visible = definedSymbols;
    for (std::size_t index = 0; index + 1 < method.body.size(); ++index) {
        const auto assignment = std::dynamic_pointer_cast<AssignGoal>(method.body[index]);
        // Method locals are immutable and must not silently overwrite a
        // global/direct binding in this first VM environment slice.
        if (!assignment || !assignment->expr || visible.contains(assignment->nameId) ||
            !isDirectDeterministicExpression(assignment->expr, visible)) return false;
        visible.insert(assignment->nameId);
    }
    return isDirectReturn(method.body.back(), visible);
}

bool isDirectProcedure(const ClauseStmt& method, const std::unordered_set<SymbolId>& globals) {
    if (method.body.empty() || !method.fallbackBranches.empty()) return false;
    auto visible = globals;
    for (const auto& parameter : method.head.args) {
        if (parameter.name.empty() || parameter.nameId == 0 || !visible.insert(parameter.nameId).second) return false;
    }
    if (method.body.size() == 1 && isDirectReturn(method.body.front(), visible)) return true;
    if (method.body.size() == 1) {
        const auto conditional = std::dynamic_pointer_cast<IfGoal>(method.body.front());
        return conditional && isDirectCondition(conditional->condition, visible) &&
            conditional->thenBranch.size() == 1 && isDirectReturn(conditional->thenBranch.front(), visible) &&
            conditional->elseBranch.size() == 1 && isDirectReturn(conditional->elseBranch.front(), visible);
    }
    auto methodLocals = visible;
    for (std::size_t index = 0; index + 1 < method.body.size(); ++index) {
        const auto assignment = std::dynamic_pointer_cast<AssignGoal>(method.body[index]);
        if (!assignment || !assignment->expr || methodLocals.contains(assignment->nameId) ||
            !isDirectDeterministicExpression(assignment->expr, methodLocals)) return false;
        methodLocals.insert(assignment->nameId);
    }
    return isDirectReturn(method.body.back(), methodLocals);
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
        case IrOpcode::ExecuteProgram: width = 2; break;
        case IrOpcode::LoadConst: reg(fragment.words[pc + 1]); fragment.words[pc + 2] += constantBase; width = 3; break;
        case IrOpcode::LoadSymbol: reg(fragment.words[pc + 1]); symbol(fragment.words[pc + 2]); width = 3; break;
        case IrOpcode::StoreSymbol: symbol(fragment.words[pc + 1]); reg(fragment.words[pc + 2]); width = 3; break;
        case IrOpcode::Move: reg(fragment.words[pc + 1]); reg(fragment.words[pc + 2]); width = 3; break;
        case IrOpcode::JumpIfFalse: reg(fragment.words[pc + 1]); fragment.words[pc + 2] += wordBase; width = 3; break;
        case IrOpcode::CallNative: case IrOpcode::MakeFact: reg(fragment.words[pc + 1]); symbol(fragment.words[pc + 2]); width = 3; break;
        case IrOpcode::Return: reg(fragment.words[pc + 1]); width = 3; break;
        case IrOpcode::Add: case IrOpcode::Sub: case IrOpcode::Mul: case IrOpcode::Div: case IrOpcode::Mod:
            reg(fragment.words[pc + 1]); reg(fragment.words[pc + 2]); reg(fragment.words[pc + 3]); width = 4; break;
        case IrOpcode::Compare:
            reg(fragment.words[pc + 1]); reg(fragment.words[pc + 2]); reg(fragment.words[pc + 3]); width = 5; break;
        case IrOpcode::GetField:
            reg(fragment.words[pc + 1]); reg(fragment.words[pc + 2]); symbol(fragment.words[pc + 3]); width = 4; break;
        case IrOpcode::SetField:
            reg(fragment.words[pc + 1]); symbol(fragment.words[pc + 2]); reg(fragment.words[pc + 3]); width = 4; break;
        case IrOpcode::Call: case IrOpcode::SemanticEval: case IrOpcode::MakeArray: {
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

FelidaeIr compileDirectConditionalEntry(const ClauseStmt& method) {
    const auto conditional = std::dynamic_pointer_cast<IfGoal>(method.body.front());
    const auto comparison = std::dynamic_pointer_cast<BinaryGoal>(conditional->condition);
    auto expression = std::make_shared<OperatorExpression>(directConditionOperator(comparison->op),
                                                            comparison->left, comparison->right);
    expression->sourceSpan = comparison->sourceSpan;
    auto conditionIr = IntegerParser::compileAstExpressionIr(expression);
    const auto conditionRegister = conditionIr.words.at(conditionIr.words.size() - 3);
    FelidaeIr result;
    appendFragment(result, std::move(conditionIr), true);
    const auto elseJump = result.words.size();
    result.words.insert(result.words.end(), {static_cast<IrWord>(IrOpcode::JumpIfFalse),
                                             conditionRegister, 0});
    ClauseStmt thenMethod(method.head, conditional->thenBranch);
    appendFragment(result, IntegerParser::compileAstEntryMethodIr(thenMethod), false, true);
    const auto endJump = result.words.size();
    result.words.insert(result.words.end(), {static_cast<IrWord>(IrOpcode::Jump), 0});
    const auto elseTarget = result.words.size();
    ClauseStmt elseMethod(method.head, conditional->elseBranch);
    appendFragment(result, IntegerParser::compileAstEntryMethodIr(elseMethod), false);
    result.words[elseJump + 2] = elseTarget;
    result.words[endJump + 1] = result.words.size() - 1; // the final END boundary
    IrVerifier::verify(result);
    return result;
}

FelidaeIr compileDirectSequentialEntry(const ClauseStmt& method) {
    FelidaeIr result;
    for (std::size_t index = 0; index + 1 < method.body.size(); ++index) {
        const auto assignment = std::dynamic_pointer_cast<AssignGoal>(method.body[index]);
        GlobalBindingStmt binding(assignment->name, assignment->expr);
        binding.sourceSpan = assignment->sourceSpan;
        appendFragment(result, IntegerParser::compileAstGlobalBindingIr(binding), true);
    }
    ClauseStmt returned(method.head, {method.body.back()});
    appendFragment(result, IntegerParser::compileAstEntryMethodIr(returned), false);
    IrVerifier::verify(result);
    return result;
}

FelidaeIr compileDirectProcedure(const ClauseStmt& method) {
    if (method.body.size() == 1) {
        if (std::dynamic_pointer_cast<IfGoal>(method.body.front())) {
            return compileDirectConditionalEntry(method);
        }
        return IntegerParser::compileAstEntryMethodIr(method);
    }
    return compileDirectSequentialEntry(method);
}

} // namespace

IrModule IrCodeGenerator::compile(Program program) const {
    IrModule module;
    // First production compiler slice: an isolated simple main can execute
    // as real IR without crossing into Interpreter. Unsupported constructs
    // remain on the compatibility path until their own lowering is complete.
    std::unordered_set<SymbolId> directSymbols;
    std::unordered_set<SymbolId> procedureSymbols;
    for (const auto& clause : program.clauses) {
        if (!clause) continue;
        if (!procedureSymbols.insert(clause->head.nameId).second) {
            throw IntegerParserError("not yet lowered to IR: duplicate procedure declaration at " +
                std::to_string(clause->sourceSpan.startLine) + ":" +
                std::to_string(clause->sourceSpan.startColumn));
        }
    }
    const DirectProcedureScope procedureScope(procedureSymbols);
    bool directGlobals = program.imports.empty() && !program.clauses.empty();
    for (const auto& binding : program.globals) {
        if (!binding || !isDirectDeterministicExpression(binding->expr, directSymbols)) {
            directGlobals = false;
            break;
        }
        directSymbols.insert(symbolIdForName(binding->name));
    }
    const auto main = std::find_if(program.clauses.begin(), program.clauses.end(), [](const auto& clause) {
        return clause && clause->head.nameId == symbolIdForName("main");
    });
    const bool eligibleMethods = std::all_of(program.clauses.begin(), program.clauses.end(),
        [&](const auto& clause) {
            return clause && (clause->head.nameId == symbolIdForName("main")
                ? isDirectEntry(*clause, directSymbols) : isDirectProcedure(*clause, directSymbols));
        });
    if (directGlobals && main != program.clauses.end() && eligibleMethods &&
        isDirectEntry(**main, directSymbols)) {
        for (const auto& binding : program.globals) {
            appendFragment(module.ir, IntegerParser::compileAstGlobalBindingIr(*binding), true);
        }
        for (const auto& clause : program.clauses) {
                std::vector<IrSymbolRef> parameters;
                parameters.reserve(clause->head.args.size());
                for (const auto& parameter : clause->head.args) parameters.push_back(parameter.nameId);
                const auto [_, inserted] = module.procedures.emplace(clause->head.nameId, IrProcedure{
                    compileDirectProcedure(*clause), parameters, parameters,
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
        if (!binding || !isDirectDeterministicExpression(binding->expr, directSymbols)) {
            span = binding && binding->expr
                ? firstUnsupportedExpression(binding->expr, directSymbols) : SourceSpan{};
            foundUnsupportedSpan = true;
            break;
        }
    }
    if (!foundUnsupportedSpan) {
        for (const auto& clause : program.clauses) {
            const auto eligible = clause && (clause->head.nameId == symbolIdForName("main")
                ? isDirectEntry(*clause, directSymbols) : isDirectProcedure(*clause, directSymbols));
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
    