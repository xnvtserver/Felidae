#pragma once

#include <cstdint>

namespace Felidae {

// Permanent semantic ABI namespace shared by structured compiler IR and ISA.
enum class SemanticOperationId : std::uint16_t {
    Identity = 0x0001,
    SelectFact = 0x0002,
    DeriveFact = 0x0003,
    EvaluateDegree = 0x0004,
};

} // namespace Felidae
