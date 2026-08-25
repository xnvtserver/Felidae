#pragma once

#include "FelidaeIsa.h"
#include "IrModule.h"

#include <cstdint>
#include <unordered_map>

namespace Felidae {

// Sole compiler-IR -> executable-ISA boundary. Input IR is verified first.
// Runtime code includes FelidaeIsa.h and cannot submit compiler words here.
class IsaLowerer {
public:
    static IsaBlock lower(
        const FelidaeIr& ir,
        const std::unordered_map<IrSymbolRef, std::uint16_t>& procedures);
    static IsaModule lowerModule(const IrModule& module);
};

} // namespace Felidae
