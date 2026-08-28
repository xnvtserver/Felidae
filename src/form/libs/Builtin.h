#pragma once

#include "form/BuiltinOperation.h"
#include "form/RegisterVm.h"

namespace Felidae::Form {

struct BuiltinTextCodec {
  VmTextDecoder decode;
  std::function<PieceSequence(std::string_view)> encode;
};

VmValue evaluateBuiltin(BuiltinId operation, std::span<const VmValue> inputs,
                        std::span<const PieceSequence> symbolTable,
                        const BuiltinTextCodec &text);

} // namespace Felidae::Form
