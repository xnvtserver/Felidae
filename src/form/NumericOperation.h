#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace Felidae {

// Stable operand selector for the executable IR Numeric instruction. Boolean
// classifiers still return the VM's numeric truth values 0.0 and 1.0.
enum class NumericOperation : std::uint8_t {
  Min = 0,
  Max,
  Abs,
  Diff,
  Average,
  WeightedAverage,
  Clamp,
  Floor,
  Ceil,
  Round,
  Trunc,
  Sqrt,
  Cbrt,
  Pow,
  Exp,
  Log,
  Log10,
  Lerp,
  Sign,
  Reciprocal,
  Square,
  Cube,
  InRange,
  IsFinite,
  IsNan,
  Count
};

constexpr std::size_t numericOperationArity(NumericOperation operation) {
  switch (operation) {
  case NumericOperation::Abs:
  case NumericOperation::Floor:
  case NumericOperation::Ceil:
  case NumericOperation::Round:
  case NumericOperation::Trunc:
  case NumericOperation::Sqrt:
  case NumericOperation::Cbrt:
  case NumericOperation::Exp:
  case NumericOperation::Log:
  case NumericOperation::Log10:
  case NumericOperation::Sign:
  case NumericOperation::Reciprocal:
  case NumericOperation::Square:
  case NumericOperation::Cube:
  case NumericOperation::IsFinite:
  case NumericOperation::IsNan:
    return 1;
  case NumericOperation::Min:
  case NumericOperation::Max:
  case NumericOperation::Diff:
  case NumericOperation::Average:
  case NumericOperation::Pow:
    return 2;
  case NumericOperation::Clamp:
  case NumericOperation::Lerp:
  case NumericOperation::InRange:
    return 3;
  case NumericOperation::WeightedAverage:
    return 4;
  case NumericOperation::Count:
    break;
  }
  return 0;
}

inline std::optional<NumericOperation>
numericOperationForName(std::string_view name) {
  struct Entry {
    std::string_view name;
    NumericOperation operation;
  };
  static constexpr Entry entries[] = {
      {"min", NumericOperation::Min},
      {"max", NumericOperation::Max},
      {"abs", NumericOperation::Abs},
      {"diff", NumericOperation::Diff},
      {"avg", NumericOperation::Average},
      {"weighted_avg", NumericOperation::WeightedAverage},
      {"clamp", NumericOperation::Clamp},
      {"floor", NumericOperation::Floor},
      {"ceil", NumericOperation::Ceil},
      {"round", NumericOperation::Round},
      {"trunc", NumericOperation::Trunc},
      {"sqrt", NumericOperation::Sqrt},
      {"cbrt", NumericOperation::Cbrt},
      {"pow", NumericOperation::Pow},
      {"exp", NumericOperation::Exp},
      {"log", NumericOperation::Log},
      {"log10", NumericOperation::Log10},
      {"lerp", NumericOperation::Lerp},
      {"sign", NumericOperation::Sign},
      {"reciprocal", NumericOperation::Reciprocal},
      {"square", NumericOperation::Square},
      {"cube", NumericOperation::Cube},
      {"in_range", NumericOperation::InRange},
      {"is_finite", NumericOperation::IsFinite},
      {"is_nan", NumericOperation::IsNan},
  };
  for (const auto &entry : entries)
    if (entry.name == name)
      return entry.operation;
  return std::nullopt;
}

} // namespace Felidae
