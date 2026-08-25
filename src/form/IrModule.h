#pragma once

#include "../FelidaeIr.h"

#include <unordered_map>
#include <vector>

namespace Felidae {

struct IrFactType {
    IrSymbolRef symbol = 0;
    std::vector<IrSymbolRef> parents;
    IrSourceMapEntry::Span sourceSpan;
};

// AST-free internal compiler module. It is verified and deterministically
// lowered to IsaModule; neither the binary writer nor the VM consumes it.
struct IrModule {
    FelidaeIr ir; // module initializer
    std::unordered_map<IrSymbolRef, IrProcedure> procedures;
    std::vector<IrFactType> factTypes;
    IrSymbolRef entryProcedure = 0;
};

// Authoritative decoder for compiler-only IR. It validates both fixed and
// variable-width instructions before returning their encoded word count.
std::size_t compilerInstructionWidth(const FelidaeIr& ir, std::size_t pc);
void verifyIrModule(const IrModule& module);

} // namespace Felidae
