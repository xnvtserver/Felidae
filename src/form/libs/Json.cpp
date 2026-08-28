#include "Json.h"

#include <stdexcept>

namespace Felidae::Form::Json {

Value parse(std::string_view text) {
  try {
    return Value::parse(text.begin(), text.end());
  } catch (const nlohmann::json::exception &error) {
    throw std::runtime_error("json.parse received invalid JSON: " +
                             std::string(error.what()));
  }
}

const Value &get(const Value &value, std::string_view key) {
  if (!value.is_object())
    throw std::runtime_error("json.get expects an object");
  const auto found = value.find(std::string(key));
  if (found == value.end())
    throw std::runtime_error("json.get key is absent: " + std::string(key));
  return *found;
}

bool has(const Value &value, std::string_view key) {
  if (!value.is_object())
    throw std::runtime_error("json.has expects an object");
  return value.contains(std::string(key));
}

std::vector<std::string> keys(const Value &value) {
  if (!value.is_object())
    throw std::runtime_error("json.keys expects an object");
  std::vector<std::string> result;
  result.reserve(value.size());
  for (const auto &[key, _] : value.items())
    result.push_back(key);
  return result;
}

Value set(Value value, std::string key, Value fieldValue) {
  if (!value.is_object())
    throw std::runtime_error("json.set expects an object");
  value[std::move(key)] = std::move(fieldValue);
  return value;
}

Value remove(Value value, std::string_view key) {
  if (!value.is_object())
    throw std::runtime_error("json.remove expects an object");
  value.erase(std::string(key));
  return value;
}

std::string toText(const Value &value) { return value.dump(); }

} // namespace Felidae::Form::Json
