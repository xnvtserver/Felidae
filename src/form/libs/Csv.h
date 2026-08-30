#pragma once

#include "Json.h"

#include <span>
#include <string>
#include <string_view>

namespace Felidae::Form::Csv {

Json::Value parse(std::string_view text);
Json::Value toFacts(std::string_view text, std::string_view typeName);
std::string toText(const Json::Value &rows);
std::string toText(const Json::Value &rows,
                   std::span<const std::string> columns);
std::string toFelidaeFacts(const Json::Value &rows, std::string_view typeName);

} // namespace Felidae::Form::Csv
