#pragma once

#include <cstdint>
#include <cstddef>

namespace Felidae {

// Permanent semantic ABI namespace shared by structured compiler IR and ISA.
enum class SemanticOperationId : std::uint16_t {
    Identity = 0x0001,
    SelectFact = 0x0002,
    DeriveFact = 0x0003,
    EvaluateDegree = 0x0004,
};

inline bool isKnownSemanticOperation(std::uint16_t operation) noexcept {
    switch (static_cast<SemanticOperationId>(operation)) {
    case SemanticOperationId::Identity:
    case SemanticOperationId::SelectFact:
    case SemanticOperationId::DeriveFact:
    case SemanticOperationId::EvaluateDegree: return true;
    }
    return false;
}

inline bool semanticOperationAcceptsArity(std::uint16_t operation,
                                          std::size_t inputCount) noexcept {
    return isKnownSemanticOperation(operation) && inputCount == 1;
}

} // namespace Felidae
