#pragma once

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
};

inline void skipWhitespace(const std::string& source, size_t& position) {
    while (position < source.size() &&
           std::isspace(static_cast<unsigned char>(source[position]))) {
        ++position;
    }
}

inline bool parseValue(const std::string& source, size_t& position, Value& value);

inline bool parseString(const std::string& source, size_t& position, std::string& value) {
    if (position >= source.size() || source[position] != '"') return false;
    ++position;
    while (position < source.size()) {
        const char current = source[position++];
        if (current == '"') return true;
        if (current != '\\') {
            value.push_back(current);
            continue;
        }
        if (position >= source.size()) return false;
        const char escaped = source[position++];
        switch (escaped) {
            case '"': value.push_back('"'); break;
            case '\\': value.push_back('\\'); break;
            case '/': value.push_back('/'); break;
            case 'b': value.push_back('\b'); break;
            case 'f': value.push_back('\f'); break;
            case 'n': value.push_back('\n'); break;
            case 'r': value.push_back('\r'); break;
            case 't': value.push_back('\t'); break;
            default: return false;
        }
    }
    return false;
}

inline bool parseArray(const std::string& source, size_t& position, Value& value) {
    if (position >= source.size() || source[position] != '[') return false;
    ++position;
    value.kind = Value::Kind::Array;
    skipWhitespace(source, position);
    if (position < source.size() && source[position] == ']') {
        ++position;
        return true;
    }
    while (position < source.size()) {
        Value item;
        if (!parseValue(source, position, item)) return false;
        value.items.push_back(std::move(item));
        skipWhitespace(source, position);
        if (position < source.size() && source[position] == ',') {
            ++position;
            continue;
        }
        if (position < source.size() && source[position] == ']') {
            ++position;
            return true;
        }
        return false;
    }
    return false;
}

inline bool parseObject(const std::string& source, size_t& position, Value& value) {
    if (position >= source.size() || source[position] != '{') return false;
    ++position;
    value.kind = Value::Kind::Object;
    skipWhitespace(source, position);
    if (position < source.size() && source[position] == '}') {
        ++position;
        return true;
    }
    while (position < source.size()) {
        skipWhitespace(source, position);
        std::string key;
        if (!parseString(source, position, key)) return false;
        skipWhitespace(source, position);
        if (position >= source.size() || source[position] != ':') return false;
        ++position;
        Value fieldValue;
        if (!parseValue(source, position, fieldValue)) return false;
        value.fields.emplace(std::move(key), std::move(fieldValue));
        skipWhitespace(source, position);
        if (position < source.size() && source[position] == ',') {
            ++position;
            continue;
        }
        if (position < source.size() && source[position] == '}') {
            ++position;
            return true;
        }
        return false;
    }
    return false;
}

inline bool parseNumber(const std::string& source, size_t& position, Value& value) {
    const size_t start = position;
    if (position < source.size() && source[position] == '-') ++position;
    if (position >= source.size()) return false;
    if (source[position] == '0') {
        ++position;
    } else {
        if (!std::isdigit(static_cast<unsigned char>(source[position]))) return false;
        while (position < source.size() &&
               std::isdigit(static_cast<unsigned char>(source[position]))) {
            ++position;
        }
    }
    if (position < source.size() && source[position] == '.') {
        ++position;
        if (position >= source.size() ||
            !std::isdigit(static_cast<unsigned char>(source[position]))) {
            return false;
        }
        while (position < source.size() &&
               std::isdigit(static_cast<unsigned char>(source[position]))) {
            ++position;
        }
    }
    if (position < source.size() &&
        (source[position] == 'e' || source[position] == 'E')) {
        ++position;
        if (position < source.size() &&
            (source[position] == '+' || source[position] == '-')) {
            ++position;
        }
        if (position >= source.size() ||
            !std::isdigit(static_cast<unsigned char>(source[position]))) {
            return false;
        }
        while (position < source.size() &&
               std::isdigit(static_cast<unsigned char>(source[position]))) {
            ++position;
        }
    }
    value.kind = Value::Kind::Number;
    value.number = std::strtod(source.substr(start, position - start).c_str(), nullptr);
    return std::isfinite(value.number);
}

inline bool parseValue(const std::string& source, size_t& position, Value& value) {
    skipWhitespace(source, position);
    if (position >= source.size()) return false;
    if (source[position] == '"') {
        value.kind = Value::Kind::String;
        return parseString(source, position, value.text);
    }
    if (source[position] == '[') return parseArray(source, position, value);
    if (source[position] == '{') return parseObject(source, position, value);
    if (source.compare(position, 4, "null") == 0) {
        position += 4;
        value.kind = Value::Kind::Null;
        return true;
    }
    if (source.compare(position, 4, "true") == 0) {
        position += 4;
        value.kind = Value::Kind::Bool;
        value.boolean = true;
        return true;
    }
    if (source.compare(position, 5, "false") == 0) {
        position += 5;
        value.kind = Value::Kind::Bool;
        value.boolean = false;
        return true;
    }
    return parseNumber(source, position, value);
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
            for (const auto& entry : value.fields) {
                if (!first) out += ",";
                first = false;
                out += "\"" + escape(entry.first) + "\":" + stringify(entry.second);
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
