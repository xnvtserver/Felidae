#include "LibTorchTensorRuntime.h"

#include "IrModule.h"
#include "TensorOperation.h"

#include <torch/torch.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace Felidae {
namespace {

void appendFactFeatures(const VmValue &value,
                        std::span<const PieceSequence> symbols,
                        std::vector<double> &result) {
  const auto append = [&](std::span<const PieceId> pieces) {
    for (const auto piece : pieces)
      result.push_back(static_cast<double>(piece));
  };
  if (const auto number = std::get_if<double>(&value)) {
    if (!std::isfinite(*number))
      throw IrError("tensor numeric value must be finite");
    result.push_back(*number);
    return;
  }
  if (const auto degree = std::get_if<VmDegree>(&value)) {
    result.push_back(degree->value);
    return;
  }
  if (const auto tensor = std::get_if<VmTensorPtr>(&value)) {
    if (!*tensor || !(*tensor)->storage)
      throw IrError("fact tensor field has no LibTorch storage");
    const auto flat = (*tensor)
                          ->storage->to(torch::kCPU)
                          .to(torch::kFloat64)
                          .contiguous()
                          .reshape({-1});
    if (!flat.isfinite().all().item<bool>())
      throw IrError("fact tensor field must be finite");
    const auto *data = flat.const_data_ptr<double>();
    result.insert(result.end(), data, data + flat.numel());
    return;
  }
  if (const auto text = std::get_if<VmText>(&value)) {
    append(text->pieces);
    return;
  }
  if (const auto symbol = std::get_if<VmSymbol>(&value)) {
    append(irSymbolPieces(symbols, symbol->value));
    return;
  }
  if (const auto fact = std::get_if<VmFactPtr>(&value); fact && *fact) {
    append(irSymbolPieces(symbols, (*fact)->type));
    for (const auto &[field, fieldValue] : (*fact)->fields) {
      append(irSymbolPieces(symbols, field));
      appendFactFeatures(fieldValue, symbols, result);
    }
    return;
  }
  if (const auto array = std::get_if<VmArrayPtr>(&value); array && *array) {
    for (const auto &item : (*array)->values)
      appendFactFeatures(item, symbols, result);
    return;
  }
  if (const auto map = std::get_if<VmMapPtr>(&value); map && *map) {
    for (const auto &[field, fieldValue] : (*map)->entries) {
      append(irSymbolPieces(symbols, field));
      appendFactFeatures(fieldValue, symbols, result);
    }
    return;
  }
  if (const auto map = std::get_if<VmTextMapPtr>(&value); map && *map) {
    for (const auto &[field, fieldValue] : (*map)->entries) {
      append(field);
      appendFactFeatures(fieldValue, symbols, result);
    }
  }
}

torch::Tensor tensorFromValue(const VmValue &value,
                              std::span<const PieceSequence> symbols) {
  if (const auto tensor = std::get_if<VmTensorPtr>(&value)) {
    if (!*tensor || !(*tensor)->storage)
      throw IrError("tensor value has no LibTorch storage");
    return *(*tensor)->storage;
  }
  if (const auto number = std::get_if<double>(&value)) {
    if (!std::isfinite(*number))
      throw IrError("tensor numeric value must be finite");
    return torch::tensor(*number,
                         torch::TensorOptions().dtype(torch::kFloat64));
  }
  if (const auto degree = std::get_if<VmDegree>(&value))
    return torch::tensor(degree->value,
                         torch::TensorOptions().dtype(torch::kFloat64));
  if (const auto array = std::get_if<VmArrayPtr>(&value); array && *array) {
    if ((*array)->values.empty())
      throw IrError("tensor array must not be empty");
    std::vector<torch::Tensor> rows;
    rows.reserve((*array)->values.size());
    for (const auto &item : (*array)->values)
      rows.push_back(tensorFromValue(item, symbols));
    const auto shape = rows.front().sizes().vec();
    for (const auto &row : rows)
      if (row.sizes().vec() != shape)
        throw IrError("tensor array must be rectangular");
    return torch::stack(rows);
  }

  std::vector<double> features;
  appendFactFeatures(value, symbols, features);
  if (features.empty())
    throw IrError(
        "tensor value has neither numeric data nor SentencePiece IDs");
  return torch::tensor(features, torch::TensorOptions().dtype(torch::kFloat64));
}

VmValue arrayFromTensor(const torch::Tensor &source) {
  const auto tensor = source.to(torch::kCPU).to(torch::kFloat64).contiguous();
  if (!tensor.isfinite().all().item<bool>())
    throw IrError("tensor operation produced a non-finite result");
  if (tensor.dim() == 0)
    return tensor.item<double>();
  auto result = std::make_shared<VmArray>();
  result->values.reserve(static_cast<std::size_t>(tensor.size(0)));
  for (std::int64_t index = 0; index < tensor.size(0); ++index)
    result->values.push_back(arrayFromTensor(tensor[index]));
  return result;
}

VmValue tensorValue(const torch::Tensor &source) {
  auto tensor = source.to(torch::kCPU).to(torch::kFloat64).contiguous();
  if (!tensor.isfinite().all().item<bool>())
    throw IrError("tensor operation produced a non-finite result");
  auto result = std::make_shared<VmTensor>();
  result->shape = tensor.sizes().vec();
  result->storage = std::make_shared<torch::Tensor>(std::move(tensor));
  return result;
}

VmValue shapeValue(const torch::Tensor &tensor) {
  auto shape = std::make_shared<VmArray>();
  shape->values.reserve(static_cast<std::size_t>(tensor.dim()));
  for (const auto dimension : tensor.sizes())
    shape->values.emplace_back(static_cast<double>(dimension));
  return shape;
}

void requireEqualShapes(const torch::Tensor &left, const torch::Tensor &right,
                        std::string_view operation) {
  if (left.sizes().vec() != right.sizes().vec())
    throw IrError(std::string(operation) + " requires equal tensor shapes");
}

} // namespace

Value LibTorchTensorRuntime::evaluateTensor(
    TensorOperation operation, std::span<const Value> inputs,
    std::span<const PieceSequence> symbolTable) {
  if (operation >= TensorOperation::Count ||
      inputs.size() != tensorOperationArity(operation))
    throw IrError("tensor operation has an invalid operation ID or arity");

  torch::InferenceMode guard;
  const auto left = tensorFromValue(inputs[0], symbolTable);
  switch (operation) {
  case TensorOperation::Size:
    return static_cast<double>(left.numel());
  case TensorOperation::Shape:
    return shapeValue(left);
  case TensorOperation::Dimensions:
    return static_cast<double>(left.dim());
  case TensorOperation::Sigmoid:
    return tensorValue(torch::sigmoid(left));
  case TensorOperation::Relu:
    return tensorValue(torch::relu(left));
  case TensorOperation::Transpose:
    if (left.dim() != 2)
      throw IrError("tensor.transpose requires a rank-2 tensor");
    return tensorValue(left.transpose(0, 1));
  case TensorOperation::IsSymmetric:
    if (left.dim() != 2 || left.size(0) != left.size(1))
      return 0.0;
    return left.equal(left.transpose(0, 1)) ? 1.0 : 0.0;
  case TensorOperation::Clone:
    return tensorValue(left.clone());
  case TensorOperation::Dot: {
    const auto right = tensorFromValue(inputs[1], symbolTable);
    if (left.dim() != 1 || right.dim() != 1 ||
        left.sizes().vec() != right.sizes().vec())
      throw IrError("ml.dot requires equal-length rank-1 tensors");
    const auto result = torch::dot(left, right).item<double>();
    if (!std::isfinite(result))
      throw IrError("ml.dot produced a non-finite result");
    return result;
  }
  case TensorOperation::MeanSquaredError: {
    const auto right = tensorFromValue(inputs[1], symbolTable);
    requireEqualShapes(left, right, "ml.meanSquaredError");
    const auto result = torch::mse_loss(left, right).item<double>();
    if (!std::isfinite(result))
      throw IrError("ml.meanSquaredError produced a non-finite result");
    return result;
  }
  case TensorOperation::Difference: {
    const auto right = tensorFromValue(inputs[1], symbolTable);
    requireEqualShapes(left, right, "tensor.difference");
    return tensorValue(torch::abs(left - right));
  }
  case TensorOperation::CosineSimilarity: {
    const auto right = tensorFromValue(inputs[1], symbolTable);
    if (left.dim() != 1 || right.dim() != 1)
      throw IrError("tensor.cosineSimilarity requires rank-1 tensors");
    requireEqualShapes(left, right, "tensor.cosineSimilarity");
    const auto denominator =
        left.norm().item<double>() * right.norm().item<double>();
    if (denominator == 0.0)
      throw IrError("tensor.cosineSimilarity requires non-zero tensors");
    const auto result = torch::dot(left, right).item<double>() / denominator;
    if (!std::isfinite(result))
      throw IrError("tensor.cosineSimilarity produced a non-finite result");
    return result;
  }
  case TensorOperation::Count:
    break;
  }
  throw IrError("tensor operation is invalid");
}

Value LibTorchTensorRuntime::materializeTensor(const VmTensor &tensor) const {
  if (!tensor.storage)
    throw IrError("tensor value has no LibTorch storage");
  torch::InferenceMode guard;
  return arrayFromTensor(*tensor.storage);
}

std::string LibTorchTensorRuntime::displayTensor(const VmTensor &tensor) const {
  return vmValueToDisplayString(materializeTensor(tensor));
}

} // namespace Felidae
