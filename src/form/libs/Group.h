#pragma once

#include "Json.h"
#include "form/BuiltinOperation.h"

#include <optional>

namespace Felidae::Form::Group {

Json::Value evaluate(BuiltinId operation, const Json::Value &members,
                     const Json::Value &table,
                     const std::optional<Json::Value> &identity);

} // namespace Felidae::Form::Group
