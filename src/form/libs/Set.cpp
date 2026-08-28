#include "Set.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace Felidae::Form::Set {
namespace {

using Keys = std::unordered_set<std::string>;

std::vector<std::string> selectedFields(const Json::Value &fields) {
  if (!fields.is_array())
    throw std::runtime_error("Set fields must be an array");
  std::vector<std::string> result;
  result.reserve(fields.size());
  for (const auto &field : fields) {
    if (!field.is_string())
      throw std::runtime_error("Set fields must contain strings");
    result.push_back(field.get<std::string>());
  }
  return result;
}

std::vector<const Json::Value *> inputSets(const Json::Value &sets) {
  if (!sets.is_array() || sets.empty())
    throw std::runtime_error("Set operation requires at least one input set");
  std::vector<const Json::Value *> result;
  result.reserve(sets.size());
  for (const auto &set : sets) {
    if (!set.is_array())
      throw std::runtime_error("Set input must be an array");
    result.push_back(&set);
  }
  return result;
}

std::string keyFor(const Json::Value &item,
                   const std::vector<std::string> &fields) {
  if (fields.empty())
    return item.dump();
  if (!item.is_object())
    throw std::runtime_error("Set field comparison requires object values");
  std::string key;
  for (const auto &field : fields) {
    const auto found = item.find(field);
    if (found == item.end())
      return {};
    key +=
        std::to_string(field.size()) + ':' + field + '=' + found->dump() + ';';
  }
  return key;
}

Keys keysOf(const Json::Value &set, const std::vector<std::string> &fields,
            bool requireComparable = true) {
  Keys keys;
  for (const auto &item : set) {
    auto key = keyFor(item, fields);
    if (!key.empty())
      keys.insert(std::move(key));
  }
  if (requireComparable && !fields.empty() && !set.empty() && keys.empty())
    throw std::runtime_error(
        "Set.by cannot compare an input set because no objects contain all "
        "requested fields");
  return keys;
}

Json::Value orderedUnique(const std::vector<const Json::Value *> &sets,
                          const std::vector<std::string> &fields,
                          const Keys *allowed = nullptr) {
  auto result = Json::Value::array();
  Keys seen;
  for (const auto *set : sets) {
    for (const auto &item : *set) {
      const auto comparisonKey = keyFor(item, fields);
      if (comparisonKey.empty() ||
          (allowed && !allowed->contains(comparisonKey)))
        continue;
      if (seen.insert(item.dump()).second)
        result.push_back(item);
    }
  }
  return result;
}

bool subset(const Json::Value &left, const Json::Value &right,
            const std::vector<std::string> &fields) {
  const auto leftKeys = keysOf(left, fields);
  const auto rightKeys = keysOf(right, fields);
  return std::ranges::all_of(
      leftKeys, [&](const auto &key) { return rightKeys.contains(key); });
}

} // namespace

Json::Value evaluate(BuiltinId operation, const Json::Value &setsValue,
                     const std::optional<Json::Value> &value,
                     const Json::Value &fieldsValue) {
  const auto sets = inputSets(setsValue);
  const auto fields = selectedFields(fieldsValue);
  switch (operation) {
  case BuiltinId::SetUnion:
    return orderedUnique(sets, {});
  case BuiltinId::SetIntersection:
  case BuiltinId::SetIntersectionBy: {
    auto common = keysOf(*sets.front(), fields);
    for (std::size_t index = 1; index < sets.size(); ++index) {
      const auto keys = keysOf(*sets[index], fields);
      std::erase_if(common,
                    [&](const auto &key) { return !keys.contains(key); });
    }
    return orderedUnique(sets, fields, &common);
  }
  case BuiltinId::SetDifference:
  case BuiltinId::SetDifferenceBy: {
    Keys excluded;
    for (std::size_t index = 1; index < sets.size(); ++index) {
      const auto keys = keysOf(*sets[index], fields, false);
      excluded.insert(keys.begin(), keys.end());
    }
    auto result = Json::Value::array();
    Keys seen;
    for (const auto &item : *sets.front()) {
      const auto key = keyFor(item, fields);
      if (!key.empty() && !excluded.contains(key) &&
          seen.insert(item.dump()).second)
        result.push_back(item);
    }
    return result;
  }
  case BuiltinId::SetSymmetricDifference:
  case BuiltinId::SetSymmetricDifferenceBy: {
    std::vector<Keys> allKeys;
    allKeys.reserve(sets.size());
    for (const auto *set : sets)
      allKeys.push_back(keysOf(*set, fields));
    Keys allowed;
    for (const auto &keys : allKeys) {
      for (const auto &key : keys) {
        const auto occurrences = std::ranges::count_if(
            allKeys, [&](const auto &other) { return other.contains(key); });
        if (occurrences != static_cast<std::ptrdiff_t>(allKeys.size()))
          allowed.insert(key);
      }
    }
    return orderedUnique(sets, fields, &allowed);
  }
  case BuiltinId::SetEquals:
  case BuiltinId::SetEqualsBy:
  case BuiltinId::SetSubset:
  case BuiltinId::SetSubsetBy:
  case BuiltinId::SetSuperset:
  case BuiltinId::SetDisjoint:
  case BuiltinId::SetDisjointBy: {
    if (sets.size() != 2)
      throw std::runtime_error("Set relation requires exactly two input sets");
    if (operation == BuiltinId::SetEquals ||
        operation == BuiltinId::SetEqualsBy)
      return subset(*sets[0], *sets[1], fields) &&
             subset(*sets[1], *sets[0], fields);
    if (operation == BuiltinId::SetSubset ||
        operation == BuiltinId::SetSubsetBy)
      return subset(*sets[0], *sets[1], fields);
    if (operation == BuiltinId::SetSuperset)
      return subset(*sets[1], *sets[0], fields);
    const auto left = keysOf(*sets[0], fields);
    const auto right = keysOf(*sets[1], fields);
    return std::ranges::none_of(
        left, [&](const auto &key) { return right.contains(key); });
  }
  case BuiltinId::SetCardinality:
    if (sets.size() != 1)
      throw std::runtime_error("Set.cardinality requires one input set");
    return static_cast<double>(keysOf(*sets.front(), {}).size());
  case BuiltinId::SetContains:
  case BuiltinId::SetContainsBy: {
    if (sets.size() != 1 || !value)
      throw std::runtime_error("Set.contains requires one set and a value");
    const auto keys = keysOf(*sets.front(), fields);
    std::string wanted;
    if (!fields.empty() && !value->is_object()) {
      if (fields.size() != 1)
        throw std::runtime_error(
            "Set.containsBy scalar membership requires exactly one field");
      wanted = std::to_string(fields.front().size()) + ':' + fields.front() +
               '=' + value->dump() + ';';
    } else {
      wanted = keyFor(*value, fields);
    }
    return keys.contains(wanted);
  }
  default:
    throw std::runtime_error("Unsupported Set operation");
  }
}

} // namespace Felidae::Form::Set
