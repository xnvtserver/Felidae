#pragma once

#include "Json.h"
#include "form/BuiltinOperation.h"

#include <optional>

namespace Felidae::Form::Set {

Json::Value evaluate(BuiltinId operation, const Json::Value &sets,
                     const std::optional<Json::Value> &value = std::nullopt,
                     const Json::Value &fields = Json::Value::array());

} // namespace Felidae::Form::Set
