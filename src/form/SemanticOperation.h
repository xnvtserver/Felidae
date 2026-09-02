#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace Felidae {

// Permanent semantic ABI namespace shared by compiler output and executable IR.
enum class SemanticOperationId : std::uint16_t {
  Identity = 0x0001,
  SelectFact = 0x0002,
  DeriveFact = 0x0003,
  EvaluateDegree = 0x0004,
  // Explicit application-facing scoring hook. Unlike EvaluateDegree, its
  // result is an ordinary finite double and is never clamped to [0, 1].
  Suggest = 0x0005,
};

inline bool isKnownSemanticOperation(std::uint16_t operation) noexcept {
  switch (static_cast<SemanticOperationId>(operation)) {
  case SemanticOperationId::Identity:
  case SemanticOperationId::SelectFact:
  case SemanticOperationId::DeriveFact:
  case SemanticOperationId::EvaluateDegree:
  case SemanticOperationId::Suggest:
    return true;
  }
  return false;
}

inline bool semanticOperationAcceptsArity(std::uint16_t operation,
                                          std::size_t inputCount) noexcept {
  return isKnownSemanticOperation(operation) && inputCount == 1;
}

inline std::optional<SemanticOperationId>
semanticOperationForName(std::string_view name) noexcept {
  if (name == "semantic_identity")
    return SemanticOperationId::Identity;
  if (name == "semantic_select_fact")
    return SemanticOperationId::SelectFact;
  if (name == "semantic_derive_fact")
    return SemanticOperationId::DeriveFact;
  if (name == "semantic_evaluate_degree")
    return SemanticOperationId::EvaluateDegree;
  if (name == "ssm.suggest" || name == "ssm:suggest")
    return SemanticOperationId::Suggest;
  return std::nullopt;
}

} // namespace Felidae
