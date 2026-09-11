#include "form/libs/ExecutionVectorMemory.h"
#include "form/IrModule.h"

#include <array>
#include <cassert>
#include <limits>

int main() {
  using Felidae::Form::ExecutionVectorMemory;
  const auto rejects = [](auto action) {
    try { action(); } catch (const Felidae::IrError &) { return true; }
    return false;
  };
  assert(rejects([] { ExecutionVectorMemory invalid(0, 2); }));
  assert(rejects([] { ExecutionVectorMemory invalid(2, 0); }));
  {
    ExecutionVectorMemory first(2, 2), second(2, 2);
    std::array<float, 2> state{-212.421f, 3.5f};
    first.append(state);
    assert(first.size() == 1 && second.size() == 0);
    assert(rejects([&] { first.append(std::span(state).first(1)); }));
    for (const float invalid : {std::numeric_limits<float>::infinity(),
                                std::numeric_limits<float>::quiet_NaN()}) {
      state[0] = invalid;
      assert(rejects([&] { first.append(state); }));
      assert(first.size() == 1);
    }
    state = {0.0f, -0.25f};
    first.append(state);
    assert(first.size() == 2);
    assert(rejects([&] { first.append(state); }));
  }
  ExecutionVectorMemory nextExecution(2, 2);
  assert(nextExecution.size() == 0);
}
