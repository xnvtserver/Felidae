#pragma once

// Compiler-side compatibility include. The AST-free register VM API lives in
// the form subsystem.
#include <cstddef>
namespace Felidae { using SentencePieceId = std::size_t; }
#include "form/RegisterVm.h"
