#include "Builtin.h"

#include "Csv.h"
#include "Group.h"
#include "Json.h"
#include "Set.h"
#include "form/IrModule.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
#include <random>
#include <stdexcept>
#include <vector>

namespace Felidae::Form {
namespace {

std::string decodeText(std::span<const PieceId> pieces,
                       const BuiltinTextCodec &codec) {
  if (!codec.decode)
    throw IrError("VM builtin text decoder service is absent");
  return codec.decode(pieces);
}

PieceSequence encodeText(std::string_view text, const BuiltinTextCodec &codec) {
  if (!codec.encode)
    throw IrError("VM builtin text encoder service is absent");
  return codec.encode(text);
}

std::string textValue(const VmValue &value, const BuiltinTextCodec &codec,
                      std::string_view operation) {
  const auto text = std::get_if<VmText>(&value);
  if (!text)
    throw IrError(std::string(operation) + " requires text input");
  return decodeText(text->pieces, codec);
}

double numberValue(const VmValue &value, std::string_view operation) {
  const auto number = std::get_if<double>(&value);
  if (!number || !std::isfinite(*number))
    throw IrError(std::string(operation) + " requires a finite number");
  return *number;
}

} // namespace

Json::Value vmValueToJson(const VmValue &value,
                          std::span<const PieceSequence> symbolTable,
                          const BuiltinTextCodec &codec) {
  if (std::holds_alternative<VmNil>(value))
    return nullptr;
  if (const auto number = std::get_if<double>(&value))
    return *number;
  if (const auto degree = std::get_if<VmDegree>(&value))
    return degree->value;
  if (const auto text = std::get_if<VmText>(&value))
    return decodeText(text->pieces, codec);
  if (const auto symbol = std::get_if<VmSymbol>(&value))
    return decodeText(irSymbolPieces(symbolTable, symbol->value), codec);
  if (const auto array = std::get_if<VmArrayPtr>(&value); array && *array) {
    auto result = Json::Value::array();
    for (const auto &item : (*array)->values)
      result.push_back(vmValueToJson(item, symbolTable, codec));
    return result;
  }
  if (const auto map = std::get_if<VmTextMapPtr>(&value); map && *map) {
    auto result = Json::Value::object();
    for (const auto &[key, item] : (*map)->entries)
      result[decodeText(key, codec)] =
          vmValueToJson(item, symbolTable, codec);
    return result;
  }
  if (const auto map = std::get_if<VmMapPtr>(&value); map && *map) {
    auto result = Json::Value::object();
    for (const auto &[key, item] : (*map)->entries)
      result[decodeText(irSymbolPieces(symbolTable, key), codec)] =
          vmValueToJson(item, symbolTable, codec);
    return result;
  }
  if (const auto fact = std::get_if<VmFactPtr>(&value); fact && *fact) {
    auto result = Json::Value::object();
    result["__type"] =
        decodeText(irSymbolPieces(symbolTable, (*fact)->type), codec);
    for (const auto &[key, item] : (*fact)->fields)
      result[decodeText(irSymbolPieces(symbolTable, key), codec)] =
          vmValueToJson(item, symbolTable, codec);
    return result;
  }
  throw IrError("VM builtin cannot convert this value to JSON");
}

VmValue jsonToVmValue(const Json::Value &value,
                      const BuiltinTextCodec &codec) {
  if (value.is_null())
    return VmNil{};
  if (value.is_boolean())
    return value.get<bool>() ? 1.0 : 0.0;
  if (value.is_number()) {
    const auto number = value.get<double>();
    if (!std::isfinite(number))
      throw IrError("JSON numeric value must be finite");
    return number;
  }
  if (value.is_string()) {
    return VmText{encodeText(value.get_ref<const std::string &>(), codec)};
  }
  if (value.is_array()) {
    auto result = std::make_shared<VmArray>();
    result->values.reserve(value.size());
    for (const auto &item : value)
      result->values.push_back(jsonToVmValue(item, codec));
    return result;
  }
  if (value.is_object()) {
    auto result = std::make_shared<VmTextMap>();
    result->entries.reserve(value.size());
    for (const auto &[key, item] : value.items())
      result->entries.emplace_back(encodeText(key, codec),
                                   jsonToVmValue(item, codec));
    return result;
  }
  throw IrError("JSON contains an unsupported value kind");
}

VmValue evaluateBuiltin(BuiltinId operation, std::span<const VmValue> inputs,
                        std::span<const PieceSequence> symbolTable,
                        const BuiltinTextCodec &codec) {
  const auto arity = builtinOperationArity(operation);
  if (!arity || inputs.size() != *arity)
    throw IrError("VM builtin operation has an invalid ID or arity");
  try {
    switch (operation) {
    case BuiltinId::ArrayGet: {
      const auto array = std::get_if<VmArrayPtr>(&inputs[0]);
      const auto position = std::get_if<double>(&inputs[1]);
      if (!array || !*array || !position || !std::isfinite(*position) ||
          *position < 0.0 || std::trunc(*position) != *position ||
          *position >= static_cast<double>((*array)->values.size())) {
        throw IrError("array.get requires a valid array position");
      }
      return (*array)->values[static_cast<std::size_t>(*position)];
    }
    case BuiltinId::Count:
    case BuiltinId::ArrayLen: {
      const auto array = std::get_if<VmArrayPtr>(&inputs[0]);
      if (!array || !*array)
        throw IrError("array length requires an array");
      return static_cast<double>((*array)->values.size());
    }
    case BuiltinId::ArrayLimit: {
      const auto array = std::get_if<VmArrayPtr>(&inputs[0]);
      const auto records = std::get_if<double>(&inputs[1]);
      if (!array || !*array)
        throw IrError("limit requires an array query result");
      if (!records || !std::isfinite(*records) || *records < 0.0 ||
          std::trunc(*records) != *records)
        throw IrError("limit records must be a finite non-negative integer");
      if (*records > static_cast<double>(std::numeric_limits<std::size_t>::max()))
        throw IrError("limit records exceeds the supported range");
      auto result = std::make_shared<VmArray>();
      const auto count = std::min((*array)->values.size(),
                                  static_cast<std::size_t>(*records));
      result->values.assign((*array)->values.begin(),
                            (*array)->values.begin() + count);
      return result;
    }
    case BuiltinId::Sum:
    case BuiltinId::Average:
    case BuiltinId::Min:
    case BuiltinId::Max: {
      const auto array = std::get_if<VmArrayPtr>(&inputs[0]);
      if (!array || !*array)
        throw IrError("numeric aggregation requires an array");
      if ((*array)->values.empty()) {
        if (operation == BuiltinId::Sum)
          return 0.0;
        throw IrError("numeric aggregation requires at least one value");
      }
      std::vector<double> values;
      values.reserve((*array)->values.size());
      for (const auto &item : (*array)->values) {
        const auto number = std::get_if<double>(&item);
        if (!number || !std::isfinite(*number))
          throw IrError("numeric aggregation requires finite numbers");
        values.push_back(*number);
      }
      if (operation == BuiltinId::Sum || operation == BuiltinId::Average) {
        const auto total = std::accumulate(values.begin(), values.end(), 0.0);
        return operation == BuiltinId::Sum
                   ? total
                   : total / static_cast<double>(values.size());
      }
      const auto bounds = std::minmax_element(values.begin(), values.end());
      return operation == BuiltinId::Min ? *bounds.first : *bounds.second;
    }
    case BuiltinId::Sort: {
      // Registered in BuiltinRegistry and documented in core/prelude.fx
      // ("sort(array)") since the start, but had no arity entry and no case
      // here -- every call compiled to "not yet lowered to IR". Numeric-only,
      // ascending, matching the sibling aggregations (sum/min/max) it sits
      // beside in both places.
      const auto array = std::get_if<VmArrayPtr>(&inputs[0]);
      if (!array || !*array)
        throw IrError("sort requires an array");
      std::vector<double> values;
      values.reserve((*array)->values.size());
      for (const auto &item : (*array)->values) {
        const auto number = std::get_if<double>(&item);
        if (!number || !std::isfinite(*number))
          throw IrError("sort requires an array of finite numbers");
        values.push_back(*number);
      }
      std::sort(values.begin(), values.end());
      auto result = std::make_shared<VmArray>();
      result->values.reserve(values.size());
      for (const auto value : values)
        result->values.emplace_back(value);
      return result;
    }
    case BuiltinId::JsonParse:
      return jsonToVmValue(
          Json::parse(textValue(inputs[0], codec, "json.parse")), codec);
    case BuiltinId::JsonGet: {
      const auto data = vmValueToJson(inputs[0], symbolTable, codec);
      return jsonToVmValue(
          Json::get(data, textValue(inputs[1], codec, "json.get")), codec);
    }
    case BuiltinId::JsonHas: {
      const auto data = vmValueToJson(inputs[0], symbolTable, codec);
      return Json::has(data, textValue(inputs[1], codec, "json.has")) ? 1.0
                                                                      : 0.0;
    }
    case BuiltinId::JsonKeys: {
      auto result = std::make_shared<VmArray>();
      for (const auto &key :
           Json::keys(vmValueToJson(inputs[0], symbolTable, codec)))
        result->values.emplace_back(VmText{encodeText(key, codec)});
      return result;
    }
    case BuiltinId::JsonSet:
      return jsonToVmValue(
          Json::set(vmValueToJson(inputs[0], symbolTable, codec),
                    textValue(inputs[1], codec, "json.set"),
                    vmValueToJson(inputs[2], symbolTable, codec)),
          codec);
    case BuiltinId::JsonRemove:
      return jsonToVmValue(
          Json::remove(vmValueToJson(inputs[0], symbolTable, codec),
                       textValue(inputs[1], codec, "json.remove")),
          codec);
    case BuiltinId::JsonToText:
      return VmText{encodeText(
          Json::toText(vmValueToJson(inputs[0], symbolTable, codec)), codec)};
    case BuiltinId::CsvParse:
      return jsonToVmValue(
          Csv::parse(textValue(inputs[0], codec, "csv.parse")), codec);
    case BuiltinId::CsvToFacts:
      throw IrError("csv.toFacts requires the VM fact service");
    case BuiltinId::CsvToText:
      return VmText{encodeText(
          Csv::toText(vmValueToJson(inputs[0], symbolTable, codec)), codec)};
    case BuiltinId::CsvToFelidaeFacts:
      return VmText{
          encodeText(Csv::toFelidaeFacts(
                         vmValueToJson(inputs[0], symbolTable, codec),
                         textValue(inputs[1], codec, "csv.toFelidaeFacts")),
                     codec)};
    case BuiltinId::GroupClosed:
    case BuiltinId::GroupAssociative:
    case BuiltinId::GroupCommutative:
      return jsonToVmValue(
          Group::evaluate(operation,
                          vmValueToJson(inputs[0], symbolTable, codec),
                          vmValueToJson(inputs[1], symbolTable, codec),
                          std::nullopt),
          codec);
    case BuiltinId::GroupValidate:
    case BuiltinId::GroupIdentity:
    case BuiltinId::GroupInverse:
    case BuiltinId::GroupAbelian:
      return jsonToVmValue(
          Group::evaluate(operation,
                          vmValueToJson(inputs[0], symbolTable, codec),
                          vmValueToJson(inputs[1], symbolTable, codec),
                          vmValueToJson(inputs[2], symbolTable, codec)),
          codec);
    case BuiltinId::SetUnion:
    case BuiltinId::SetIntersection:
    case BuiltinId::SetDifference:
    case BuiltinId::SetSymmetricDifference:
    case BuiltinId::SetEquals:
    case BuiltinId::SetSubset:
    case BuiltinId::SetSuperset:
    case BuiltinId::SetDisjoint:
      return jsonToVmValue(
          Set::evaluate(operation,
                        vmValueToJson(inputs[0], symbolTable, codec)),
          codec);
    case BuiltinId::SetCardinality:
      return jsonToVmValue(
          Set::evaluate(operation,
                        Json::Value::array({vmValueToJson(
                            inputs[0], symbolTable, codec)})),
          codec);
    case BuiltinId::SetIntersectionBy:
    case BuiltinId::SetDifferenceBy:
    case BuiltinId::SetSymmetricDifferenceBy:
    case BuiltinId::SetEqualsBy:
    case BuiltinId::SetSubsetBy:
    case BuiltinId::SetDisjointBy:
      return jsonToVmValue(
          Set::evaluate(operation,
                        vmValueToJson(inputs[0], symbolTable, codec),
                        std::nullopt,
                        vmValueToJson(inputs[1], symbolTable, codec)),
          codec);
    case BuiltinId::SetContains:
      return jsonToVmValue(
          Set::evaluate(operation,
                        Json::Value::array({vmValueToJson(
                            inputs[0], symbolTable, codec)}),
                        vmValueToJson(inputs[1], symbolTable, codec)),
          codec);
    case BuiltinId::SetContainsBy:
      return jsonToVmValue(
          Set::evaluate(operation,
                        Json::Value::array({vmValueToJson(
                            inputs[0], symbolTable, codec)}),
                        vmValueToJson(inputs[1], symbolTable, codec),
                        vmValueToJson(inputs[2], symbolTable, codec)),
          codec);
    // math.* were registered in BuiltinRegistry (and declared in
    // core/math.fx) with no arity entry and no case here at all -- every
    // call compiled to "not yet lowered to IR", the same dead-registration
    // pattern as the earlier sort/throw fixes this session.
    case BuiltinId::MathPi:
      return std::numbers::pi;
    case BuiltinId::MathE:
      return std::numbers::e;
    case BuiltinId::MathRandom: {
      const auto minimum = numberValue(inputs[0], "math.random");
      const auto maximum = numberValue(inputs[1], "math.random");
      if (minimum > maximum)
        throw IrError("math.random requires min <= max");
      thread_local std::mt19937 generator{std::random_device{}()};
      std::uniform_real_distribution<double> distribution(minimum, maximum);
      return distribution(generator);
    }
    case BuiltinId::MathPow:
      return std::pow(numberValue(inputs[0], "math.pow"),
                      numberValue(inputs[1], "math.pow"));
    case BuiltinId::MathAtan2:
      return std::atan2(numberValue(inputs[0], "math.atan2"),
                        numberValue(inputs[1], "math.atan2"));
    case BuiltinId::MathSqrt: {
      const auto value = numberValue(inputs[0], "math.sqrt");
      if (value < 0.0)
        throw IrError("math.sqrt requires a non-negative number");
      return std::sqrt(value);
    }
    case BuiltinId::MathSin:
      return std::sin(numberValue(inputs[0], "math.sin"));
    case BuiltinId::MathCos:
      return std::cos(numberValue(inputs[0], "math.cos"));
    case BuiltinId::MathTan:
      return std::tan(numberValue(inputs[0], "math.tan"));
    case BuiltinId::MathAsin:
      return std::asin(numberValue(inputs[0], "math.asin"));
    case BuiltinId::MathAcos:
      return std::acos(numberValue(inputs[0], "math.acos"));
    case BuiltinId::MathAtan:
      return std::atan(numberValue(inputs[0], "math.atan"));
    case BuiltinId::MathLog: {
      const auto value = numberValue(inputs[0], "math.log");
      if (value <= 0.0)
        throw IrError("math.log requires a positive number");
      return std::log(value);
    }
    case BuiltinId::MathLog10: {
      const auto value = numberValue(inputs[0], "math.log10");
      if (value <= 0.0)
        throw IrError("math.log10 requires a positive number");
      return std::log10(value);
    }
    case BuiltinId::MathExp:
      return std::exp(numberValue(inputs[0], "math.exp"));
    case BuiltinId::MathAbs:
      return std::fabs(numberValue(inputs[0], "math.abs"));
    case BuiltinId::MathFloor:
      return std::floor(numberValue(inputs[0], "math.floor"));
    case BuiltinId::MathCeil:
      return std::ceil(numberValue(inputs[0], "math.ceil"));
    case BuiltinId::MathRound:
      return std::round(numberValue(inputs[0], "math.round"));
    case BuiltinId::WhereGuardFailed:
      // Compiler-synthesized only: the implicit else branch of a
      // where-guarded clause with no explicit `else` (see
      // desugarWhereGuardedClauses in IrCodeGenerator.cpp). Reaching this
      // means every guard in the clause failed and there was nothing else
      // to fall back to -- a clear runtime failure, not a silent nil.
      throw IrError(
          "where guard failed: no matching clause and no else branch");
    default:
      break;
    }
  } catch (const IrError &) {
    throw;
  } catch (const std::exception &error) {
    throw IrError(error.what());
  }
  throw IrError("VM builtin operation is not implemented");
}

} // namespace Felidae::Form
