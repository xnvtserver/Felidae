#include "Group.h"

#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Felidae::Form::Group {
namespace {

std::string key(const Json::Value &value) { return value.dump(); }

struct FiniteOperation {
  const Json::Value &members;
  std::unordered_map<std::string, Json::Value> table;

  FiniteOperation(const Json::Value &members, const Json::Value &rows)
      : members(members) {
    if (!members.is_array())
      throw std::runtime_error("Group set must be an array");
    if (members.empty())
      throw std::runtime_error("Group set must not be empty");
    if (!rows.is_array())
      throw std::runtime_error("Group table must be an array");
    std::unordered_set<std::string> memberKeys;
    for (const auto &member : members) {
      if (!memberKeys.insert(key(member)).second)
        throw std::runtime_error("Group set contains a duplicate member");
    }
    for (const auto &row : rows) {
      if (!row.is_object() || !row.contains("left") || !row.contains("right") ||
          !row.contains("result")) {
        throw std::runtime_error(
            "Group table row requires left, right, and result");
      }
      const auto pairKey = key(row.at("left")) + '\x1f' + key(row.at("right"));
      if (!table.emplace(pairKey, row.at("result")).second)
        throw std::runtime_error("Group table contains a duplicate pair");
    }
  }

  const Json::Value *apply(const Json::Value &left,
                           const Json::Value &right) const {
    const auto found = table.find(key(left) + '\x1f' + key(right));
    return found == table.end() ? nullptr : &found->second;
  }
};

bool closed(const FiniteOperation &operation) {
  std::unordered_set<std::string> members;
  for (const auto &item : operation.members)
    members.insert(key(item));
  for (const auto &left : operation.members) {
    for (const auto &right : operation.members) {
      const auto *result = operation.apply(left, right);
      if (!result || !members.contains(key(*result)))
        return false;
    }
  }
  return true;
}

bool associative(const FiniteOperation &operation) {
  for (const auto &a : operation.members) {
    for (const auto &b : operation.members) {
      for (const auto &c : operation.members) {
        const auto *ab = operation.apply(a, b);
        const auto *bc = operation.apply(b, c);
        if (!ab || !bc)
          return false;
        const auto *left = operation.apply(*ab, c);
        const auto *right = operation.apply(a, *bc);
        if (!left || !right || *left != *right)
          return false;
      }
    }
  }
  return true;
}

bool hasIdentity(const FiniteOperation &operation,
                 const Json::Value &identity) {
  for (const auto &item : operation.members) {
    const auto *left = operation.apply(identity, item);
    const auto *right = operation.apply(item, identity);
    if (!left || !right || *left != item || *right != item)
      return false;
  }
  return true;
}

bool hasInverses(const FiniteOperation &operation,
                 const Json::Value &identity) {
  for (const auto &item : operation.members) {
    bool found = false;
    for (const auto &candidate : operation.members) {
      const auto *left = operation.apply(item, candidate);
      const auto *right = operation.apply(candidate, item);
      if (left && right && *left == identity && *right == identity) {
        found = true;
        break;
      }
    }
    if (!found)
      return false;
  }
  return true;
}

bool commutative(const FiniteOperation &operation) {
  for (const auto &left : operation.members) {
    for (const auto &right : operation.members) {
      const auto *forward = operation.apply(left, right);
      const auto *reverse = operation.apply(right, left);
      if (!forward || !reverse || *forward != *reverse)
        return false;
    }
  }
  return true;
}

} // namespace

Json::Value evaluate(BuiltinId operationId, const Json::Value &members,
                     const Json::Value &table,
                     const std::optional<Json::Value> &identity) {
  const FiniteOperation operation{members, table};
  switch (operationId) {
  case BuiltinId::GroupClosed:
    return closed(operation);
  case BuiltinId::GroupAssociative:
    return associative(operation);
  case BuiltinId::GroupIdentity:
    return identity && hasIdentity(operation, identity.value());
  case BuiltinId::GroupInverse:
    return identity && hasInverses(operation, identity.value());
  case BuiltinId::GroupCommutative:
    return commutative(operation);
  case BuiltinId::GroupAbelian:
    return identity && closed(operation) && associative(operation) &&
           hasIdentity(operation, identity.value()) &&
           hasInverses(operation, identity.value()) && commutative(operation);
  case BuiltinId::GroupValidate: {
    const bool closure = closed(operation);
    const bool associativity = associative(operation);
    const bool identityValid =
        identity && hasIdentity(operation, identity.value());
    const bool inverseValid =
        identity && hasInverses(operation, identity.value());
    const bool commutativity = commutative(operation);
    return Json::Value{
        {"valid", closure && associativity && identityValid && inverseValid},
        {"closure", closure},
        {"associative", associativity},
        {"identity", identityValid},
        {"inverse", inverseValid},
        {"commutative", commutativity},
        {"abelian", closure && associativity && identityValid && inverseValid &&
                        commutativity}};
  }
  default:
    throw std::runtime_error("Unsupported Group operation");
  }
}

} // namespace Felidae::Form::Group
