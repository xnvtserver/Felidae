#include "FelidaeRuntime.h"
#include "FelidaeIr.h"
#include "Interpreter.h"
#include "IntegerParser.h"
#include "IntegerTokenList.h"
#include "SentencePieceModel.h"

#include <cassert>
#include <memory>

int main() {
    // This covers the production path: source -> SentencePiece IDs -> integer
    // parser -> executable intermediate tree -> interpreter.
    const auto program = Felidae::parseProgramFile(FELIDAE_PIPELINE_FIXTURE_PATH);
    assert(program.clauses.size() == 1);
    assert(program.clauses.front()->head.name == "main");
    // Source spans come from the original SentencePiece offsets. This covers
    // the parser's indexed span path: no secondary source tokenization and no
    // per-node linear rescan of the encoded stream.
    assert(program.clauses.front()->sourceSpan.startLine == 1);
    assert(program.clauses.front()->sourceSpan.startColumn == 1);
    assert(program.clauses.front()->sourceSpan.endLine >= 3);
    assert(program.clauses.front()->body.size() == 2);
    assert(program.clauses.front()->body.front()->sourceSpan.startLine == 2);
    assert(program.clauses.front()->body.back()->sourceSpan.startLine == 3);

    {
    Felidae::Interpreter interpreter;
    interpreter.addProgram(program);
    assert(interpreter.hasMethod("main"));

    const auto result = interpreter.callMain(
        std::make_shared<Felidae::ArrayExpr>(std::vector<std::shared_ptr<Felidae::Expr>>{}));
    const auto returned = std::dynamic_pointer_cast<Felidae::MapExpr>(result);
    assert(returned);
    assert(returned->entries.size() == 1);
    assert(returned->entries.front().key == "answer");
    const auto answer = std::dynamic_pointer_cast<Felidae::NumberExpr>(
        returned->entries.front().value);
    assert(answer && answer->value == 42.0);
    }

    // The public execution path has a verified integer IR boundary. The VM
    // delegates language semantics to the existing runtime service rather
    // than duplicating Interpreter's evaluator.
    const auto module = Felidae::compileProgramFileToIr(FELIDAE_PIPELINE_FIXTURE_PATH);
    Felidae::IrVerifier::verify(module.ir);
    Felidae::LegacyVmRuntime vmRuntime(module);
    assert(vmRuntime.shouldBranchFalse(Felidae::legacyVmValue(std::make_shared<Felidae::NilExpr>())));
    assert(!vmRuntime.shouldBranchFalse(Felidae::legacyVmValue(std::make_shared<Felidae::NumberExpr>(0.0))));
    assert(!vmRuntime.shouldBranchFalse(Felidae::legacyVmValue(std::make_shared<Felidae::NumberExpr>(1.0))));
    Felidae::RegisterVm vm;
    const auto vmResult = vm.execute(module.ir, vmRuntime, Felidae::legacyVmValue(
        std::make_shared<Felidae::ArrayExpr>(std::vector<std::shared_ptr<Felidae::Expr>>{})));
    assert(vmRuntime.services().valueToDisplayString(Felidae::legacyExprFromVmValue(vmResult)) == "{answer: 42}");

    // Primitive normal syntax is directly lowered from the integer parser to
    // canonical IR and evaluated by the register VM, without Interpreter.
    Felidae::IntegerTokenList expressionInput(Felidae::felidaeSentencePieceModel(), "6 * 7");
    Felidae::IntegerParser expressionParser(expressionInput);
    const auto expressionIr = expressionParser.compileExpressionIr();
    class NoRuntime final : public Felidae::VmRuntime {
    public:
        Felidae::VmValue executeProgram(Felidae::IrWord, const Felidae::VmValue&) override {
            throw Felidae::IrError("unexpected runtime call");
        }
        Felidae::VmValue callSymbol(Felidae::IrSymbolRef, std::span<const Felidae::VmValue> arguments) override {
            double total = 0.0;
            for (const auto& argument : arguments) total += std::get<double>(argument);
            return total;
        }
        Felidae::VmValue callSymbolNamed(Felidae::IrSymbolRef,
                                         std::span<const Felidae::VmCallArgument> arguments) override {
            double total = 0.0;
            for (const auto& argument : arguments) total += std::get<double>(argument.value);
            return total;
        }
    } noRuntime;
    Felidae::RegisterVm nativeVm;
    assert(std::get<double>(nativeVm.execute(expressionIr, noRuntime, Felidae::VmNil{})) == 42.0);
    const auto runtimeExpressionIr = Felidae::tryCompileExpressionTextToIr("10 - 3");
    assert(runtimeExpressionIr);
    assert(std::get<double>(nativeVm.execute(*runtimeExpressionIr, noRuntime, Felidae::VmNil{})) == 7.0);
    const auto comparisonIr = Felidae::tryCompileExpressionTextToIr("3 < 7");
    assert(comparisonIr);
    assert(std::get<bool>(nativeVm.execute(*comparisonIr, noRuntime, Felidae::VmNil{})));
    const auto logicIr = Felidae::tryCompileExpressionTextToIr("not false and true");
    assert(logicIr);
    assert(std::get<bool>(nativeVm.execute(*logicIr, noRuntime, Felidae::VmNil{})));
    const auto moduloIr = Felidae::tryCompileExpressionTextToIr("17 % 5");
    assert(moduloIr);
    assert(std::get<double>(nativeVm.execute(*moduloIr, noRuntime, Felidae::VmNil{})) == 2.0);
    const auto callIr = Felidae::tryCompileExpressionTextToIr("combine(6, 7)");
    assert(callIr);
    assert(std::get<double>(nativeVm.execute(*callIr, noRuntime, Felidae::VmNil{})) == 13.0);
    const auto namedCallIr = Felidae::tryCompileExpressionTextToIr("combine(left: 6, right: 7)");
    assert(namedCallIr);
    assert(std::get<double>(nativeVm.execute(*namedCallIr, noRuntime, Felidae::VmNil{})) == 13.0);
    const auto textIr = Felidae::tryCompileExpressionTextToIr("\"mixfix text\"");
    assert(textIr);
    assert(std::get<Felidae::VmText>(nativeVm.execute(*textIr, noRuntime, Felidae::VmNil{})).utf8 == "mixfix text");
    const auto nilIr = Felidae::tryCompileExpressionTextToIr("nil");
    assert(nilIr);
    assert(std::holds_alternative<Felidae::VmNil>(nativeVm.execute(*nilIr, noRuntime, Felidae::VmNil{})));
    const auto arrayIr = Felidae::tryCompileExpressionTextToIr("[1, 2, 3]");
    assert(arrayIr);
    const auto array = std::get<Felidae::VmArrayPtr>(nativeVm.execute(*arrayIr, noRuntime, Felidae::VmNil{}));
    assert(array && array->values.size() == 3 && std::get<double>(array->values[2]) == 3.0);
    const auto mapIr = Felidae::tryCompileExpressionTextToIr("{answer: 42, enabled: true}");
    assert(mapIr);
    const auto map = std::get<Felidae::VmMapPtr>(nativeVm.execute(*mapIr, noRuntime, Felidae::VmNil{}));
    assert(map && map->entries.size() == 2);
    assert(std::get<double>(map->entries[0].second) == 42.0);
    assert(std::get<bool>(map->entries[1].second));
    const auto fieldIr = Felidae::tryCompileExpressionTextToIr("{answer: 42}:answer");
    assert(fieldIr);
    assert(std::get<double>(nativeVm.execute(*fieldIr, noRuntime, Felidae::VmNil{})) == 42.0);
    const auto aggregateEqualityIr = Felidae::tryCompileExpressionTextToIr("[1, {answer: 42}] == [1, {answer: 42}]");
    assert(aggregateEqualityIr);
    assert(std::get<bool>(nativeVm.execute(*aggregateEqualityIr, noRuntime, Felidae::VmNil{})));
}
