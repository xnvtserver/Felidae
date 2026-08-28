#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace Felidae {

// Operations executed by the optional platform tensor backend. Supported
// desktop builds provide the backend through LibTorch; RegisterVm retains one
// instruction format. Tensor operations accept numeric scalars, rectangular
// numeric arrays, or an existing tensor; symbolic facts retain fact semantics.
enum class TensorOperation : std::uint8_t {
  Size = 0,
  Shape,
  Dimensions,
  Sigmoid,
  Relu,
  Dot,
  MeanSquaredError,
  Difference,
  CosineSimilarity,
  Transpose,
  IsSymmetric,
  Clone,
  Count
};

constexpr std::size_t tensorOperationArity(TensorOperation operation) {
  switch (operation) {
  case TensorOperation::Size:
  case TensorOperation::Shape:
  case TensorOperation::Dimensions:
  case TensorOperation::Sigmoid:
  case TensorOperation::Relu:
  case TensorOperation::Transpose:
  case TensorOperation::IsSymmetric:
  case TensorOperation::Clone:
    return 1;
  case TensorOperation::Dot:
  case TensorOperation::MeanSquaredError:
  case TensorOperation::Difference:
  case TensorOperation::CosineSimilarity:
    return 2;
  case TensorOperation::Count:
    return 0;
  }
  return 0;
}

inline std::optional<std::size_t>
tensorOperationArgumentIndex(TensorOperation operation, std::string_view name) {
  if (operation == TensorOperation::Dot ||
      operation == TensorOperation::MeanSquaredError ||
      operation == TensorOperation::Difference ||
      operation == TensorOperation::CosineSimilarity) {
    if (name == "left")
      return 0;
    if (name == "right")
      return 1;
    return std::nullopt;
  }
  return name == "value" ? std::optional<std::size_t>{0} : std::nullopt;
}

inline std::optional<TensorOperation>
tensorOperationForName(std::string_view name) {
  if (name == "tensor:size")
    return TensorOperation::Size;
  if (name == "tensor:shape")
    return TensorOperation::Shape;
  if (name == "tensor:dimensions")
    return TensorOperation::Dimensions;
  if (name == "ml:sigmoid")
    return TensorOperation::Sigmoid;
  if (name == "ml:relu")
    return TensorOperation::Relu;
  if (name == "ml:dot")
    return TensorOperation::Dot;
  if (name == "ml:meanSquaredError")
    return TensorOperation::MeanSquaredError;
  if (name == "tensor:difference")
    return TensorOperation::Difference;
  if (name == "tensor:cosineSimilarity")
    return TensorOperation::CosineSimilarity;
  if (name == "tensor:transpose")
    return TensorOperation::Transpose;
  if (name == "tensor:isSymmetric")
    return TensorOperation::IsSymmetric;
  if (name == "tensor:clone")
    return TensorOperation::Clone;
  return std::nullopt;
}

// Returns each source argument's canonical operand position. Empty names mean
// a positional call; named and positional arguments cannot be mixed. This is
// the single argument-shape contract shared by eligibility checks and IR
// emission.
inline std::optional<std::vector<std::size_t>>
tensorOperationArgumentOrder(TensorOperation operation,
                             std::span<const std::string_view> names) {
  const auto arity = tensorOperationArity(operation);
  if (names.size() != arity)
    return std::nullopt;
  const bool named = !names.empty() && !names.front().empty();
  std::vector<std::size_t> order;
  order.reserve(arity);
  std::vector<bool> seen(arity);
  for (std::size_t index = 0; index < arity; ++index) {
    if (named != !names[index].empty())
      return std::nullopt;
    const auto target =
        named ? tensorOperationArgumentIndex(operation, names[index])
              : std::optional<std::size_t>{index};
    if (!target || *target >= arity || seen[*target])
      return std::nullopt;
    seen[*target] = true;
    order.push_back(*target);
  }
  return order;
}

} // namespace Felidae
