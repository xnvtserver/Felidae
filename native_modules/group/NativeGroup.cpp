#include "../common/NativeJson.h"

#include <cstdlib>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

#if defined(_WIN32)
#define FELIDAE_GROUP_EXPORT __declspec(dllexport)
#else
#define FELIDAE_GROUP_EXPORT __attribute__((visibility("default")))
#endif

namespace {
using Felidae::NativeJson::Value;

std::string key(const Value& value) { return Felidae::NativeJson::stringify(value); }

Value boolean(bool value) {
    Value result;
    result.kind = Value::Kind::Bool;
    result.boolean = value;
    return result;
}

struct FiniteOperation {
    const Value& members;
    std::unordered_map<std::string, Value> table;

    explicit FiniteOperation(const Value& args)
        : members(Felidae::NativeJson::requireField(args, "set", Value::Kind::Array, "Group")) {
        const Value& rows =
            Felidae::NativeJson::requireField(args, "table", Value::Kind::Array, "Group");
        for (const Value& row : rows.items) {
            if (row.kind != Value::Kind::Object) throw std::runtime_error("Group table rows must be objects");
            const Value* left = Felidae::NativeJson::field(row, "left");
            const Value* right = Felidae::NativeJson::field(row, "right");
            const Value* result = Felidae::NativeJson::field(row, "result");
            if (!left || !right || !result) throw std::runtime_error("Group table row requires left, right, and result");
            table[key(*left) + "\x1f" + key(*right)] = *result;
        }
    }

    const Value* apply(const Value& left, const Value& right) const {
        const auto found = table.find(key(left) + "\x1f" + key(right));
        return found == table.end() ? nullptr : &found->second;
    }
};

bool closed(const FiniteOperation& operation) {
    std::unordered_set<std::string> members;
    for (const Value& item : operation.members.items) members.insert(key(item));
    for (const Value& left : operation.members.items) {
        for (const Value& right : operation.members.items) {
            const Value* result = operation.apply(left, right);
            if (!result || !members.count(key(*result))) return false;
        }
    }
    return true;
}

bool associative(const FiniteOperation& operation) {
    for (const Value& a : operation.members.items) {
        for (const Value& b : operation.members.items) {
            for (const Value& c : operation.members.items) {
                const Value* ab = operation.apply(a, b);
                const Value* bc = operation.apply(b, c);
                if (!ab || !bc) return false;
                const Value* left = operation.apply(*ab, c);
                const Value* right = operation.apply(a, *bc);
                if (!left || !right || key(*left) != key(*right)) return false;
            }
        }
    }
    return true;
}

bool hasIdentity(const FiniteOperation& operation, const Value& identity) {
    for (const Value& item : operation.members.items) {
        const Value* left = operation.apply(identity, item);
        const Value* right = operation.apply(item, identity);
        if (!left || !right || key(*left) != key(item) || key(*right) != key(item)) return false;
    }
    return true;
}

bool hasInverses(const FiniteOperation& operation, const Value& identity) {
    for (const Value& item : operation.members.items) {
        bool found = false;
        for (const Value& candidate : operation.members.items) {
            const Value* left = operation.apply(item, candidate);
            const Value* right = operation.apply(candidate, item);
            if (left && right && key(*left) == key(identity) && key(*right) == key(identity)) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

bool commutative(const FiniteOperation& operation) {
    for (const Value& left : operation.members.items) {
        for (const Value& right : operation.members.items) {
            const Value* forward = operation.apply(left, right);
            const Value* reverse = operation.apply(right, left);
            if (!forward || !reverse || key(*forward) != key(*reverse)) return false;
        }
    }
    return true;
}

Value dispatch(const std::string& functionName, const Value& args) {
    const size_t separator = functionName.rfind(':');
    const std::string operationName =
        separator == std::string::npos ? functionName : functionName.substr(separator + 1);
    const FiniteOperation operation(args);
    const Value* identity = Felidae::NativeJson::field(args, "identity");
    const bool closure = closed(operation);
    const bool associativity = associative(operation);
    const bool identityValid = identity && hasIdentity(operation, *identity);
    const bool inverseValid = identity && hasInverses(operation, *identity);
    const bool commutativity = commutative(operation);
    if (operationName == "closed") return boolean(closure);
    if (operationName == "associative") return boolean(associativity);
    if (operationName == "identity") return boolean(identityValid);
    if (operationName == "inverse") return boolean(inverseValid);
    if (operationName == "commutative") return boolean(commutativity);
    if (operationName != "validate" && operationName != "abelian") {
        throw std::runtime_error("Unsupported Group native function");
    }
    Value result;
    result.kind = Value::Kind::Object;
    result.fieldOrder = {"valid", "closure", "associative", "identity", "inverse", "commutative", "abelian"};
    result.fields.emplace("closure", boolean(closure));
    result.fields.emplace("associative", boolean(associativity));
    result.fields.emplace("identity", boolean(identityValid));
    result.fields.emplace("inverse", boolean(inverseValid));
    result.fields.emplace("commutative", boolean(commutativity));
    const bool valid = closure && associativity && identityValid && inverseValid;
    result.fields.emplace("abelian", boolean(valid && commutativity));
    result.fields.emplace("valid", boolean(valid));
    return result;
}
}

extern "C" FELIDAE_GROUP_EXPORT char* felidae_native_call(const char* functionName,
                                                           const char* argsJson) {
    try {
        const Value args = Felidae::NativeJson::parse(argsJson, "Group native module");
        return Felidae::NativeJson::copyResponse(
            Felidae::NativeJson::stringify(dispatch(functionName ? functionName : "", args)));
    } catch (const std::exception& error) {
        return Felidae::NativeJson::copyResponse(
            Felidae::NativeJson::stringify(Felidae::NativeJson::error(error.what())));
    }
}

extern "C" FELIDAE_GROUP_EXPORT void felidae_native_free(char* value) {
    std::free(value);
}
