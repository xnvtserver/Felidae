#pragma once

#include "form/BuiltinOperation.h"
#include "form/RegisterVm.h"
#include "Json.h"

namespace Felidae::Form {

struct BuiltinTextCodec {
  VmTextDecoder decode;
  std::function<PieceSequence(std::string_view)> encode;
};

Json::Value vmValueToJson(const VmValue &value,
                          std::span<const PieceSequence> symbolTable,
                          const BuiltinTextCodec &text);
VmValue jsonToVmValue(const Json::Value &value,
                      const BuiltinTextCodec &text);

VmValue evaluateBuiltin(BuiltinId operation, std::span<const VmValue> inputs,
                        std::span<const PieceSequence> symbolTable,
                        const BuiltinTextCodec &text);

} // namespace Felidae::Form
