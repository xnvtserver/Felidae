#pragma once

#include "AST.h"
#include <memory>
#include <string>
#include <unordered_map>

namespace Felidae {

using Env = std::unordered_map<std::string, std::shared_ptr<Expr>>;

struct Solution {
    Env env;
};

} // namespace Felidae
