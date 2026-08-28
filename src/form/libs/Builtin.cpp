#include "Builtin.h"

#include "Csv.h"
#include "Group.h"
#include "Json.h"
#include "Set.h"
#include "form/IrModule.h"

#include <cmath>
#include <stdexcept>

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

Json::Value toJson(const VmValue &value,
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
      result.push_back(toJson(item, symbolTable, codec));
    return result;
  }
  if (const auto map = std::get_if<VmTextMapPtr>(&value); map && *map) {
    auto result = Json::Value::object();
    for (const auto &[key, item] : (*map)->entries)
      result[decodeText(key, codec)] = toJson(item, symbolTable, codec);
    return result;
  }
  if (const auto map = std::get_if<VmMapPtr>(&value); map && *map) {
    auto result = Json::Value::object();
    for (const auto &[key, item] : (*map)->entries)
      result[decodeText(irSymbolPieces(symbolTable, key), codec)] =
          toJson(item, symbolTable, codec);
    return result;
  }
  if (const auto fact = std::get_if<VmFactPtr>(&value); fact && *fact) {
    auto result = Json::Value::object();
    result["__type"] =
        decodeText(irSymbolPieces(symbolTable, (*fact)->type), codec);
    for (const auto &[key, item] : (*fact)->fields)
      result[decodeText(irSymbolPieces(symbolTable, key), codec)] =
          toJson(item, symbolTable, codec);
    return result;
  }
  throw IrError("VM builtin cannot convert this value to JSON");
}

VmValue fromJson(const Json::Value &value, const BuiltinTextCodec &codec) {
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
      result->values.push_back(fromJson(item, codec));
    return result;
  }
  if (value.is_object()) {
    auto result = std::make_shared<VmTextMap>();
    result->entries.reserve(value.size());
    for (const auto &[key, item] : value.items())
      result->entries.emplace_back(encodeText(key, codec),
                                   fromJson(item, codec));
    return result;
  }
  throw IrError("JSON contains an unsupported value kind");
}

} // namespace

VmValue evaluateBuiltin(BuiltinId operation, std::span<const VmValue> inputs,
                        std::span<const PieceSequence> symbolTable,
                        const BuiltinTextCodec &codec) {
  const auto arity = builtinOperationArity(operation);
  if (!arity || inputs.size() != *arity)
    throw IrError("VM builtin operation has an invalid ID or arity");
  try {
    switch (operation) {
    case BuiltinId::JsonParse:
      return fromJson(Json::parse(textValue(inputs[0], codec, "json.parse")),
                      codec);
    case BuiltinId::JsonGet: {
      const auto data = toJson(inputs[0], symbolTable, codec);
      return fromJson(Json::get(data, textValue(inputs[1], codec, "json.get")),
                      codec);
    }
    case BuiltinId::JsonHas: {
      const auto data = toJson(inputs[0], symbolTable, codec);
      return Json::has(data, textValue(inputs[1], codec, "json.has")) ? 1.0
                                                                      : 0.0;
    }
    case BuiltinId::JsonKeys: {
      auto result = std::make_shared<VmArray>();
      for (const auto &key : Json::keys(toJson(inputs[0], symbolTable, codec)))
        result->values.emplace_back(VmText{encodeText(key, codec)});
      return result;
    }
    case BuiltinId::JsonSet:
      return fromJson(Json::set(toJson(inputs[0], symbolTable, codec),
                                textValue(inputs[1], codec, "json.set"),
                                toJson(inputs[2], symbolTable, codec)),
                      codec);
    case BuiltinId::JsonRemove:
      return fromJson(Json::remove(toJson(inputs[0], symbolTable, codec),
                                   textValue(inputs[1], codec, "json.remove")),
                      codec);
    case BuiltinId::JsonToText:
      return VmText{encodeText(
          Json::toText(toJson(inputs[0], symbolTable, codec)), codec)};
    case BuiltinId::CsvParse:
      return fromJson(Csv::parse(textValue(inputs[0], codec, "csv.parse")),
                      codec);
    case BuiltinId::CsvToFacts:
      return fromJson(Csv::toFacts(textValue(inputs[0], codec, "csv.toFacts"),
                                   textValue(inputs[1], codec, "csv.toFacts")),
                      codec);
    case BuiltinId::CsvToText:
      return VmText{encodeText(
          Csv::toText(toJson(inputs[0], symbolTable, codec)), codec)};
    case BuiltinId::CsvToFelidaeFacts:
      return VmText{
          encodeText(Csv::toFelidaeFacts(
                         toJson(inputs[0], symbolTable, codec),
                         textValue(inputs[1], codec, "csv.toFelidaeFacts")),
                     codec)};
    case BuiltinId::GroupClosed:
    case BuiltinId::GroupAssociative:
    case BuiltinId::GroupCommutative:
      return fromJson(
          Group::evaluate(operation, toJson(inputs[0], symbolTable, codec),
                          toJson(inputs[1], symbolTable, codec), std::nullopt),
          codec);
    case BuiltinId::GroupValidate:
    case BuiltinId::GroupIdentity:
    case BuiltinId::GroupInverse:
    case BuiltinId::GroupAbelian:
      return fromJson(Group::evaluate(operation,
                                      toJson(inputs[0], symbolTable, codec),
                                      toJson(inputs[1], symbolTable, codec),
                                      toJson(inputs[2], symbolTable, codec)),
                      codec);
    case BuiltinId::SetUnion:
    case BuiltinId::SetIntersection:
    case BuiltinId::SetDifference:
    case BuiltinId::SetSymmetricDifference:
    case BuiltinId::SetEquals:
    case BuiltinId::SetSubset:
    case BuiltinId::SetSuperset:
    case BuiltinId::SetDisjoint:
      return fromJson(
          Set::evaluate(operation, toJson(inputs[0], symbolTable, codec)),
          codec);
    case BuiltinId::SetCardinality:
      return fromJson(
          Set::evaluate(operation, Json::Value::array({toJson(
                                       inputs[0], symbolTable, codec)})),
          codec);
    case BuiltinId::SetIntersectionBy:
    case BuiltinId::SetDifferenceBy:
    case BuiltinId::SetSymmetricDifferenceBy:
    case BuiltinId::SetEqualsBy:
    case BuiltinId::SetSubsetBy:
    case BuiltinId::SetDisjointBy:
      return fromJson(
          Set::evaluate(operation, toJson(inputs[0], symbolTable, codec),
                        std::nullopt, toJson(inputs[1], symbolTable, codec)),
          codec);
    case BuiltinId::SetContains:
      return fromJson(Set::evaluate(operation,
                                    Json::Value::array({toJson(
                                        inputs[0], symbolTable, codec)}),
                                    toJson(inputs[1], symbolTable, codec)),
                      codec);
    case BuiltinId::SetContainsBy:
      return fromJson(Set::evaluate(operation,
                                    Json::Value::array({toJson(
                                        inputs[0], symbolTable, codec)}),
                                    toJson(inputs[1], symbolTable, codec),
                                    toJson(inputs[2], symbolTable, codec)),
                      codec);
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
