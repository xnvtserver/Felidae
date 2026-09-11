#include "ExecutionVectorMemory.h"

#include "form/IrModule.h"
#include <faiss/IndexFlat.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Felidae::Form {

class ExecutionVectorMemory::Implementation {
public:
  Implementation(std::size_t dimensions, std::size_t maximum)
      : index(static_cast<faiss::idx_t>(dimensions)), capacity(maximum) {}
  faiss::IndexFlatL2 index;
  std::size_t capacity;
};

ExecutionVectorMemory::ExecutionVectorMemory(std::size_t dimensions,
                                           std::size_t capacity) {
  if (dimensions == 0 || dimensions > static_cast<std::size_t>(
          std::numeric_limits<int>::max()) || capacity == 0 ||
      capacity > std::numeric_limits<std::size_t>::max() / sizeof(float) / dimensions)
    throw IrError("invalid execution vector memory dimensions or capacity");
  implementation_ = std::make_unique<Implementation>(dimensions, capacity);
}

ExecutionVectorMemory::~ExecutionVectorMemory() = default;

void ExecutionVectorMemory::append(std::span<const float> state) {
  auto &memory = *implementation_;
  if (state.size() != static_cast<std::size_t>(memory.index.d) ||
      !std::all_of(state.begin(), state.end(), [](float v) { return std::isfinite(v); }))
    throw IrError("execution hidden state has invalid dimensions or non-finite values");
  if (size() == memory.capacity)
    throw IrError("execution vector memory reached its semantic-step capacity");
  memory.index.add(1, state.data());
}

std::size_t ExecutionVectorMemory::size() const {
  return static_cast<std::size_t>(implementation_->index.ntotal);
}

} // namespace Felidae::Form
