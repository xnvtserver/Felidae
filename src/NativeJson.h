#pragma once

#include <nlohmann/json.hpp>

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace Felidae::NativeJson {

struct Value {
    enum class Kind { Null, Bool, Number, String, Array, Object } kind = Kind::Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;
    std::vector<Value> items;
    std::map<std::string, Value> fields;
    std::vector<std::string> fieldOrder;
};

inline void skipWhitespace(const std::string& source, size_t& position) {
    while (position < source.size() &&
           std::isspace(static_cast<unsigned char>(source[position]))) {
        ++position;
    }
}

inline Value fromNlohmann(const nlohmann::json& node) {
    Value value;
    switch (node.type()) {
        case nlohmann::json::value_t::null:
        case nlohmann::json::value_t::discarded:
            value.kind = Value::Kind::Null;
            break;
        case nlohmann::json::value_t::boolean:
            value.kind = Value::Kind::Bool;
            value.boolean = node.get<bool>();
            break;
        case nlohmann::json::value_t::number_integer:
        case nlohmann::json::value_t::number_unsigned:
        case nlohmann::json::value_t::number_float:
            value.kind = Value::Kind::Number;
            value.number = node.get<double>();
            break;
        case nlohmann::json::value_t::string:
            value.kind = Value::Kind::String;
            value.text = node.get<std::string>();
            break;
        case nlohmann::json::value_t::array:
            value.kind = Value::Kind::Array;
            value.items.reserve(node.size());
            for (const auto& item : node) value.items.push_back(fromNlohmann(item));
            break;
        case nlohmann::json::value_t::object:
            value.kind = Value::Kind::Object;
            value.fieldOrder.reserve(node.size());
            for (auto entry = node.begin(); entry != node.end(); ++entry) {
                value.fieldOrder.push_back(entry.key());
                value.fields.emplace(entry.key(), fromNlohmann(entry.value()));
            }
            break;
        default:
            value.kind = Value::Kind::Null;
            break;
    }
    return value;
}

// Parses through the real JSON grammar parser (nlohmann::json, already
// vendored at third_party/nlohmann_json) rather than a second, bespoke
// recursive-descent one: hand-rolling string escapes, number formats
// (exponents, leading-zero rejection), and object/array nesting here would
// only be duplicating what that library already does correctly. Every call
// site in this codebase parses one complete document starting at position
// 0, never several values back-to-back from the same buffer, so "parse the
// rest of source as one document" is exactly what "one value from position"
// needs to mean.
inline bool parseValue(const std::string& source, size_t& position, Value& value) {
    const std::string remaining = source.substr(position);
    const nlohmann::json parsed = nlohmann::json::parse(remaining, nullptr, false);
    if (parsed.is_discarded()) return false;
    value = fromNlohmann(parsed);
    position = source.size();
    return true;
}

inline Value parse(const char* raw, const std::string& context) {
    const std::string source = raw ? raw : "{}";
    size_t position = 0;
    Value value;
    if (!parseValue(source, position, value)) {
        throw std::runtime_error(context + " received invalid JSON");
    }
    skipWhitespace(source, position);
    if (position != source.size()) {
        throw std::runtime_error(context + " received trailing JSON data");
    }
    return value;
}

inline const Value* field(const Value& object, const std::string& key) {
    if (object.kind != Value::Kind::Object) return nullptr;
    const auto found = object.fields.find(key);
    return found == object.fields.end() ? nullptr : &found->second;
}

inline const Value& requireField(const Value& object,
                                 const std::string& key,
                                 Value::Kind kind,
                                 const std::string& context) {
    const Value* value = field(object, key);
    if (!value || value->kind != kind) {
        throw std::runtime_error(context + " expects '" + key + "' with the declared type");
    }
    return *value;
}

inline std::string escape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (const char current : value) {
        switch (current) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(current); break;
        }
    }
    return out;
}

inline std::string stringify(const Value& value) {
    switch (value.kind) {
        case Value::Kind::Null:
            return "null";
        case Value::Kind::Bool:
            return value.boolean ? "true" : "false";
        case Value::Kind::Number: {
            std::ostringstream out;
            out.precision(15);
            out << value.number;
            return out.str();
        }
        case Value::Kind::String:
            return "\"" + escape(value.text) + "\"";
        case Value::Kind::Array: {
            std::string out = "[";
            for (size_t index = 0; index < value.items.size(); ++index) {
                if (index != 0) out += ",";
                out += stringify(value.items[index]);
            }
            return out + "]";
        }
        case Value::Kind::Object: {
            std::string out = "{";
            bool first = true;
            auto appendEntry = [&](const std::string& key, const Value& fieldValue) {
                if (!first) out += ",";
                first = false;
                out += "\"" + escape(key) + "\":" + stringify(fieldValue);
            };
            if (!value.fieldOrder.empty()) {
                for (const auto& key : value.fieldOrder) {
                    const auto found = value.fields.find(key);
                    if (found != value.fields.end()) appendEntry(found->first, found->second);
                }
            } else {
                for (const auto& entry : value.fields) appendEntry(entry.first, entry.second);
            }
            return out + "}";
        }
    }
    return "null";
}

inline Value string(std::string value) {
    Value result;
    result.kind = Value::Kind::String;
    result.text = std::move(value);
    return result;
}

inline Value error(const std::string& message) {
    Value result;
    result.kind = Value::Kind::Object;
    result.fields.emplace("error", string(message));
    return result;
}

inline char* copyResponse(const std::string& response) {
    char* buffer = static_cast<char*>(std::malloc(response.size() + 1));
    if (!buffer) return nullptr;
    std::memcpy(buffer, response.c_str(), response.size() + 1);
    return buffer;
}

} // namespace Felidae::NativeJson
