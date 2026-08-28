#include "form/IrModule.h"
#include "form/LibTorchTensorRuntime.h"
#include "form/TensorOperation.h"

#include <torch/torch.h>

#include <array>
#include <cassert>
#include <cmath>
#include <functional>
#include <limits>

struct RegisteredNetwork final : torch::nn::Module {
  RegisteredNetwork() {
    embedding = register_module("embedding", torch::nn::Embedding(8, 4));
    encoder = register_module(
        "encoder", torch::nn::GRU(torch::nn::GRUOptions(4, 4).num_layers(1)));
  }
  torch::nn::Embedding embedding{nullptr};
  torch::nn::GRU encoder{nullptr};
};

namespace {

Felidae::VmValue array(std::initializer_list<Felidae::VmValue> values) {
  auto result = std::make_shared<Felidae::VmArray>();
  result->values.assign(values);
  return result;
}

double number(const Felidae::VmValue &value) { return std::get<double>(value); }

const Felidae::VmArray &values(const Felidae::VmValue &value) {
  return **std::get_if<Felidae::VmArrayPtr>(&value);
}

Felidae::VmValue materialized(const Felidae::LibTorchTensorRuntime &runtime,
                              const Felidae::VmValue &value) {
  if (const auto tensor = std::get_if<Felidae::VmTensorPtr>(&value))
    return runtime.materializeTensor(**tensor);
  return value;
}

bool rejects(const std::function<void()> &action) {
  try {
    action();
    return false;
  } catch (const Felidae::IrError &) {
    return true;
  }
}

} // namespace

int main() {
  const auto value =
      torch::ones({1}, torch::TensorOptions().dtype(torch::kFloat));
  if (value.item<float>() != 1.0f)
    return 1;
  auto embedding = torch::nn::Embedding(torch::nn::EmbeddingOptions(8, 4));
  auto gru = torch::nn::GRU(torch::nn::GRUOptions(4, 4).num_layers(1));
  const auto ids = torch::tensor(std::vector<std::int64_t>{1},
                                 torch::TensorOptions().dtype(torch::kInt64))
                       .reshape({1, 1});
  const auto output = gru->forward(embedding->forward(ids));
  if (std::get<0>(output).numel() != 4)
    return 1;
  auto network = std::make_shared<RegisteredNetwork>();
  network->eval();
  const auto registeredOutput =
      network->encoder->forward(network->embedding->forward(ids));
  if (std::get<0>(registeredOutput).numel() != 4)
    return 1;

  Felidae::LibTorchTensorRuntime tensors;
  const std::vector<Felidae::PieceSequence> noSymbols;
  const auto vectorA = array({1.0, 2.0, 3.0});
  const auto vectorB = array({4.0, 5.0, 6.0});
  assert(number(tensors.evaluateTensor(Felidae::TensorOperation::Dot,
                                       std::array{vectorA, vectorB},
                                       noSymbols)) == 32.0);
  assert(number(tensors.evaluateTensor(Felidae::TensorOperation::Size,
                                       std::array{vectorA}, noSymbols)) == 3.0);
  const auto matrix = array({array({1.0, 2.0}), array({2.0, 1.0})});
  const auto shape = tensors.evaluateTensor(Felidae::TensorOperation::Shape,
                                            std::array{matrix}, noSymbols);
  assert(values(shape).values.size() == 2);
  assert(number(values(shape).values[0]) == 2.0);
  assert(number(values(shape).values[1]) == 2.0);
  assert(number(tensors.evaluateTensor(Felidae::TensorOperation::Dimensions,
                                       std::array{matrix}, noSymbols)) == 2.0);
  assert(number(tensors.evaluateTensor(Felidae::TensorOperation::IsSymmetric,
                                       std::array{matrix}, noSymbols)) == 1.0);
  const auto asymmetric = array({array({1.0, 2.0}), array({3.0, 4.0})});
  assert(number(tensors.evaluateTensor(Felidae::TensorOperation::IsSymmetric,
                                       std::array{asymmetric}, noSymbols)) ==
         0.0);
  const auto transpose = tensors.evaluateTensor(
      Felidae::TensorOperation::Transpose, std::array{asymmetric}, noSymbols);
  assert(std::holds_alternative<Felidae::VmTensorPtr>(transpose));
  const auto transposeValues = materialized(tensors, transpose);
  assert(number(values(values(transposeValues).values[0]).values[1]) == 3.0);
  const auto copy = tensors.evaluateTensor(Felidae::TensorOperation::Clone,
                                           std::array{matrix}, noSymbols);
  assert(std::holds_alternative<Felidae::VmTensorPtr>(copy));
  const auto copyAgain = tensors.evaluateTensor(Felidae::TensorOperation::Clone,
                                                std::array{copy}, noSymbols);
  assert(std::get<Felidae::VmTensorPtr>(copyAgain)->storage !=
         std::get<Felidae::VmTensorPtr>(copy)->storage);
  const auto copyValues = materialized(tensors, copy);
  assert(number(values(values(copyValues).values[1]).values[0]) == 2.0);
  const auto difference = tensors.evaluateTensor(
      Felidae::TensorOperation::Difference,
      std::array{array({1.0, 4.0}), array({3.0, 1.0})}, noSymbols);
  const auto differenceValues = materialized(tensors, difference);
  assert(number(values(differenceValues).values[0]) == 2.0);
  assert(number(values(differenceValues).values[1]) == 3.0);
  assert(number(tensors.evaluateTensor(
             Felidae::TensorOperation::MeanSquaredError,
             std::array{array({1.0, 2.0}), array({1.0, 4.0})}, noSymbols)) ==
         2.0);
  const auto cosine = number(tensors.evaluateTensor(
      Felidae::TensorOperation::CosineSimilarity,
      std::array{array({1.0, 0.0}), array({1.0, 1.0})}, noSymbols));
  assert(std::abs(cosine - std::sqrt(0.5)) < 1e-12);
  const auto sigmoid = tensors.evaluateTensor(
      Felidae::TensorOperation::Sigmoid, std::array{array({0.0})}, noSymbols);
  assert(number(values(materialized(tensors, sigmoid)).values[0]) == 0.5);
  const auto relu =
      tensors.evaluateTensor(Felidae::TensorOperation::Relu,
                             std::array{array({-2.0, 3.0})}, noSymbols);
  const auto reluValues = materialized(tensors, relu);
  assert(number(values(reluValues).values[0]) == 0.0);
  assert(number(values(reluValues).values[1]) == 3.0);
  assert(rejects([&] {
    const auto zero = array({0.0, 0.0});
    (void)tensors.evaluateTensor(Felidae::TensorOperation::CosineSimilarity,
                                 std::array{zero, zero}, noSymbols);
  }));
  assert(rejects([&] {
    (void)tensors.evaluateTensor(Felidae::TensorOperation::Transpose,
                                 std::array{vectorA}, noSymbols);
  }));
  assert(rejects([&] {
    const auto infinity = array({std::numeric_limits<double>::infinity()});
    (void)tensors.evaluateTensor(Felidae::TensorOperation::Clone,
                                 std::array{infinity}, noSymbols);
  }));
  assert(rejects([&] {
    const auto largest = std::numeric_limits<double>::max();
    (void)tensors.evaluateTensor(
        Felidae::TensorOperation::MeanSquaredError,
        std::array{array({largest}), array({-largest})}, noSymbols);
  }));
  assert(rejects([&] {
    const auto ragged = array({array({1.0}), array({2.0, 3.0})});
    (void)tensors.evaluateTensor(Felidae::TensorOperation::Shape,
                                 std::array{ragged}, noSymbols);
  }));

  std::vector<Felidae::PieceSequence> symbols{{11, 12}, {21}, {22}, {23, 24}};
  auto fact = std::make_shared<Felidae::VmFact>();
  fact->type = 1;
  fact->fields.emplace_back(2, Felidae::VmText{{31, 32}});
  fact->fields.emplace_back(3, 2.5);
  const Felidae::VmValue factValue = fact;
  assert(number(tensors.evaluateTensor(Felidae::TensorOperation::Size,
                                       std::array{factValue}, symbols)) == 7.0);
  const auto identitySimilarity =
      number(tensors.evaluateTensor(Felidae::TensorOperation::CosineSimilarity,
                                    std::array{factValue, factValue}, symbols));
  assert(std::abs(identitySimilarity - 1.0) < 1e-12);
  auto changedFact = std::make_shared<Felidae::VmFact>(*fact);
  changedFact->fields.back().second = 4.0;
  const auto factDifference = tensors.evaluateTensor(
      Felidae::TensorOperation::Difference,
      std::array<Felidae::VmValue, 2>{factValue, changedFact}, symbols);
  assert(number(values(materialized(tensors, factDifference)).values.back()) ==
         1.5);
  return 0;
}
