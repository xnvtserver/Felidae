#pragma once

// Runtime semantics remain centralized in the established implementation while
// execution moves behind RegisterVm. New frontend/VM code must use VmRuntime,
// not the legacy Interpreter spelling.
#include "Interpreter.h"

namespace Felidae {
using VmRuntime = Interpreter;
} // namespace Felidae
