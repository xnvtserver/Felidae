#include "NativeCsv.h"
#include "../common/NativeJson.h"

#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Felidae::NativeJson::Value;

std::string operationName(const std::string& name) {
    const size_t separator = name.rfind(':');
    return separator == std::string::npos ? name : name.substr(separator + 1);
}

std::vector<std::string> parseLine(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char current = line[i];
        if (current == '"') {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"') {
                field.push_back('"');
                ++i;
            } else {
                quoted = !quoted;
            }
        } else if (current == ',' && !quoted) {
            fields.push_back(field);
            field.clear();
        } else {
            field.push_back(current);
        }
    }
    if (quoted) throw std::runtime_error("csv.parse found an unterminated quoted field");
    fields.push_back(field);
    return fields;
}

Value parseCsv(const std::string& text, const std::string& typeName) {
    std::istringstream input(text);
    std::string line;
    if (!std::getline(input, line)) {
        Value empty;
        empty.kind = Value::Kind::Array;
        return empty;
    }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const auto headers = parseLine(line);
    Value result;
    result.kind = Value::Kind::Array;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const auto fields = parseLine(line);
        if (fields.size() != headers.size()) {
            throw std::runtime_error("csv.parse row has a different field count than the header");
        }
        Value row;
        row.kind = Value::Kind::Object;
        if (!typeName.empty()) {
            row.fieldOrder.push_back("__type");
            row.fields.emplace("__type", Felidae::NativeJson::string(typeName));
        }
        for (size_t i = 0; i < headers.size(); ++i) {
            row.fieldOrder.push_back(headers[i]);
            row.fields.emplace(headers[i], Felidae::NativeJson::string(fields[i]));
        }
        result.items.push_back(std::move(row));
    }
    return result;
}

std::string scalarText(const Value& value) {
    return value.kind == Value::Kind::String ? value.text : Felidae::NativeJson::stringify(value);
}

std::string quoteCsv(const std::string& text) {
    if (text.find_first_of(",\"\r\n") == std::string::npos) return text;
    std::string result = "\"";
    for (char current : text) result += current == '"' ? "\"\"" : std::string(1, current);
    return result + "\"";
}

const Value& requireRows(const Value& args) {
    return Felidae::NativeJson::requireField(args, "data", Value::Kind::Array, "csv native call");
}

std::string rowsToCsv(const Value& rows) {
    if (rows.items.empty()) return "";
    if (rows.items.front().kind != Value::Kind::Object) {
        throw std::runtime_error("csv.toText expects object rows");
    }
    std::vector<std::string> headers;
    if (!rows.items.front().fieldOrder.empty()) {
        for (const auto& field : rows.items.front().fieldOrder) {
            if (field != "__type") headers.push_back(field);
        }
    } else {
        for (const auto& field : rows.items.front().fields) {
            if (field.first != "__type") headers.push_back(field.first);
        }
    }
    std::ostringstream out;
    for (size_t i = 0; i < headers.size(); ++i) {
        if (i) out << ',';
        out << quoteCsv(headers[i]);
    }
    out << '\n';
    for (const auto& row : rows.items) {
        if (row.kind != Value::Kind::Object) throw std::runtime_error("csv.toText expects object rows");
        for (size_t i = 0; i < headers.size(); ++i) {
            if (i) out << ',';
            const auto found = row.fields.find(headers[i]);
            if (found != row.fields.end()) out << quoteCsv(scalarText(found->second));
        }
        out << '\n';
    }
    return out.str();
}

Value dispatch(const std::string& functionName, const Value& args) {
    const std::string operation = operationName(functionName);
    if (operation == "parse" || operation == "toFacts") {
        const auto& data = Felidae::NativeJson::requireField(args, "data", Value::Kind::String, "csv." + operation);
        std::string typeName;
        if (operation == "toFacts") {
            typeName = Felidae::NativeJson::requireField(args, "type", Value::Kind::String, "csv.toFacts").text;
        }
        return parseCsv(data.text, typeName);
    }
    if (operation == "toText") return Felidae::NativeJson::string(rowsToCsv(requireRows(args)));
    if (operation == "toFelidaeFacts") {
        const auto& rows = requireRows(args);
        const std::string typeName =
            Felidae::NativeJson::requireField(args, "type", Value::Kind::String, "csv.toFelidaeFacts").text;
        std::ostringstream out;
        for (const auto& row : rows.items) {
            if (row.kind != Value::Kind::Object) throw std::runtime_error("csv.toFelidaeFacts expects object rows");
            out << typeName << "(";
            bool first = true;
            const auto& order = row.fieldOrder;
            for (const auto& fieldName : order) {
                if (fieldName == "__type") continue;
                const auto field = row.fields.find(fieldName);
                if (field == row.fields.end()) continue;
                if (!first) out << ", ";
                first = false;
                out << field->first << ": " << Felidae::NativeJson::stringify(field->second);
            }
            out << ")\n";
        }
        return Felidae::NativeJson::string(out.str());
    }
    const auto& rows = requireRows(args);
    Value result = rows;
    if (operation == "addRow") {
        const Value* row = Felidae::NativeJson::field(args, "row");
        if (!row || row->kind != Value::Kind::Object) throw std::runtime_error("csv.addRow expects object argument 'row'");
        result.items.push_back(*row);
        return result;
    }
    const std::string key =
        Felidae::NativeJson::requireField(args, "key", Value::Kind::String, "csv." + operation).text;
    const Value* expectedValue = Felidae::NativeJson::field(args, "value");
    if (!expectedValue) throw std::runtime_error("csv." + operation + " expects argument 'value'");
    const std::string expected = scalarText(*expectedValue);
    result.items.clear();
    for (Value row : rows.items) {
        if (row.kind != Value::Kind::Object) throw std::runtime_error("csv operation expects object rows");
        const auto found = row.fields.find(key);
        const bool match = found != row.fields.end() && scalarText(found->second) == expected;
        if (operation == "findRows" && match) result.items.push_back(std::move(row));
        else if (operation == "deleteRows" && !match) result.items.push_back(std::move(row));
        else if (operation == "updateRows") {
            if (match) {
                const Value* patch = Felidae::NativeJson::field(args, "patch");
                if (!patch || patch->kind != Value::Kind::Object) {
                    throw std::runtime_error("csv.updateRows expects object argument 'patch'");
                }
                for (const auto& entry : patch->fields) row.fields[entry.first] = entry.second;
            }
            result.items.push_back(std::move(row));
        }
    }
    if (operation == "findRows" || operation == "deleteRows" || operation == "updateRows") return result;
    throw std::runtime_error("Unsupported CSV native function '" + functionName + "'");
}

} // namespace

extern "C" FELIDAE_CSV_EXPORT char* felidae_native_call(const char* functionName,
                                                         const char* argsJson) {
    try {
        const auto args = Felidae::NativeJson::parse(argsJson, "CSV native module");
        return Felidae::NativeJson::copyResponse(
            Felidae::NativeJson::stringify(dispatch(functionName ? functionName : "", args)));
    } catch (const std::exception& error) {
        return Felidae::NativeJson::copyResponse(
            Felidae::NativeJson::stringify(Felidae::NativeJson::error(error.what())));
    } catch (...) {
        return Felidae::NativeJson::copyResponse("{\"error\":\"Unknown CSV native module failure\"}");
    }
}

extern "C" FELIDAE_CSV_EXPORT void felidae_native_free(char* value) {
    std::free(value);
}
