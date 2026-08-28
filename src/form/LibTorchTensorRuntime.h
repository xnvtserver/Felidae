#pragma once

#include "RegisterVm.h"

namespace Felidae {

// LibTorch implementation of RegisterVm's tensor boundary. The core VM keeps
// Torch headers out of its public value representation and remains buildable
// on explicitly unsupported targets.
class LibTorchTensorRuntime final : public TensorRuntime {
public:
  Value evaluateTensor(TensorOperation operation, std::span<const Value> inputs,
                       std::span<const PieceSequence> symbolTable) override;
  Value materializeTensor(const VmTensor &tensor) const;
  std::string displayTensor(const VmTensor &tensor) const;
};

} // namespace Felidae
