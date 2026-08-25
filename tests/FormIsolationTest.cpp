#include "form/FelidaeIsa.h"
#include "form/RegisterVm.h"

#include <cassert>
#include <utility>

int main() {
    using namespace Felidae;

    constexpr IrSymbolRef kMain = 0x1001;
    IsaModule isa;
    isa.entryProcedure = 0;
    isa.procedureSymbols = {kMain};
    isa.initializer.code = {{
        encodeIsaABC(IsaOpcode::Call, 0, 0), 0,
        encodeIsaABC(IsaOpcode::Return, 0)}, 1};
    IsaProcedure procedure;
    procedure.program.code = {{
        encodeIsaABx(IsaOpcode::LoadConstant, 0, 0),
        encodeIsaABC(IsaOpcode::Return, 0)}, 1};
    procedure.program.constants = {encodeIrNumber(42.0)};
    procedure.program.constantKinds = {IrConstantKind::Number};
    isa.procedures.push_back(std::move(procedure));

    verifyIsaModule(isa);
    FelidaeKnowledgeRuntime runtime;
    RegisterVm vm;
    assert(std::get<double>(vm.executeIsaMain(isa, runtime)) == 42.0);
}
