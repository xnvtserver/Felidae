#include "NativeCsv.h"
#include "../common/NativeJson.h"

#include <algorithm>
#include <cctype>
#include <cmath>
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

bool valuesEqual(const Value& left, const Value& right) {
    if (left.kind == Value::Kind::Number && right.kind == Value::Kind::Number) {
        return std::fabs(left.number - right.number) < 1e-12;
    }
    return scalarText(left) == scalarText(right);
}

Value number(double value) {
    Value result;
    result.kind = Value::Kind::Number;
    result.number = value;
    return result;
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
    if (operation == "findMany") {
        const Value& conditions =
            Felidae::NativeJson::requireField(args, "conditions", Value::Kind::Object, "csv.findMany");
        result.items.clear();
        for (const auto& row : rows.items) {
            if (row.kind != Value::Kind::Object) throw std::runtime_error("csv.findMany expects object rows");
            bool matches = true;
            for (const auto& condition : conditions.fields) {
                const auto found = row.fields.find(condition.first);
                if (found == row.fields.end() || !valuesEqual(found->second, condition.second)) {
                    matches = false;
                    break;
                }
            }
            if (matches) result.items.push_back(row);
        }
        return result;
    }
    if (operation == "sortRows") {
        const std::string key =
            Felidae::NativeJson::requireField(args, "key", Value::Kind::String, "csv.sortRows").text;
        const std::string direction =
            Felidae::NativeJson::requireField(args, "direction", Value::Kind::String, "csv.sortRows").text;
        if (direction != "asc" && direction != "desc") {
            throw std::runtime_error("csv.sortRows direction must be 'asc' or 'desc'");
        }
        for (const auto& row : result.items) {
            if (row.kind != Value::Kind::Object || row.fields.find(key) == row.fields.end()) {
                throw std::runtime_error("csv.sortRows field '" + key + "' is missing");
            }
        }
        std::stable_sort(result.items.begin(), result.items.end(), [&](const Value& left, const Value& right) {
            const Value& a = left.fields.at(key);
            const Value& b = right.fields.at(key);
            bool less = a.kind == Value::Kind::Number && b.kind == Value::Kind::Number
                ? a.number < b.number
                : scalarText(a) < scalarText(b);
            return direction == "asc" ? less : !less && !valuesEqual(a, b);
        });
        return result;
    }
    if (operation == "searchRows") {
        const std::string key =
            Felidae::NativeJson::requireField(args, "key", Value::Kind::String, "csv.searchRows").text;
        std::string query =
            Felidae::NativeJson::requireField(args, "query", Value::Kind::String, "csv.searchRows").text;
        std::transform(query.begin(), query.end(), query.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        result.items.clear();
        for (const auto& row : rows.items) {
            if (row.kind != Value::Kind::Object) throw std::runtime_error("csv.searchRows expects object rows");
            const auto found = row.fields.find(key);
            if (found == row.fields.end()) continue;
            std::string text = scalarText(found->second);
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (text.find(query) != std::string::npos) result.items.push_back(row);
        }
        return result;
    }
    if (operation == "distinct") {
        const std::string key =
            Felidae::NativeJson::requireField(args, "key", Value::Kind::String, "csv.distinct").text;
        result.items.clear();
        std::set<std::string> seen;
        for (const auto& row : rows.items) {
            if (row.kind != Value::Kind::Object) throw std::runtime_error("csv.distinct expects object rows");
            const auto found = row.fields.find(key);
            if (found != row.fields.end() && seen.insert(scalarText(found->second)).second) {
                result.items.push_back(found->second);
            }
        }
        return result;
    }
    if (operation == "paginate") {
        const Value& offsetValue =
            Felidae::NativeJson::requireField(args, "offset", Value::Kind::Number, "csv.paginate");
        const Value& limitValue =
            Felidae::NativeJson::requireField(args, "limit", Value::Kind::Number, "csv.paginate");
        if (offsetValue.number < 0 || limitValue.number < 0 ||
            std::floor(offsetValue.number) != offsetValue.number ||
            std::floor(limitValue.number) != limitValue.number) {
            throw std::runtime_error("csv.paginate offset and limit must be non-negative integers");
        }
        const size_t begin = std::min(rows.items.size(), static_cast<size_t>(offsetValue.number));
        const size_t end = std::min(rows.items.size(), begin + static_cast<size_t>(limitValue.number));
        result.items.assign(rows.items.begin() + static_cast<std::ptrdiff_t>(begin),
                            rows.items.begin() + static_cast<std::ptrdiff_t>(end));
        return result;
    }
    if (operation == "aggregate") {
        const std::string aggregate =
            Felidae::NativeJson::requireField(args, "operation", Value::Kind::String, "csv.aggregate").text;
        if (aggregate == "count") return number(static_cast<double>(rows.items.size()));
        const std::string key =
            Felidae::NativeJson::requireField(args, "key", Value::Kind::String, "csv.aggregate").text;
        if (aggregate != "sum" && aggregate != "average" && aggregate != "min" && aggregate != "max") {
            throw std::runtime_error("csv.aggregate operation must be count, sum, average, min, or max");
        }
        if (rows.items.empty()) throw std::runtime_error("csv.aggregate cannot aggregate an empty collection");
        double total = 0.0;
        double minimum = 0.0;
        double maximum = 0.0;
        bool firstNumber = true;
        for (const auto& row : rows.items) {
            if (row.kind != Value::Kind::Object) throw std::runtime_error("csv.aggregate expects object rows");
            const auto found = row.fields.find(key);
            if (found == row.fields.end() || found->second.kind != Value::Kind::Number) {
                throw std::runtime_error("csv.aggregate field '" + key + "' must be numeric in every row");
            }
            const double current = found->second.number;
            total += current;
            if (firstNumber) minimum = maximum = current;
            else {
                minimum = std::min(minimum, current);
                maximum = std::max(maximum, current);
            }
            firstNumber = false;
        }
        if (aggregate == "sum") return number(total);
        if (aggregate == "average") return number(total / static_cast<double>(rows.items.size()));
        return number(aggregate == "min" ? minimum : maximum);
    }
    if (operation == "deleteCascade") {
        const Value& dependents =
            Felidae::NativeJson::requireField(args, "dependents", Value::Kind::Array, "csv.deleteCascade");
        const std::string parentKey =
            Felidae::NativeJson::requireField(args, "parentKey", Value::Kind::String, "csv.deleteCascade").text;
        const std::string dependentKey =
            Felidae::NativeJson::requireField(args, "dependentKey", Value::Kind::String, "csv.deleteCascade").text;
        const Value* expected = Felidae::NativeJson::field(args, "value");
        if (!expected) throw std::runtime_error("csv.deleteCascade expects argument 'value'");
        Value keptParents;
        keptParents.kind = Value::Kind::Array;
        Value keptDependents;
        keptDependents.kind = Value::Kind::Array;
        for (const auto& row : rows.items) {
            const auto found = row.fields.find(parentKey);
            if (found == row.fields.end() || !valuesEqual(found->second, *expected)) keptParents.items.push_back(row);
        }
        for (const auto& row : dependents.items) {
            if (row.kind != Value::Kind::Object) throw std::runtime_error("csv.deleteCascade expects object dependent rows");
            const auto found = row.fields.find(dependentKey);
            if (found == row.fields.end() || !valuesEqual(found->second, *expected)) keptDependents.items.push_back(row);
        }
        Value cascade;
        cascade.kind = Value::Kind::Object;
        cascade.fieldOrder = {"parents", "dependents"};
        cascade.fields.emplace("parents", std::move(keptParents));
        cascade.fields.emplace("dependents", std::move(keptDependents));
        return cascade;
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
                for (const auto& fieldName : patch->fieldOrder) {
                    const auto entry = patch->fields.find(fieldName);
                    if (entry == patch->fields.end()) continue;
                    if (row.fields.find(fieldName) == row.fields.end()) {
                        row.fieldOrder.push_back(fieldName);
                    }
                    row.fields[fieldName] = entry->second;
                }
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
