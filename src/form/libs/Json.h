#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace Felidae::Form::Json {

using Value = nlohmann::ordered_json;

Value parse(std::string_view text);
const Value &get(const Value &value, std::string_view key);
bool has(const Value &value, std::string_view key);
std::vector<std::string> keys(const Value &value);
Value set(Value value, std::string key, Value fieldValue);
Value remove(Value value, std::string_view key);
std::string toText(const Value &value);

} // namespace Felidae::Form::Json
