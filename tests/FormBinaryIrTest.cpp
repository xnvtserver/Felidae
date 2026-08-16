#include "form/BinaryIr.h"
#include "Symbol.h"

#include <cassert>
#include <filesystem>

int main() {
    using namespace Felidae;

    const auto mainSymbol = symbolIdForName("main");
    IrModule module;
    module.entryProcedure = mainSymbol;
    module.ir.registerCount = 1;
    module.ir.symbols = {mainSymbol};
    module.ir.words = {
        static_cast<IrWord>(IrOpcode::Call), 0, 0, 0,
        static_cast<IrWord>(IrOpcode::Return), 0, 0,
        static_cast<IrWord>(IrOpcode::End)};

    IrProcedure main;
    // Register 22 happens to be the numeric value of the legacy opcode.
    // It remains a legal operand; only instruction-boundary opcodes are
    // forbidden from a binary module.
    main.ir.registerCount = 23;
    main.ir.constants = {encodeIrNumber(42.0)};
    main.ir.constantKinds = {IrConstantKind::Number};
    main.ir.words = {
        static_cast<IrWord>(IrOpcode::LoadConst), 22, 0,
        static_cast<IrWord>(IrOpcode::Return), 22, 0,
        static_cast<IrWord>(IrOpcode::End)};
    module.procedures.emplace(mainSymbol, std::move(main));

    const auto path = std::filesystem::temp_directory_path() / "felidae_form_standalone.fir";
    writeBinaryIr(path, module);
    const auto loaded = loadBinaryIr(path);
    DirectVmRuntime runtime(loaded.procedures);
    RegisterVm vm;
    assert(std::get<double>(vm.executeMain(loaded, runtime)) == 42.0);
    std::filesystem::remove(path);
}
