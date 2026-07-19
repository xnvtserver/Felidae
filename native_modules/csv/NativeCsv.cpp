#include "NativeCsv.h"

#include <set>
#include <sstream>
#include <vector>

#if __has_include("../../third_party/rapidcsv/rapidcsv.h")
#include "../../third_party/rapidcsv/rapidcsv.h"
#define FELIDAE_HAS_RAPIDCSV 1
#endif

namespace Felidae::NativeCsv {

static bool asString(const std::shared_ptr<Expr>& expr, std::string& out) {
    if (auto str = std::dynamic_pointer_cast<StringExpr>(expr)) {
        out = str->value;
        return true;
    }
    return false;
}

static std::shared_ptr<ArrayExpr> rowsToMaps(const std::vector<std::string>& headers,
                                             const std::vector<std::vector<std::string>>& rows,
                                             const std::string& typeName) {
    std::vector<std::shared_ptr<Expr>> items;
    items.reserve(rows.size());
    for (const auto& row : rows) {
        std::vector<MapEntry> entries;
        if (!typeName.empty()) entries.push_back(MapEntry{"__type", std::make_shared<StringExpr>(typeName)});
        for (size_t i = 0; i < headers.size(); ++i) {
            entries.push_back(MapEntry{headers[i], std::make_shared<StringExpr>(row[i])});
        }
        items.push_back(std::make_shared<MapExpr>(std::move(entries)));
    }
    return std::make_shared<ArrayExpr>(std::move(items));
}

static std::vector<std::shared_ptr<Expr>> requireArrayItems(const std::shared_ptr<Expr>& value,
                                                            const std::string& builtinName,
                                                            const std::string& label) {
    if (auto array = std::dynamic_pointer_cast<ArrayExpr>(value)) return array->items;
    throw Error(builtinName + " expects array argument '" + label + "'");
}

static std::vector<MapEntry> requireMapEntries(const std::shared_ptr<Expr>& value,
                                               const std::string& builtinName,
                                               const std::string& label) {
    if (auto map = std::dynamic_pointer_cast<MapExpr>(value)) return map->entries;
    throw Error(builtinName + " expects map row in '" + label + "'");
}

static std::vector<std::string> headersFromRows(const std::vector<std::shared_ptr<Expr>>& rows,
                                                const std::string& builtinName) {
    if (rows.empty()) return {};
    std::vector<std::string> headers;
    std::set<std::string> seen;
    for (const auto& entry : requireMapEntries(rows.front(), builtinName, "data")) {
        if (entry.key == "__type" || entry.key == "__parent") continue;
        headers.push_back(entry.key);
        seen.insert(entry.key);
    }
    for (const auto& row : rows) {
        for (const auto& entry : requireMapEntries(row, builtinName, "data")) {
            if (entry.key == "__type" || entry.key == "__parent") continue;
            if (seen.insert(entry.key).second) headers.push_back(entry.key);
        }
    }
    return headers;
}

static std::string mapFieldAsText(const std::vector<MapEntry>& entries, const std::string& key) {
    for (const auto& entry : entries) {
        if (entry.key == key) {
            std::string text;
            if (asString(entry.value, text)) return text;
            return entry.value->debug();
        }
    }
    return {};
}

std::shared_ptr<ArrayExpr> parse(const std::string& csvText,
                                 const std::string& typeName,
                                 const std::string& builtinName) {
#ifdef FELIDAE_HAS_RAPIDCSV
    std::stringstream stream(csvText);
    try {
        rapidcsv::Document doc(stream, rapidcsv::LabelParams(0, -1));
        std::vector<std::string> headers = doc.GetColumnNames();
        if (headers.empty()) throw Error(builtinName + " failed: CSV header row is empty");
        std::set<std::string> seen;
        for (const auto& header : headers) {
            if (header.empty()) throw Error(builtinName + " failed: CSV header names cannot be empty");
            if (!seen.insert(header).second) throw Error(builtinName + " failed: Duplicate CSV header: " + header);
        }
        std::vector<std::vector<std::string>> rows;
        const size_t rowCount = doc.GetRowCount();
        rows.reserve(rowCount);
        for (size_t i = 0; i < rowCount; ++i) {
            rows.push_back(doc.GetRow<std::string>(i));
            if (rows.back().size() != headers.size()) {
                throw Error(builtinName + " failed: CSV row " + std::to_string(i + 2) +
                            " has " + std::to_string(rows.back().size()) +
                            " fields, expected " + std::to_string(headers.size()));
            }
        }
        return rowsToMaps(headers, rows, typeName);
    } catch (const Error&) {
        throw;
    } catch (const std::exception& ex) {
        throw Error(builtinName + " failed: " + std::string(ex.what()));
    }
#else
    (void)csvText;
    (void)typeName;
    throw Error(builtinName + " requires rapidcsv. Add third_party/rapidcsv/rapidcsv.h or build with the CSV dependency available.");
#endif
}

std::string toText(const std::shared_ptr<Expr>& value, const std::string& builtinName) {
    const auto rows = requireArrayItems(value, builtinName, "data");
    const auto headers = headersFromRows(rows, builtinName);
#ifdef FELIDAE_HAS_RAPIDCSV
    rapidcsv::Document doc("", rapidcsv::LabelParams(0, -1));
    for (size_t col = 0; col < headers.size(); ++col) doc.SetColumnName(col, headers[col]);
    for (size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
        const auto entries = requireMapEntries(rows[rowIndex], builtinName, "data");
        for (size_t col = 0; col < headers.size(); ++col) {
            doc.SetCell<std::string>(col, rowIndex, mapFieldAsText(entries, headers[col]));
        }
    }
    std::ostringstream out;
    doc.Save(out);
    return out.str();
#else
    (void)headers;
    throw Error(builtinName + " requires rapidcsv. Add third_party/rapidcsv/rapidcsv.h or build with the CSV dependency available.");
#endif
}

std::string toFelidaeFacts(const std::shared_ptr<Expr>& value,
                           const std::string& typeName,
                           const std::string& builtinName) {
    if (typeName.empty()) throw Error(builtinName + " expects non-empty string argument 'type'");
    const auto rows = requireArrayItems(value, builtinName, "data");
    std::ostringstream out;
    for (const auto& row : rows) {
        const auto entries = requireMapEntries(row, builtinName, "data");
        out << typeName << "(";
        bool first = true;
        for (const auto& entry : entries) {
            if (entry.key == "__type" || entry.key == "__parent") continue;
            if (!first) out << ", ";
            out << entry.key << ": " << (entry.value ? entry.value->debug() : "nil");
            first = false;
        }
        out << ").\n";
    }
    return out.str();
}

} // namespace Felidae::NativeCsv
