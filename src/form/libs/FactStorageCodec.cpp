#include "form/libs/FactStorageCodec.h"

#include "form/IrModule.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <type_traits>
#include <limits>

namespace Felidae::Form {
namespace {

using Json = nlohmann::ordered_json;
constexpr std::size_t kMaximumStorageDepth = 64;
constexpr std::size_t kMaximumStorageItems = 1'000'000;
constexpr std::uint64_t kFactStorageVersion = 1;
constexpr std::uint64_t kFactTypeStorageVersion = 1;

void appendValueKey(const VmValue &value,
                    std::span<const PieceSequence> symbols,
                    StoredValueKey &key, std::size_t depth = 0) {
  if (depth > kMaximumStorageDepth)
    throw IrError("fact index key exceeds its nesting limit");
  key.push_back(static_cast<std::uint64_t>(runtimeValueKind(value)));
  const auto appendSize = [&](std::size_t size) {
    key.push_back(static_cast<std::uint64_t>(size));
  };
  const auto appendPieces = [&](std::span<const PieceId> value) {
    appendSize(value.size());
    for (const auto piece : value)
      key.push_back(static_cast<std::uint64_t>(piece));
  };
  const auto appendEntries = [&](const auto &entries) {
    std::vector<std::size_t> order(entries.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](std::size_t left,
                                              std::size_t right) {
      // Field order must survive a different symbol-interning order after
      // reopening storage. Only SentencePiece sequences define that order.
      if constexpr (std::is_same_v<
                        std::remove_cvref_t<decltype(entries[left].first)>,
                        PieceSequence>) {
        return entries[left].first < entries[right].first;
      } else {
        const auto a = irSymbolPieces(symbols, entries[left].first);
        const auto b = irSymbolPieces(symbols, entries[right].first);
        return std::lexicographical_compare(a.begin(), a.end(),
                                             b.begin(), b.end());
      }
    });
    appendSize(order.size());
    for (const auto index : order) {
      const auto &[field, item] = entries[index];
      if constexpr (std::is_same_v<std::remove_cvref_t<decltype(field)>,
                                   PieceSequence>)
        appendPieces(field);
      else
        appendPieces(irSymbolPieces(symbols, field));
      appendValueKey(item, symbols, key, depth + 1);
    }
  };
  if (std::holds_alternative<VmNil>(value))
    return;
  if (const auto number = std::get_if<double>(&value)) {
    if (!std::isfinite(*number))
      throw IrError("fact index key must be finite");
    key.push_back(std::bit_cast<std::uint64_t>(*number == 0.0 ? 0.0 : *number));
    return;
  }
  if (const auto degree = std::get_if<VmDegree>(&value)) {
    key.push_back(std::bit_cast<std::uint64_t>(degree->value));
    return;
  }
  if (const auto text = std::get_if<VmText>(&value)) {
    appendPieces(text->pieces);
    return;
  }
  if (const auto symbol = std::get_if<VmSymbol>(&value)) {
    appendPieces(irSymbolPieces(symbols, symbol->value));
    return;
  }
  if (const auto array = std::get_if<VmArrayPtr>(&value)) {
    if (!*array)
      throw IrError("fact index key contains a null array");
    appendSize((*array)->values.size());
    for (const auto &item : (*array)->values)
      appendValueKey(item, symbols, key, depth + 1);
    return;
  }
  if (const auto map = std::get_if<VmMapPtr>(&value)) {
    if (!*map)
      throw IrError("fact index key contains a null map");
    appendEntries((*map)->entries);
    return;
  }
  if (const auto fact = std::get_if<VmFactPtr>(&value)) {
    if (!*fact)
      throw IrError("fact index key contains a null fact");
    appendPieces(irSymbolPieces(symbols, (*fact)->type));
    appendEntries((*fact)->fields);
    return;
  }
  if (std::holds_alternative<VmTensorPtr>(value))
    throw IrError("tensor values cannot be fact index keys");
  const auto &map = std::get<VmTextMapPtr>(value);
  if (!map)
    throw IrError("fact index key contains a null text map");
  appendEntries(map->entries);
}

Json pieces(std::span<const PieceId> value) {
  Json result = Json::array();
  for (const auto piece : value)
    result.push_back(piece);
  return result;
}

// CBOR is an external boundary: JSON numeric conversions otherwise truncate
// fractions and narrow oversized integers before the caller can validate them.
std::uint64_t readUnsigned(const Json &value,
                           std::uint64_t maximum = UINT64_MAX) {
  if (!value.is_number_integer() ||
      (!value.is_number_unsigned() && value.get<std::int64_t>() < 0))
    throw IrError("stored fact contains an invalid unsigned integer");
  const auto number = value.get<std::uint64_t>();
  if (number > maximum)
    throw IrError("stored fact integer is outside its range");
  return number;
}

PieceSequence readPieces(const Json &value, bool allowEmpty = false) {
  if (!value.is_array() || (!allowEmpty && value.empty()) ||
      value.size() > kMaximumStorageItems)
    throw IrError("stored fact contains an invalid SentencePiece sequence");
  PieceSequence result;
  result.reserve(value.size());
  for (const auto &piece : value) {
    const auto number = readUnsigned(piece, std::numeric_limits<PieceId>::max());
    result.push_back(static_cast<PieceId>(number));
  }
  return result;
}

Json encodeValue(const VmValue &value,
                 std::span<const PieceSequence> symbols, std::size_t depth);

Json encodeEntries(
    std::span<const std::pair<IrSymbolRef, VmValue>> entries,
    std::span<const PieceSequence> symbols, std::size_t depth) {
  if (entries.size() > kMaximumStorageItems)
    throw IrError("fact storage entry count exceeds its limit");
  Json result = Json::array();
  for (const auto &[field, value] : entries)
    result.push_back(
        Json::array({pieces(irSymbolPieces(symbols, field)),
                     encodeValue(value, symbols, depth + 1)}));
  return result;
}

Json encodeFact(const VmFact &fact, std::span<const PieceSequence> symbols,
                std::size_t depth) {
  if (fact.type == 0)
    throw IrError("fact storage cannot encode an untyped fact");
  return Json::array(
      {fact.id, pieces(irSymbolPieces(symbols, fact.type)),
       static_cast<std::uint8_t>(fact.origin), fact.createdSequence,
       encodeEntries(fact.fields, symbols, depth)});
}

Json encodeValue(const VmValue &value,
                 std::span<const PieceSequence> symbols, std::size_t depth) {
  if (depth > kMaximumStorageDepth)
    throw IrError("fact storage value nesting exceeds its limit");
  const auto kind = runtimeValueKind(value);
  switch (kind) {
  case RuntimeValueKind::Nil:
    return Json::array({static_cast<std::uint8_t>(kind)});
  case RuntimeValueKind::Number: {
    const auto number = std::get<double>(value);
    if (!std::isfinite(number))
      throw IrError("fact storage cannot encode a non-finite number");
    return Json::array({static_cast<std::uint8_t>(kind), number});
  }
  case RuntimeValueKind::Degree:
    return Json::array({static_cast<std::uint8_t>(kind),
                        std::get<VmDegree>(value).value});
  case RuntimeValueKind::Text:
    return Json::array({static_cast<std::uint8_t>(kind),
                        pieces(std::get<VmText>(value).pieces)});
  case RuntimeValueKind::Symbol:
    return Json::array(
        {static_cast<std::uint8_t>(kind),
         pieces(irSymbolPieces(symbols, std::get<VmSymbol>(value).value))});
  case RuntimeValueKind::Array: {
    const auto &array = std::get<VmArrayPtr>(value);
    if (!array || array->values.size() > kMaximumStorageItems)
      throw IrError("fact storage cannot encode an invalid array");
    Json items = Json::array();
    for (const auto &item : array->values)
      items.push_back(encodeValue(item, symbols, depth + 1));
    return Json::array({static_cast<std::uint8_t>(kind), std::move(items)});
  }
  case RuntimeValueKind::Map: {
    const auto &map = std::get<VmMapPtr>(value);
    if (!map)
      throw IrError("fact storage cannot encode an invalid map");
    return Json::array({static_cast<std::uint8_t>(kind),
                        encodeEntries(map->entries, symbols, depth)});
  }
  case RuntimeValueKind::Fact: {
    const auto &fact = std::get<VmFactPtr>(value);
    if (!fact)
      throw IrError("fact storage cannot encode an invalid nested fact");
    return Json::array({static_cast<std::uint8_t>(kind),
                        encodeFact(*fact, symbols, depth + 1)});
  }
  case RuntimeValueKind::TextMap: {
    const auto &map = std::get<VmTextMapPtr>(value);
    if (!map || map->entries.size() > kMaximumStorageItems)
      throw IrError("fact storage cannot encode an invalid text map");
    Json entries = Json::array();
    for (const auto &[field, item] : map->entries)
      entries.push_back(Json::array(
          {pieces(field), encodeValue(item, symbols, depth + 1)}));
    return Json::array({static_cast<std::uint8_t>(kind), std::move(entries)});
  }
  case RuntimeValueKind::Tensor:
    throw IrError("fact storage cannot persist a LibTorch tensor value");
  }
  throw IrError("fact storage encountered an unknown value kind");
}

void requireArray(const Json &value, std::size_t size,
                  std::string_view description) {
  if (!value.is_array() || value.size() != size)
    throw IrError("stored fact has invalid " + std::string(description));
}

VmValue decodeValue(const Json &value, const StoredSymbolInterner &intern,
                    std::size_t depth);

std::vector<std::pair<IrSymbolRef, VmValue>>
decodeEntries(const Json &value, const StoredSymbolInterner &intern,
              std::size_t depth) {
  if (!value.is_array() || value.size() > kMaximumStorageItems)
    throw IrError("stored fact has an invalid field collection");
  std::vector<std::pair<IrSymbolRef, VmValue>> result;
  result.reserve(value.size());
  for (const auto &entry : value) {
    requireArray(entry, 2, "field entry");
    result.emplace_back(intern(readPieces(entry[0])),
                        decodeValue(entry[1], intern, depth + 1));
  }
  return result;
}

VmFactPtr decodeFact(const Json &value, const StoredSymbolInterner &intern,
                     std::size_t depth) {
  requireArray(value, 5, "fact record");
  auto fact = std::make_shared<VmFact>();
  fact->id = readUnsigned(value[0], std::numeric_limits<IrFactRef>::max());
  fact->type = intern(readPieces(value[1]));
  const auto origin = readUnsigned(value[2],
      static_cast<std::uint8_t>(VmFact::Origin::Derived));
  fact->origin = static_cast<VmFact::Origin>(origin);
  fact->createdSequence = readUnsigned(value[3]);
  fact->fields = decodeEntries(value[4], intern, depth);
  return fact;
}

VmValue decodeValue(const Json &value, const StoredSymbolInterner &intern,
                    std::size_t depth) {
  if (depth > kMaximumStorageDepth || !value.is_array() || value.empty())
    throw IrError("stored fact contains an invalid nested value");
  const auto kind = static_cast<RuntimeValueKind>(readUnsigned(value[0], UINT8_MAX));
  switch (kind) {
  case RuntimeValueKind::Nil:
    requireArray(value, 1, "nil value");
    return VmNil{};
  case RuntimeValueKind::Number: {
    requireArray(value, 2, "number value");
    const auto number = value[1].get<double>();
    if (!std::isfinite(number))
      throw IrError("stored fact contains a non-finite number");
    return number;
  }
  case RuntimeValueKind::Degree:
    requireArray(value, 2, "degree value");
    return VmDegree(value[1].get<double>());
  case RuntimeValueKind::Text:
    requireArray(value, 2, "text value");
    return VmText{readPieces(value[1], true)};
  case RuntimeValueKind::Symbol:
    requireArray(value, 2, "symbol value");
    return VmSymbol{intern(readPieces(value[1]))};
  case RuntimeValueKind::Array: {
    requireArray(value, 2, "array value");
    if (!value[1].is_array() || value[1].size() > kMaximumStorageItems)
      throw IrError("stored fact contains an invalid array");
    auto result = std::make_shared<VmArray>();
    result->values.reserve(value[1].size());
    for (const auto &item : value[1])
      result->values.push_back(decodeValue(item, intern, depth + 1));
    return result;
  }
  case RuntimeValueKind::Map: {
    requireArray(value, 2, "map value");
    auto result = std::make_shared<VmMap>();
    result->entries = decodeEntries(value[1], intern, depth);
    return result;
  }
  case RuntimeValueKind::Fact:
    requireArray(value, 2, "nested fact value");
    return decodeFact(value[1], intern, depth + 1);
  case RuntimeValueKind::TextMap: {
    requireArray(value, 2, "text-map value");
    if (!value[1].is_array() || value[1].size() > kMaximumStorageItems)
      throw IrError("stored fact contains an invalid text map");
    auto result = std::make_shared<VmTextMap>();
    result->entries.reserve(value[1].size());
    for (const auto &entry : value[1]) {
      requireArray(entry, 2, "text-map entry");
      result->entries.emplace_back(
          readPieces(entry[0], true), decodeValue(entry[1], intern, depth + 1));
    }
    return result;
  }
  case RuntimeValueKind::Tensor:
    throw IrError("stored fact contains an unsupported tensor value");
  }
  throw IrError("stored fact contains an unknown value kind");
}

} // namespace

StoredValueKey encodeStoredValueKey(
    const VmValue &value, std::span<const PieceSequence> symbols) {
  StoredValueKey result;
  appendValueKey(value, symbols, result);
  return result;
}

std::string encodeStoredFact(const VmFact &fact,
                             std::optional<std::string_view> source,
                             std::span<const PieceSequence> symbols) {
  const Json record = Json::array(
      {kFactStorageVersion, encodeFact(fact, symbols, 0),
       source ? Json(*source) : Json(nullptr)});
  const auto bytes = Json::to_cbor(record);
  return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}

StoredFactRecord decodeStoredFact(std::string_view bytes,
                                  const StoredSymbolInterner &intern) {
  if (!intern || bytes.empty())
    throw IrError("stored fact record is empty or has no symbol interner");
  try {
    const auto *begin = reinterpret_cast<const std::uint8_t *>(bytes.data());
    const Json record = Json::from_cbor(begin, begin + bytes.size());
    requireArray(record, 3, "storage envelope");
    if (readUnsigned(record[0]) != kFactStorageVersion)
      throw IrError("stored fact schema version is unsupported");
    StoredFactRecord result;
    result.fact = decodeFact(record[1], intern, 0);
    if (!record[2].is_null())
      result.source = record[2].get<std::string>();
    return result;
  } catch (const IrError &) {
    throw;
  } catch (const std::exception &error) {
    throw IrError("cannot decode stored fact record: " +
                  std::string(error.what()));
  }
}

std::string encodeStoredFactType(
    IrSymbolRef type, std::span<const IrSymbolRef> parents,
    std::span<const std::vector<IrSymbolRef>> indexes,
    std::span<const PieceSequence> symbols) {
  Json encodedParents = Json::array();
  for (const auto parent : parents)
    encodedParents.push_back(pieces(irSymbolPieces(symbols, parent)));
  Json encodedIndexes = Json::array();
  for (const auto &index : indexes) {
    Json fields = Json::array();
    for (const auto field : index)
      fields.push_back(pieces(irSymbolPieces(symbols, field)));
    encodedIndexes.push_back(std::move(fields));
  }
  const Json record = Json::array(
      {kFactTypeStorageVersion, pieces(irSymbolPieces(symbols, type)),
       std::move(encodedParents), std::move(encodedIndexes)});
  const auto bytes = Json::to_cbor(record);
  return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}

StoredFactTypeRecord decodeStoredFactType(
    std::string_view bytes, const StoredSymbolInterner &intern) {
  if (!intern || bytes.empty())
    throw IrError("stored fact-type record is empty or has no symbol interner");
  try {
    const auto *begin = reinterpret_cast<const std::uint8_t *>(bytes.data());
    const Json record = Json::from_cbor(begin, begin + bytes.size());
    requireArray(record, 4, "fact-type storage envelope");
    if (readUnsigned(record[0]) != kFactTypeStorageVersion)
      throw IrError("stored fact-type schema version is unsupported");
    StoredFactTypeRecord result;
    result.type = intern(readPieces(record[1]));
    if (!record[2].is_array() || record[2].size() > kMaximumStorageItems ||
        !record[3].is_array() || record[3].size() > kMaximumStorageItems)
      throw IrError("stored fact-type metadata is invalid");
    for (const auto &parent : record[2])
      result.parents.push_back(intern(readPieces(parent)));
    for (const auto &encodedIndex : record[3]) {
      if (!encodedIndex.is_array() || encodedIndex.empty() ||
          encodedIndex.size() > kMaximumStorageItems)
        throw IrError("stored fact-type index is invalid");
      std::vector<IrSymbolRef> index;
      index.reserve(encodedIndex.size());
      for (const auto &field : encodedIndex)
        index.push_back(intern(readPieces(field)));
      result.indexes.push_back(std::move(index));
    }
    return result;
  } catch (const IrError &) {
    throw;
  } catch (const std::exception &error) {
    throw IrError("cannot decode stored fact-type record: " +
                  std::string(error.what()));
  }
}

} // namespace Felidae::Form
