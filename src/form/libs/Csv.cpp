#include "Csv.h"

#include <charconv>
#include <cctype>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace Felidae::Form::Csv {
namespace {

std::vector<std::string> parseLine(std::string_view line) {
  std::vector<std::string> fields;
  std::string field;
  bool quoted = false;
  for (std::size_t index = 0; index < line.size(); ++index) {
    const char current = line[index];
    if (quoted) {
      if (current == '"') {
        if (index + 1 < line.size() && line[index + 1] == '"') {
          field.push_back('"');
          ++index;
        } else {
          quoted = false;
        }
      } else {
        field.push_back(current);
      }
      // A quote only opens quoted mode as the first character of a field
      // (RFC4180): a stray quote appearing mid-unquoted-field is literal
      // text, not a toggle, and must not swallow a later comma.
    } else if (current == '"' && field.empty()) {
      quoted = true;
    } else if (current == ',') {
      fields.push_back(std::move(field));
      field.clear();
    } else {
      field.push_back(current);
    }
  }
  if (quoted)
    throw std::runtime_error("csv.parse found an unterminated quoted field");
  fields.push_back(std::move(field));
  return fields;
}

Json::Value parseRows(std::string_view text, std::string_view typeName) {
  std::istringstream input{std::string(text)};
  std::string line;
  Json::Value result = Json::Value::array();
  if (!std::getline(input, line))
    return result;
  if (!line.empty() && line.back() == '\r')
    line.pop_back();
  const auto headers = parseLine(line);
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    // A blank line is ambiguous for multi-column data (most commonly a
    // harmless trailing blank line at end of file, so it's skipped), but for
    // single-column data it's the only way to represent an intentional
    // empty-string value and must become a real row, not be silently
    // dropped.
    if (line.empty() && headers.size() != 1)
      continue;
    const auto fields = parseLine(line);
    if (fields.size() != headers.size())
      throw std::runtime_error(
          "csv.parse row has a different field count than the header");
    Json::Value row = Json::Value::object();
    if (!typeName.empty())
      row["__type"] = typeName;
    for (std::size_t index = 0; index < headers.size(); ++index)
      row[headers[index]] = fields[index];
    result.push_back(std::move(row));
  }
  return result;
}

std::string scalarText(const Json::Value &value) {
  return value.is_string() ? value.get<std::string>() : value.dump();
}

std::string quote(std::string_view text) {
  if (text.find_first_of(",\"\r\n") == std::string_view::npos)
    return std::string(text);
  std::string result{"\""};
  for (const char current : text)
    result += current == '"' ? "\"\"" : std::string(1, current);
  result.push_back('"');
  return result;
}

std::vector<std::string> headers(const Json::Value &rows) {
  if (!rows.is_array())
    throw std::runtime_error("csv operation expects an array of rows");
  if (rows.empty())
    return {};
  if (!rows.front().is_object())
    throw std::runtime_error("csv operation expects object rows");
  std::vector<std::string> result;
  result.reserve(rows.front().size());
  for (const auto &[key, _] : rows.front().items())
    if (key != "__type")
      result.push_back(key);
  return result;
}

} // namespace

Json::Value parse(std::string_view text) { return parseRows(text, {}); }

Json::Value toFacts(std::string_view text, std::string_view typeName) {
  if (typeName.empty())
    throw std::runtime_error("csv.toFacts requires a non-empty fact type");
  auto rows = parseRows(text, typeName);
  for (auto &row : rows) {
    for (auto &[name, value] : row.items()) {
      if (name == "__type" || !value.is_string())
        continue;
      const auto &cell = value.get_ref<const std::string &>();
      if (cell.empty() ||
          (cell.size() > 1 && cell.front() == '0' &&
           std::isdigit(static_cast<unsigned char>(cell[1]))))
        continue;
      double number = 0.0;
      const auto parsed =
          std::from_chars(cell.data(), cell.data() + cell.size(), number);
      if (parsed.ec == std::errc{} && parsed.ptr == cell.data() + cell.size() &&
          std::isfinite(number)) {
        value = number;
      }
    }
  }
  return rows;
}

std::string toText(const Json::Value &rows) {
  const auto columns = headers(rows);
  if (rows.empty())
    return {};
  std::ostringstream output;
  for (std::size_t index = 0; index < columns.size(); ++index) {
    if (index)
      output << ',';
    output << quote(columns[index]);
  }
  output << '\n';
  for (const auto &row : rows) {
    if (!row.is_object())
      throw std::runtime_error("csv.toText expects object rows");
    for (std::size_t index = 0; index < columns.size(); ++index) {
      if (index)
        output << ',';
      const auto found = row.find(columns[index]);
      if (found != row.end())
        output << quote(scalarText(*found));
    }
    output << '\n';
  }
  return output.str();
}

std::string toFelidaeFacts(const Json::Value &rows, std::string_view typeName) {
  if (typeName.empty())
    throw std::runtime_error(
        "csv.toFelidaeFacts requires a non-empty fact type");
  const auto columns = headers(rows);
  std::ostringstream output;
  for (const auto &row : rows) {
    if (!row.is_object())
      throw std::runtime_error("csv.toFelidaeFacts expects object rows");
    output << typeName << '(';
    for (std::size_t index = 0; index < columns.size(); ++index) {
      if (index)
        output << ", ";
      const auto found = row.find(columns[index]);
      output << columns[index] << ": "
             << (found == row.end() ? "nil" : found->dump());
    }
    output << ")\n";
  }
  return output.str();
}

} // namespace Felidae::Form::Csv
