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
      {"MIN", NumericOperation::Min},
      {"MAX", NumericOperation::Max},
      {"ABS", NumericOperation::Abs},
      {"DIFF", NumericOperation::Diff},
      {"AVG", NumericOperation::Average},
      {"WEIGHTED_AVG", NumericOperation::WeightedAverage},
      {"CLAMP", NumericOperation::Clamp},
      {"FLOOR", NumericOperation::Floor},
      {"CEIL", NumericOperation::Ceil},
      {"ROUND", NumericOperation::Round},
      {"TRUNC", NumericOperation::Trunc},
      {"SQRT", NumericOperation::Sqrt},
      {"CBRT", NumericOperation::Cbrt},
      {"POW", NumericOperation::Pow},
      {"EXP", NumericOperation::Exp},
      {"LOG", NumericOperation::Log},
      {"LOG10", NumericOperation::Log10},
      {"LERP", NumericOperation::Lerp},
      {"SIGN", NumericOperation::Sign},
      {"RECIPROCAL", NumericOperation::Reciprocal},
      {"SQUARE", NumericOperation::Square},
      {"CUBE", NumericOperation::Cube},
      {"IN_RANGE", NumericOperation::InRange},
      {"IS_FINITE", NumericOperation::IsFinite},
      {"IS_NAN", NumericOperation::IsNan},
  };
  for (const auto &entry : entries)
    if (entry.name == name)
      return entry.operation;
  return std::nullopt;
}

} // namespace Felidae
