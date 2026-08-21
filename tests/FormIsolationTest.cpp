#include "form/BinaryIr.h"
#include "form/RegisterVm.h"

#include <cassert>

int main() {
    using namespace Felidae;

    constexpr IrSymbolRef kMain = 0x1001;
    IrModule module;
    module.entryProcedure = kMain;
    module.ir.registerCount = 1;
    module.ir.symbols = {kMain};
    module.ir.words = {
        static_cast<IrWord>(IrOpcode::Call), 0, 0, 0,
        static_cast<IrWord>(IrOpcode::Return), 0, 0,
        static_cast<IrWord>(IrOpcode::End)};

    IrProcedure procedure;
    procedure.ir.registerCount = 1;
    procedure.ir.constants = {encodeIrNumber(42.0)};
    procedure.ir.constantKinds = {IrConstantKind::Number};
    procedure.ir.words = {
        static_cast<IrWord>(IrOpcode::LoadConst), 0, 0,
        static_cast<IrWord>(IrOpcode::Return), 0, 0,
        static_cast<IrWord>(IrOpcode::End)};
    module.procedures.emplace(kMain, std::move(procedure));

    verifyIrModule(module);
    FelidaeKnowledgeRuntime runtime(module.procedures);
    RegisterVm vm;
    assert(std::get<double>(vm.executeMain(module, runtime)) == 42.0);
}
