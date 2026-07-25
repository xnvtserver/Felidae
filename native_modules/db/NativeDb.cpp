#include "../common/NativeJson.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#define FELIDAE_DB_EXPORT __declspec(dllexport)
#else
#define FELIDAE_DB_EXPORT
#endif

namespace {
using Felidae::NativeJson::Value;
namespace fs = std::filesystem;

std::mutex databaseMutex;

class DatabaseFileLock {
public:
    explicit DatabaseFileLock(const fs::path& database) {
        fs::path lockPath = database;
        lockPath += ".lock";
        if (lockPath.has_parent_path()) fs::create_directories(lockPath.parent_path());
#ifdef _WIN32
        handle_ = CreateFileW(
            lockPath.wstring().c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            throw std::runtime_error("Database is busy: '" + database.string() + "'");
        }
#else
        descriptor_ = ::open(lockPath.c_str(), O_CREAT | O_RDWR, 0666);
        if (descriptor_ < 0 || flock(descriptor_, LOCK_EX | LOCK_NB) != 0) {
            if (descriptor_ >= 0) ::close(descriptor_);
            descriptor_ = -1;
            throw std::runtime_error("Database is busy: '" + database.string() + "'");
        }
#endif
    }

    ~DatabaseFileLock() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
#else
        if (descriptor_ >= 0) {
            flock(descriptor_, LOCK_UN);
            ::close(descriptor_);
        }
#endif
    }

    DatabaseFileLock(const DatabaseFileLock&) = delete;
    DatabaseFileLock& operator=(const DatabaseFileLock&) = delete;

private:
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int descriptor_ = -1;
#endif
};

const Value& fieldAs(const Value& object, const std::string& name, Value::Kind kind) {
    return Felidae::NativeJson::requireField(object, name, kind, "db native call");
}

const Value& rows(const Value& args) { return fieldAs(args, "rows", Value::Kind::Array); }

std::string text(const Value& value) {
    return value.kind == Value::Kind::String ? value.text : Felidae::NativeJson::stringify(value);
}

bool equal(const Value& left, const Value& right) {
    return left.kind == Value::Kind::Number && right.kind == Value::Kind::Number
        ? std::fabs(left.number - right.number) < 1e-12
        : text(left) == text(right);
}

Value numeric(double number) {
    Value result;
    result.kind = Value::Kind::Number;
    result.number = number;
    return result;
}

Value boolean(bool input) {
    Value result;
    result.kind = Value::Kind::Bool;
    result.boolean = input;
    return result;
}

Value stringValue(std::string input) {
    Value result;
    result.kind = Value::Kind::String;
    result.text = std::move(input);
    return result;
}

Value nullValue() { return Value{}; }

Value objectValue(std::initializer_list<std::pair<std::string, Value>> fields) {
    Value result;
    result.kind = Value::Kind::Object;
    for (const auto& field : fields) {
        result.fieldOrder.push_back(field.first);
        result.fields.emplace(field.first, field.second);
    }
    return result;
}

std::string indentation(size_t width) { return std::string(width, ' '); }

std::string trimmed(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {
        return std::isspace(c) != 0;
    }).base();
    return first < last ? std::string(first, last) : std::string();
}

std::string felidaeValue(const Value& value, size_t indent = 0) {
    if (value.kind == Value::Kind::Null) return "nil";
    if (value.kind == Value::Kind::Array) {
        std::ostringstream output;
        if (value.items.empty()) return "[]";
        output << "[\n";
        for (size_t i = 0; i < value.items.size(); ++i) {
            output << indentation(indent + 4) << felidaeValue(value.items[i], indent + 4);
            if (i + 1 < value.items.size()) output << ",";
            output << "\n";
        }
        output << indentation(indent) << "]";
        return output.str();
    }
    if (value.kind == Value::Kind::Object) {
        std::ostringstream output;
        output << "{\n";
        std::vector<std::string> names;
        for (const auto& name : value.fieldOrder) {
            if (name == "__type") continue;
            const auto found = value.fields.find(name);
            if (found == value.fields.end()) continue;
            names.push_back(name);
        }
        for (size_t i = 0; i < names.size(); ++i) {
            output << indentation(indent + 4) << names[i] << ": "
                   << felidaeValue(value.fields.at(names[i]), indent + 4);
            if (i + 1 < names.size()) output << ",";
            output << "\n";
        }
        output << indentation(indent) << "}";
        return output.str();
    }
    return Felidae::NativeJson::stringify(value);
}

Value filter(const Value& source, const std::string& key, const Value& expected, bool keepMatches) {
    Value result;
    result.kind = Value::Kind::Array;
    for (const auto& row : source.items) {
        if (row.kind != Value::Kind::Object) throw std::runtime_error("db expects fact/document rows");
        const auto found = row.fields.find(key);
        const bool match = found != row.fields.end() && equal(found->second, expected);
        if (match == keepMatches) result.items.push_back(row);
    }
    return result;
}

std::string serializeFacts(const Value& source, const std::string& type) {
    if (type.empty()) throw std::runtime_error("db model cannot be empty");
    std::ostringstream output;
    for (const auto& row : source.items) {
        if (row.kind != Value::Kind::Object) throw std::runtime_error("db persistence expects document rows");
        output << type << "(\n";
        std::vector<std::string> names;
        for (const auto& name : row.fieldOrder) {
            if (name == "__type") continue;
            if (row.fields.find(name) != row.fields.end()) names.push_back(name);
        }
        for (size_t i = 0; i < names.size(); ++i) {
            output << "    " << names[i] << ": " << felidaeValue(row.fields.at(names[i]), 4);
            if (i + 1 < names.size()) output << ",";
            output << "\n";
        }
        output << ")\n";
    }
    return output.str();
}

std::string mergeModelFacts(const std::string& source,
                            const std::string& model,
                            const std::string& replacement) {
    std::string kept;
    size_t position = 0;
    while (position < source.size()) {
        const size_t lineStart = position;
        size_t token = lineStart;
        while (token < source.size() && (source[token] == ' ' || source[token] == '\t')) ++token;
        const bool modelStart =
            source.compare(token, model.size(), model) == 0 &&
            token + model.size() < source.size() &&
            source[token + model.size()] == '(';
        if (!modelStart) {
            const size_t lineEnd = source.find('\n', position);
            if (lineEnd == std::string::npos) {
                kept.append(source, position, std::string::npos);
                break;
            }
            kept.append(source, position, lineEnd - position + 1);
            position = lineEnd + 1;
            continue;
        }
        size_t cursor = token + model.size();
        int depth = 0;
        bool quoted = false;
        bool escaped = false;
        for (; cursor < source.size(); ++cursor) {
            const char current = source[cursor];
            if (quoted) {
                if (escaped) escaped = false;
                else if (current == '\\') escaped = true;
                else if (current == '"') quoted = false;
                continue;
            }
            if (current == '"') quoted = true;
            else if (current == '(') ++depth;
            else if (current == ')' && --depth == 0) {
                ++cursor;
                break;
            }
        }
        while (cursor < source.size() && (source[cursor] == '\r' || source[cursor] == '\n')) ++cursor;
        while (!kept.empty() && std::isspace(static_cast<unsigned char>(kept.back())) != 0) kept.pop_back();
        if (!kept.empty()) kept.push_back('\n');
        position = cursor;
    }
    while (!kept.empty() && std::isspace(static_cast<unsigned char>(kept.back())) != 0) kept.pop_back();
    if (!kept.empty() && !replacement.empty()) kept += "\n\n";
    kept += replacement;
    return kept;
}

class FactValueParser {
public:
    explicit FactValueParser(std::string_view source) : source_(source) {}

    Value parseFields() {
        Value object;
        object.kind = Value::Kind::Object;
        skip();
        while (position_ < source_.size()) {
            const std::string name = identifier();
            skip();
            expect(':');
            Value value = parseValue();
            object.fieldOrder.push_back(name);
            object.fields.emplace(name, std::move(value));
            skip();
            if (position_ >= source_.size()) break;
            expect(',');
            skip();
        }
        return object;
    }

private:
    std::string_view source_;
    size_t position_ = 0;

    void skip() {
        while (position_ < source_.size() &&
               std::isspace(static_cast<unsigned char>(source_[position_])) != 0) ++position_;
    }

    void expect(char expected) {
        skip();
        if (position_ >= source_.size() || source_[position_] != expected) {
            throw std::runtime_error(std::string("Malformed persisted fact: expected '") + expected + "'");
        }
        ++position_;
    }

    std::string identifier() {
        skip();
        const size_t start = position_;
        while (position_ < source_.size()) {
            const unsigned char current = static_cast<unsigned char>(source_[position_]);
            if (std::isalnum(current) == 0 && current != '_' && current != '-') break;
            ++position_;
        }
        if (start == position_) throw std::runtime_error("Malformed persisted fact: expected field name");
        return std::string(source_.substr(start, position_ - start));
    }

    Value parseString() {
        const size_t start = position_++;
        bool escaped = false;
        while (position_ < source_.size()) {
            const char current = source_[position_++];
            if (escaped) escaped = false;
            else if (current == '\\') escaped = true;
            else if (current == '"') {
                const std::string encoded(source_.substr(start, position_ - start));
                return Felidae::NativeJson::parse(encoded.c_str(), "persisted fact string");
            }
        }
        throw std::runtime_error("Malformed persisted fact: unterminated string");
    }

    Value parseArray() {
        expect('[');
        Value array;
        array.kind = Value::Kind::Array;
        skip();
        while (position_ < source_.size() && source_[position_] != ']') {
            array.items.push_back(parseValue());
            skip();
            if (position_ < source_.size() && source_[position_] == ',') {
                ++position_;
                skip();
            } else {
                break;
            }
        }
        expect(']');
        return array;
    }

    Value parseObject() {
        expect('{');
        const size_t start = position_;
        int depth = 1;
        bool quoted = false;
        bool escaped = false;
        while (position_ < source_.size()) {
            const char current = source_[position_++];
            if (quoted) {
                if (escaped) escaped = false;
                else if (current == '\\') escaped = true;
                else if (current == '"') quoted = false;
            } else if (current == '"') {
                quoted = true;
            } else if (current == '{') {
                ++depth;
            } else if (current == '}' && --depth == 0) {
                return FactValueParser(source_.substr(start, position_ - start - 1)).parseFields();
            }
        }
        throw std::runtime_error("Malformed persisted fact: unterminated object");
    }

    Value parseValue() {
        skip();
        if (position_ >= source_.size()) throw std::runtime_error("Malformed persisted fact: missing value");
        if (source_[position_] == '"') return parseString();
        if (source_[position_] == '[') return parseArray();
        if (source_[position_] == '{') return parseObject();
        const size_t start = position_;
        while (position_ < source_.size() && source_[position_] != ',' &&
               source_[position_] != ']' && source_[position_] != '}') ++position_;
        std::string token = trimmed(std::string(source_.substr(start, position_ - start)));
        if (token == "nil" || token == "null") return nullValue();
        if (token == "true") return boolean(true);
        if (token == "false") return boolean(false);
        char* end = nullptr;
        const double number = std::strtod(token.c_str(), &end);
        if (end && *end == '\0' && end != token.c_str()) return numeric(number);
        throw std::runtime_error("Malformed persisted fact value '" + token + "'");
    }
};

Value loadModelFacts(const std::string& source, const std::string& model) {
    Value result;
    result.kind = Value::Kind::Array;
    size_t position = 0;
    while (position < source.size()) {
        size_t token = position;
        while (token < source.size() && (source[token] == ' ' || source[token] == '\t')) ++token;
        const bool modelStart = source.compare(token, model.size(), model) == 0 &&
            token + model.size() < source.size() && source[token + model.size()] == '(';
        if (!modelStart) {
            const size_t next = source.find('\n', position);
            if (next == std::string::npos) break;
            position = next + 1;
            continue;
        }
        const size_t bodyStart = token + model.size() + 1;
        size_t cursor = bodyStart;
        int parens = 1;
        bool quoted = false;
        bool escaped = false;
        for (; cursor < source.size(); ++cursor) {
            const char current = source[cursor];
            if (quoted) {
                if (escaped) escaped = false;
                else if (current == '\\') escaped = true;
                else if (current == '"') quoted = false;
            } else if (current == '"') {
                quoted = true;
            } else if (current == '(') {
                ++parens;
            } else if (current == ')' && --parens == 0) {
                break;
            }
        }
        if (cursor >= source.size()) throw std::runtime_error("Unterminated " + model + " fact");
        Value row = FactValueParser(
            std::string_view(source).substr(bodyStart, cursor - bodyStart)).parseFields();
        row.fieldOrder.insert(row.fieldOrder.begin(), "__type");
        row.fields.emplace("__type", stringValue(model));
        result.items.push_back(std::move(row));
        position = cursor + 1;
    }
    return result;
}

bool matchesConditions(const Value& row, const Value& conditions) {
    if (row.kind != Value::Kind::Object || conditions.kind != Value::Kind::Object) return false;
    for (const auto& condition : conditions.fields) {
        const auto found = row.fields.find(condition.first);
        if (found == row.fields.end() || !equal(found->second, condition.second)) return false;
    }
    return true;
}

std::string readDatabaseFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read database '" + path.string() + "'");
    std::ostringstream content;
    content << input.rdbuf();
    return content.str();
}

void atomicWriteDatabase(const fs::path& path, const std::string& content) {
    if (path.has_parent_path()) fs::create_directories(path.parent_path());
    fs::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("Cannot write temporary database '" + temporary.string() + "'");
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        output.flush();
        if (!output) throw std::runtime_error("Failed writing temporary database '" + temporary.string() + "'");
    }
#ifdef _WIN32
    bool replaced = false;
    for (int attempt = 0; attempt < 20 && !replaced; ++attempt) {
        replaced = MoveFileExW(
            temporary.wstring().c_str(),
            path.wstring().c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
        if (!replaced) Sleep(10);
    }
    if (!replaced) {
        fs::remove(temporary);
        throw std::runtime_error("Cannot atomically replace database '" + path.string() + "'");
    }
#else
    if (std::rename(temporary.string().c_str(), path.string().c_str()) != 0) {
        fs::remove(temporary);
        throw std::runtime_error("Cannot atomically replace database '" + path.string() + "'");
    }
#endif
}

Value mutationResult(size_t matched, size_t modified, size_t inserted, size_t deleted,
                     Value data, Value error = nullValue()) {
    return objectValue({
        {"matched", numeric(static_cast<double>(matched))},
        {"modified", numeric(static_cast<double>(modified))},
        {"inserted", numeric(static_cast<double>(inserted))},
        {"deleted", numeric(static_cast<double>(deleted))},
        {"data", std::move(data)},
        {"problem", std::move(error)}
    });
}

Value dispatch(const std::string& function, const Value& args) {
    const auto separator = function.rfind(':');
    const std::string operation = separator == std::string::npos ? function : function.substr(separator + 1);
    const Value& source = rows(args);

    if (operation == "connect") {
        const fs::path path(fieldAs(args, "path", Value::Kind::String).text);
        const Value& models = fieldAs(args, "models", Value::Kind::Array);
        std::lock_guard<std::mutex> lock(databaseMutex);
        DatabaseFileLock fileLock(path);
        if (!fs::exists(path)) {
            std::ostringstream schema;
            schema << "# Felidae fact database\n# models:";
            for (const auto& model : models.items) {
                const std::string name =
                    model.kind == Value::Kind::String ? trimmed(model.text) : std::string();
                if (name.empty()) throw std::runtime_error("db.connect models must contain names");
                schema << " " << name;
            }
            schema << "\n";
            atomicWriteDatabase(path, schema.str());
        }
        return objectValue({{"path", stringValue(path.string())}, {"models", models}});
    }
    if (operation == "findManyFile" || operation == "findOneFile") {
        const fs::path path(fieldAs(args, "path", Value::Kind::String).text);
        const std::string type = trimmed(fieldAs(args, "type", Value::Kind::String).text);
        const Value& conditions = fieldAs(args, "conditions", Value::Kind::Object);
        std::lock_guard<std::mutex> lock(databaseMutex);
        DatabaseFileLock fileLock(path);
        const Value current = loadModelFacts(readDatabaseFile(path), type);
        Value found;
        found.kind = Value::Kind::Array;
        for (const auto& row : current.items) {
            if (matchesConditions(row, conditions)) {
                found.items.push_back(row);
                if (operation == "findOneFile") break;
            }
        }
        if (operation == "findOneFile") {
            const size_t matched = found.items.size();
            Value data = found.items.empty() ? nullValue() : found.items.front();
            return mutationResult(matched, 0, 0, 0, std::move(data));
        }
        const size_t matched = found.items.size();
        return mutationResult(matched, 0, 0, 0, std::move(found));
    }
    if (operation == "insertOneFile") {
        const fs::path path(fieldAs(args, "path", Value::Kind::String).text);
        const std::string type = trimmed(fieldAs(args, "type", Value::Kind::String).text);
        const std::string key = fieldAs(args, "key", Value::Kind::String).text;
        const Value& data = fieldAs(args, "data", Value::Kind::Object);
        const auto identity = data.fields.find(key);
        if (identity == data.fields.end()) throw std::runtime_error("db.insertOne data is missing key '" + key + "'");
        std::lock_guard<std::mutex> lock(databaseMutex);
        DatabaseFileLock fileLock(path);
        const std::string existing = readDatabaseFile(path);
        Value current = loadModelFacts(existing, type);
        for (const auto& row : current.items) {
            const auto found = row.fields.find(key);
            if (found != row.fields.end() && equal(found->second, identity->second)) {
                return mutationResult(1, 0, 0, 0, row, stringValue("DuplicateKey"));
            }
        }
        Value stored = data;
        if (stored.fields.find("__type") == stored.fields.end()) {
            stored.fieldOrder.insert(stored.fieldOrder.begin(), "__type");
            stored.fields.emplace("__type", stringValue(type));
        }
        current.items.push_back(stored);
        atomicWriteDatabase(path, mergeModelFacts(existing, type, serializeFacts(current, type)));
        return mutationResult(0, 0, 1, 0, std::move(stored));
    }
    if (operation == "updateOneFile") {
        const fs::path path(fieldAs(args, "path", Value::Kind::String).text);
        const std::string type = trimmed(fieldAs(args, "type", Value::Kind::String).text);
        const Value& conditions = fieldAs(args, "conditions", Value::Kind::Object);
        const Value& patch = fieldAs(args, "patch", Value::Kind::Object);
        std::lock_guard<std::mutex> lock(databaseMutex);
        DatabaseFileLock fileLock(path);
        const std::string existing = readDatabaseFile(path);
        Value current = loadModelFacts(existing, type);
        std::vector<size_t> matches;
        for (size_t i = 0; i < current.items.size(); ++i) {
            if (matchesConditions(current.items[i], conditions)) matches.push_back(i);
        }
        if (matches.size() > 1) {
            return mutationResult(matches.size(), 0, 0, 0, nullValue(), stringValue("MultipleMatches"));
        }
        if (matches.empty()) return mutationResult(0, 0, 0, 0, nullValue(), stringValue("NotFound"));
        Value& row = current.items[matches.front()];
        for (const auto& name : patch.fieldOrder) {
            if (name == "__type") continue;
            const auto item = patch.fields.find(name);
            if (item == patch.fields.end()) continue;
            if (row.fields.find(name) == row.fields.end()) row.fieldOrder.push_back(name);
            row.fields[name] = item->second;
        }
        atomicWriteDatabase(path, mergeModelFacts(existing, type, serializeFacts(current, type)));
        return mutationResult(1, 1, 0, 0, row);
    }
    if (operation == "deleteOneFile") {
        const fs::path path(fieldAs(args, "path", Value::Kind::String).text);
        const std::string type = trimmed(fieldAs(args, "type", Value::Kind::String).text);
        const Value& conditions = fieldAs(args, "conditions", Value::Kind::Object);
        std::lock_guard<std::mutex> lock(databaseMutex);
        DatabaseFileLock fileLock(path);
        const std::string existing = readDatabaseFile(path);
        Value current = loadModelFacts(existing, type);
        std::vector<size_t> matches;
        for (size_t i = 0; i < current.items.size(); ++i) {
            if (matchesConditions(current.items[i], conditions)) matches.push_back(i);
        }
        if (matches.size() > 1) {
            return mutationResult(matches.size(), 0, 0, 0, nullValue(), stringValue("MultipleMatches"));
        }
        if (matches.empty()) return mutationResult(0, 0, 0, 0, nullValue(), stringValue("NotFound"));
        Value deleted = current.items[matches.front()];
        current.items.erase(current.items.begin() + static_cast<std::ptrdiff_t>(matches.front()));
        atomicWriteDatabase(path, mergeModelFacts(existing, type, serializeFacts(current, type)));
        return mutationResult(1, 0, 0, 1, std::move(deleted));
    }

    if (operation == "add") {
        const Value& row = fieldAs(args, "row", Value::Kind::Object);
        Value result = source;
        result.items.push_back(row);
        return result;
    }
    if (operation == "find" || operation == "remove") {
        const std::string key = fieldAs(args, "field", Value::Kind::String).text;
        const Value* expected = Felidae::NativeJson::field(args, "equals");
        if (!expected) throw std::runtime_error("db." + operation + " expects 'equals'");
        return filter(source, key, *expected, operation == "find");
    }
    if (operation == "findMany") {
        const Value& conditions = fieldAs(args, "conditions", Value::Kind::Object);
        Value result;
        result.kind = Value::Kind::Array;
        for (const auto& row : source.items) {
            if (row.kind != Value::Kind::Object) throw std::runtime_error("db.findMany expects document rows");
            bool matches = true;
            for (const auto& condition : conditions.fields) {
                const auto found = row.fields.find(condition.first);
                if (found == row.fields.end() || !equal(found->second, condition.second)) {
                    matches = false;
                    break;
                }
            }
            if (matches) result.items.push_back(row);
        }
        return result;
    }
    if (operation == "update") {
        const std::string key = fieldAs(args, "field", Value::Kind::String).text;
        const Value* expected = Felidae::NativeJson::field(args, "equals");
        const Value& patch = fieldAs(args, "patch", Value::Kind::Object);
        if (!expected) throw std::runtime_error("db.update expects 'equals'");
        Value result = source;
        for (auto& row : result.items) {
            const auto found = row.fields.find(key);
            if (found == row.fields.end() || !equal(found->second, *expected)) continue;
            for (const auto& patchKey : patch.fieldOrder) {
                const auto item = patch.fields.find(patchKey);
                if (item == patch.fields.end()) continue;
                if (row.fields.find(patchKey) == row.fields.end()) row.fieldOrder.push_back(patchKey);
                row.fields[patchKey] = item->second;
            }
        }
        return result;
    }
    if (operation == "sort") {
        const std::string key = fieldAs(args, "field", Value::Kind::String).text;
        const std::string direction = fieldAs(args, "direction", Value::Kind::String).text;
        if (direction != "asc" && direction != "desc") throw std::runtime_error("db.sort direction must be 'asc' or 'desc'");
        Value result = source;
        for (const auto& row : result.items) {
            if (row.kind != Value::Kind::Object || row.fields.find(key) == row.fields.end()) {
                throw std::runtime_error("db.sort field '" + key + "' is missing");
            }
        }
        std::stable_sort(result.items.begin(), result.items.end(), [&](const Value& a, const Value& b) {
            const Value& left = a.fields.at(key);
            const Value& right = b.fields.at(key);
            const bool less = left.kind == Value::Kind::Number && right.kind == Value::Kind::Number
                ? left.number < right.number : text(left) < text(right);
            return direction == "asc" ? less : (!less && !equal(left, right));
        });
        return result;
    }
    if (operation == "search") {
        const std::string key = fieldAs(args, "field", Value::Kind::String).text;
        std::string query = fieldAs(args, "query", Value::Kind::String).text;
        std::transform(query.begin(), query.end(), query.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        Value result;
        result.kind = Value::Kind::Array;
        for (const auto& row : source.items) {
            const auto found = row.fields.find(key);
            if (found == row.fields.end()) continue;
            std::string value = text(found->second);
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (value.find(query) != std::string::npos) result.items.push_back(row);
        }
        return result;
    }
    if (operation == "distinct") {
        const std::string key = fieldAs(args, "field", Value::Kind::String).text;
        Value result;
        result.kind = Value::Kind::Array;
        std::set<std::string> seen;
        for (const auto& row : source.items) {
            const auto found = row.fields.find(key);
            if (found != row.fields.end() && seen.insert(text(found->second)).second) result.items.push_back(found->second);
        }
        return result;
    }
    if (operation == "paginate") {
        const double offset = fieldAs(args, "offset", Value::Kind::Number).number;
        const double limit = fieldAs(args, "limit", Value::Kind::Number).number;
        if (offset < 0 || limit < 0 || std::floor(offset) != offset || std::floor(limit) != limit) {
            throw std::runtime_error("db.paginate offset and limit must be non-negative integers");
        }
        Value result;
        result.kind = Value::Kind::Array;
        const size_t begin = std::min(source.items.size(), static_cast<size_t>(offset));
        const size_t end = std::min(source.items.size(), begin + static_cast<size_t>(limit));
        result.items.assign(source.items.begin() + static_cast<std::ptrdiff_t>(begin),
                            source.items.begin() + static_cast<std::ptrdiff_t>(end));
        return result;
    }
    if (operation == "aggregate") {
        const std::string mode = fieldAs(args, "operation", Value::Kind::String).text;
        if (mode == "count") return numeric(static_cast<double>(source.items.size()));
        const std::string key = fieldAs(args, "field", Value::Kind::String).text;
        if (mode != "sum" && mode != "average" && mode != "min" && mode != "max") {
            throw std::runtime_error("db.aggregate operation must be count, sum, average, min, or max");
        }
        if (source.items.empty()) throw std::runtime_error("db.aggregate cannot aggregate an empty database");
        double sum = 0, minimum = 0, maximum = 0;
        for (size_t i = 0; i < source.items.size(); ++i) {
            const auto found = source.items[i].fields.find(key);
            if (found == source.items[i].fields.end() || found->second.kind != Value::Kind::Number) {
                throw std::runtime_error("db.aggregate field '" + key + "' must be numeric in every document");
            }
            const double value = found->second.number;
            sum += value;
            if (i == 0) minimum = maximum = value;
            else { minimum = std::min(minimum, value); maximum = std::max(maximum, value); }
        }
        if (mode == "sum") return numeric(sum);
        if (mode == "average") return numeric(sum / source.items.size());
        return numeric(mode == "min" ? minimum : maximum);
    }
    if (operation == "serialize") {
        const std::string type = trimmed(fieldAs(args, "type", Value::Kind::String).text);
        return Felidae::NativeJson::string(serializeFacts(source, type));
    }
    if (operation == "merge") {
        const std::string type = trimmed(fieldAs(args, "type", Value::Kind::String).text);
        const std::string existing = fieldAs(args, "source", Value::Kind::String).text;
        return Felidae::NativeJson::string(
            mergeModelFacts(existing, type, serializeFacts(source, type)));
    }
    if (operation == "schema") {
        const Value& models = fieldAs(args, "models", Value::Kind::Array);
        std::ostringstream output;
        output << "# Felidae multi-model fact database\n# models:";
        for (const auto& model : models.items) {
            const std::string name = model.kind == Value::Kind::String ? trimmed(model.text) : std::string();
            if (name.empty()) {
                throw std::runtime_error("db.create models must contain non-empty strings");
            }
            output << " " << name;
        }
        output << "\n";
        return Felidae::NativeJson::string(output.str());
    }
    if (operation == "deleteCascade") {
        const Value& dependents = fieldAs(args, "dependents", Value::Kind::Array);
        const std::string parentField = fieldAs(args, "parentField", Value::Kind::String).text;
        const std::string dependentField = fieldAs(args, "dependentField", Value::Kind::String).text;
        const Value* expected = Felidae::NativeJson::field(args, "equals");
        if (!expected) throw std::runtime_error("db.deleteCascade expects 'equals'");
        Value result;
        result.kind = Value::Kind::Object;
        result.fieldOrder = {"parents", "dependents"};
        result.fields.emplace("parents", filter(source, parentField, *expected, false));
        result.fields.emplace("dependents", filter(dependents, dependentField, *expected, false));
        return result;
    }
    throw std::runtime_error("Unsupported db operation '" + operation + "'");
}
} // namespace

extern "C" FELIDAE_DB_EXPORT char* felidae_native_call(const char* function, const char* argsJson) {
    try {
        const Value args = Felidae::NativeJson::parse(argsJson, "DB native module");
        return Felidae::NativeJson::copyResponse(Felidae::NativeJson::stringify(dispatch(function ? function : "", args)));
    } catch (const std::exception& error) {
        return Felidae::NativeJson::copyResponse(Felidae::NativeJson::stringify(Felidae::NativeJson::error(error.what())));
    }
}

extern "C" FELIDAE_DB_EXPORT void felidae_native_free(char* value) { std::free(value); }
