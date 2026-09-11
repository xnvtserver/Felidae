#pragma once

#include <cstddef>
#include <memory>
#include <span>

namespace Felidae::Form {

// Owns real numeric hidden-state vectors, never facts or SentencePiece IDs.
// One instance belongs to one execution; there is no serialization or global
// cache. Destroying it releases every retained vector. No training is needed.
class ExecutionVectorMemory {
public:
  ExecutionVectorMemory(std::size_t dimensions, std::size_t capacity);
  ~ExecutionVectorMemory();
  ExecutionVectorMemory(const ExecutionVectorMemory &) = delete;
  ExecutionVectorMemory &operator=(const ExecutionVectorMemory &) = delete;
  void append(std::span<const float> state);
  std::size_t size() const;

private:
  class Implementation;
  std::unique_ptr<Implementation> implementation_;
};

} // namespace Felidae::Form
