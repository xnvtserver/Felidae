#include "form/IrModule.h"
#include "form/RegisterVm.h"

#include <cassert>
#include <utility>

int main() {
    using namespace Felidae;

    constexpr IrSymbolRef kMain = 1;
    IrModule module;
    module.entryProcedure = kMain;
    module.ir.registerCount = 1;
    module.ir.symbols = {kMain};
    module.ir.words = {static_cast<IrWord>(IrOpcode::Call), 0, 0, 0,
                       static_cast<IrWord>(IrOpcode::Return), 0, 0,
                       static_cast<IrWord>(IrOpcode::End)};
    IrProcedure procedure;
    procedure.ir.registerCount = 1;
    procedure.ir.constants = {encodeIrNumber(42.0)};
    procedure.ir.constantKinds = {IrConstantKind::Number};
    procedure.ir.words = {static_cast<IrWord>(IrOpcode::LoadConst), 0, 0,
                          static_cast<IrWord>(IrOpcode::Return), 0, 0,
                          static_cast<IrWord>(IrOpcode::End)};
    module.procedures.emplace(kMain, std::move(procedure));
    auto verified = verifyIrModule(std::move(module));
    FelidaeKnowledgeRuntime runtime;
    RegisterVm vm;
    assert(std::get<double>(vm.executeMain(verified, runtime)) == 42.0);
}
