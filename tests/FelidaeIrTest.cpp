#include "form/IrModule.h"
#include "form/RegisterVm.h"

#include <cassert>
#include <functional>

namespace {
bool rejects(const std::function<void()>& action) {
    try { action(); } catch (const Felidae::IrError&) { return true; }
    return false;
}

Felidae::IrModule returning(Felidae::IrConstant value, Felidae::IrConstantKind kind) {
    using namespace Felidae;
    IrModule module;
    module.sentencePieceModelIdentity = "sha256:test";
    module.symbolTable = {{1}};
    module.entryProcedure = 1;
    module.ir.registerCount = 1;
    module.ir.symbols = {1};
    module.ir.words = {static_cast<IrWord>(IrOpcode::Call), 0, 0, 0,
                       static_cast<IrWord>(IrOpcode::Return), 0, 0,
                       static_cast<IrWord>(IrOpcode::End)};
    IrProcedure procedure;
    procedure.ir.registerCount = 1;
    procedure.ir.constants = {{kind, value}};
    procedure.ir.words = {static_cast<IrWord>(IrOpcode::LoadConst), 0, 0,
                          static_cast<IrWord>(IrOpcode::Return), 0, 0,
                          static_cast<IrWord>(IrOpcode::End)};
    module.procedures.emplace(1, std::move(procedure));
    return module;
}
}

int main() {
    using namespace Felidae;
    static_assert(sizeof(IrWord) == 4);
    static_assert(sizeof(IrConstant) == 8);
    FelidaeKnowledgeRuntime runtime;
    auto number = verifyIrModule(returning(encodeIrNumber(42.0), IrConstantKind::Number));
    assert(std::get<double>(RegisterVm{}.executeMain(number, runtime)) == 42.0);

    FelidaeKnowledgeRuntime truthRuntime;
    auto truth = verifyIrModule(returning(1, IrConstantKind::Boolean));
    const auto truthValue = RegisterVm{}.executeMain(truth, truthRuntime);
    assert(std::holds_alternative<double>(truthValue));
    assert(std::get<double>(truthValue) == 1.0);
    assert(vmValueToDisplayString(truthValue) == "1.0");

    FelidaeKnowledgeRuntime falseRuntime;
    auto falseTruth = verifyIrModule(returning(0, IrConstantKind::Boolean));
    const auto falseValue = RegisterVm{}.executeMain(falseTruth, falseRuntime);
    assert(std::holds_alternative<double>(falseValue));
    assert(std::get<double>(falseValue) == 0.0);
    assert(vmValueToDisplayString(falseValue) == "0.0");

    auto invalid = returning(encodeIrNumber(1.0), IrConstantKind::Number);
    invalid.procedures.at(1).ir.words[1] = 99;
    assert(rejects([&] { (void)verifyIrModule(std::move(invalid)); }));

    IrModule loop;
    loop.sentencePieceModelIdentity = "sha256:test";
    loop.symbolTable = {{1}};
    loop.entryProcedure = 1;
    loop.ir.registerCount = 1;
    loop.ir.symbols = {1};
    loop.ir.words = {static_cast<IrWord>(IrOpcode::Call), 0, 0, 0,
                     static_cast<IrWord>(IrOpcode::Return), 0, 0,
                     static_cast<IrWord>(IrOpcode::End)};
    IrProcedure loopProcedure;
    loopProcedure.ir.registerCount = 0;
    loopProcedure.ir.words = {static_cast<IrWord>(IrOpcode::Jump), 0,
                              static_cast<IrWord>(IrOpcode::End)};
    loop.procedures.emplace(1, std::move(loopProcedure));
    auto verifiedLoop = verifyIrModule(std::move(loop));
    FelidaeKnowledgeRuntime loopRuntime;
    assert(rejects([&] { (void)RegisterVm{16}.executeMain(verifiedLoop, loopRuntime); }));
    return 0;
}
