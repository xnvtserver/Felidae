#include "../common/NativeJson.h"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#if defined(_WIN32)
#define FELIDAE_SET_EXPORT __declspec(dllexport)
#else
#define FELIDAE_SET_EXPORT __attribute__((visibility("default")))
#endif

namespace {
using Felidae::NativeJson::Value;

std::string operationName(const std::string& name) {
    const size_t separator = name.rfind(':');
    return separator == std::string::npos ? name : name.substr(separator + 1);
}

const Value& requireSets(const Value& args) {
    return Felidae::NativeJson::requireField(args, "sets", Value::Kind::Array, "Set operation");
}

std::vector<std::string> fields(const Value& args) {
    const Value* value = Felidae::NativeJson::field(args, "fields");
    if (!value) return {};
    if (value->kind != Value::Kind::Array) throw std::runtime_error("Set fields must be an array");
    std::vector<std::string> result;
    for (const Value& item : value->items) {
        if (item.kind != Value::Kind::String) throw std::runtime_error("Set fields must contain strings");
        result.push_back(item.text);
    }
    return result;
}

std::string keyFor(const Value& item, const std::vector<std::string>& selected) {
    if (selected.empty()) return Felidae::NativeJson::stringify(item);
    if (item.kind != Value::Kind::Object) {
        throw std::runtime_error("Set field comparison requires fact/object values");
    }
    std::string key;
    for (const std::string& fieldName : selected) {
        const Value* field = Felidae::NativeJson::field(item, fieldName);
        if (!field) return {};
        key += std::to_string(fieldName.size()) + ":" + fieldName + "=" +
               Felidae::NativeJson::stringify(*field) + ";";
    }
    return key;
}

std::vector<const Value*> inputSets(const Value& args) {
    const Value& sets = requireSets(args);
    if (sets.items.empty()) throw std::runtime_error("Set operation requires at least one input set");
    std::vector<const Value*> result;
    for (const Value& set : sets.items) {
        if (set.kind != Value::Kind::Array) throw std::runtime_error("Set input cannot resolve to a collection");
        result.push_back(&set);
    }
    return result;
}

std::unordered_set<std::string> keysOf(const Value& set,
                                       const std::vector<std::string>& selected,
                                       bool requireComparable = true) {
    std::unordered_set<std::string> keys;
    for (const Value& item : set.items) {
        std::string key = keyFor(item, selected);
        if (!key.empty()) keys.insert(std::move(key));
    }
    if (requireComparable && !selected.empty() && !set.items.empty() && keys.empty()) {
        throw std::runtime_error("Set.by cannot compare an input set because no facts contain all requested fields");
    }
    return keys;
}

Value orderedUnique(const std::vector<const Value*>& sets,
                    const std::vector<std::string>& selected,
                    const std::unordered_set<std::string>* allowed = nullptr) {
    Value result;
    result.kind = Value::Kind::Array;
    std::unordered_set<std::string> seen;
    for (const Value* set : sets) {
        for (const Value& item : set->items) {
            std::string key = keyFor(item, selected);
            if (key.empty() || (allowed && !allowed->count(key))) continue;
            const std::string exact = Felidae::NativeJson::stringify(item);
            if (seen.insert(exact).second) result.items.push_back(item);
        }
    }
    return result;
}

Value setUnion(const Value& args) {
    return orderedUnique(inputSets(args), {});
}

Value intersection(const Value& args) {
    auto sets = inputSets(args);
    auto selected = fields(args);
    auto common = keysOf(*sets.front(), selected);
    for (size_t i = 1; i < sets.size(); ++i) {
        auto keys = keysOf(*sets[i], selected);
        for (auto it = common.begin(); it != common.end();) {
            if (!keys.count(*it)) it = common.erase(it);
            else ++it;
        }
    }
    return orderedUnique(sets, selected, &common);
}

Value difference(const Value& args) {
    auto sets = inputSets(args);
    auto selected = fields(args);
    std::unordered_set<std::string> excluded;
    for (size_t i = 1; i < sets.size(); ++i) {
        auto keys = keysOf(*sets[i], selected, false);
        excluded.insert(keys.begin(), keys.end());
    }
    Value result;
    result.kind = Value::Kind::Array;
    std::unordered_set<std::string> seen;
    for (const Value& item : sets.front()->items) {
        std::string key = keyFor(item, selected);
        if (key.empty() || excluded.count(key)) continue;
        const std::string exact = Felidae::NativeJson::stringify(item);
        if (seen.insert(exact).second) result.items.push_back(item);
    }
    return result;
}

Value symmetricDifference(const Value& args) {
    auto sets = inputSets(args);
    auto selected = fields(args);
    std::vector<std::unordered_set<std::string>> allKeys;
    for (const Value* set : sets) allKeys.push_back(keysOf(*set, selected));
    std::unordered_set<std::string> allowed;
    for (const auto& keys : allKeys) {
        for (const auto& key : keys) {
            size_t occurrences = 0;
            for (const auto& other : allKeys) occurrences += other.count(key);
            if (occurrences != allKeys.size()) allowed.insert(key);
        }
    }
    return orderedUnique(sets, selected, &allowed);
}

bool subsetOf(const Value& left, const Value& right, const std::vector<std::string>& selected) {
    const auto leftKeys = keysOf(left, selected);
    const auto rightKeys = keysOf(right, selected);
    return std::all_of(leftKeys.begin(), leftKeys.end(), [&](const std::string& key) {
        return rightKeys.count(key) != 0;
    });
}

Value boolean(bool value) {
    Value result;
    result.kind = Value::Kind::Bool;
    result.boolean = value;
    return result;
}

Value relation(const std::string& operation, const Value& args) {
    auto sets = inputSets(args);
    if (sets.size() != 2) throw std::runtime_error("Set relation requires exactly two input sets");
    auto selected = fields(args);
    if (operation == "equals") return boolean(subsetOf(*sets[0], *sets[1], selected) &&
                                               subsetOf(*sets[1], *sets[0], selected));
    if (operation == "subset") return boolean(subsetOf(*sets[0], *sets[1], selected));
    if (operation == "superset") return boolean(subsetOf(*sets[1], *sets[0], selected));
    const auto left = keysOf(*sets[0], selected);
    const auto right = keysOf(*sets[1], selected);
    const bool shared = std::any_of(left.begin(), left.end(), [&](const std::string& key) {
        return right.count(key) != 0;
    });
    return boolean(!shared);
}

Value dispatch(const std::string& functionName, const Value& args) {
    const std::string operation = operationName(functionName);
    if (operation == "union") return setUnion(args);
    if (operation == "intersection") return intersection(args);
    if (operation == "difference") return difference(args);
    if (operation == "symmetricDifference") return symmetricDifference(args);
    if (operation == "equals" || operation == "subset" ||
        operation == "superset" || operation == "disjoint") {
        return relation(operation, args);
    }
    if (operation == "cardinality") {
        auto sets = inputSets(args);
        if (sets.size() != 1) throw std::runtime_error("Set.cardinality requires one input set");
        Value result;
        result.kind = Value::Kind::Number;
        result.number = static_cast<double>(keysOf(*sets.front(), {}).size());
        return result;
    }
    if (operation == "contains") {
        auto sets = inputSets(args);
        if (sets.size() != 1) throw std::runtime_error("Set.contains requires one input set");
        const Value* wanted = Felidae::NativeJson::field(args, "value");
        if (!wanted) throw std::runtime_error("Set.contains requires value");
        const auto selected = fields(args);
        const auto keys = keysOf(*sets.front(), selected);
        std::string wantedKey;
        if (!selected.empty() && wanted->kind != Value::Kind::Object) {
            if (selected.size() != 1) {
                throw std::runtime_error("Set.containsBy scalar membership requires exactly one field");
            }
            wantedKey = std::to_string(selected.front().size()) + ":" + selected.front() +
                        "=" + Felidae::NativeJson::stringify(*wanted) + ";";
        } else {
            wantedKey = keyFor(*wanted, selected);
        }
        return boolean(keys.count(wantedKey) != 0);
    }
    throw std::runtime_error("Unsupported Set native function '" + functionName + "'");
}
}

extern "C" FELIDAE_SET_EXPORT char* felidae_native_call(const char* functionName,
                                                         const char* argsJson) {
    try {
        const Value args = Felidae::NativeJson::parse(argsJson, "Set native module");
        return Felidae::NativeJson::copyResponse(
            Felidae::NativeJson::stringify(dispatch(functionName ? functionName : "", args)));
    } catch (const std::exception& error) {
        return Felidae::NativeJson::copyResponse(
            Felidae::NativeJson::stringify(Felidae::NativeJson::error(error.what())));
    }
}

extern "C" FELIDAE_SET_EXPORT void felidae_native_free(char* value) {
    std::free(value);
}
