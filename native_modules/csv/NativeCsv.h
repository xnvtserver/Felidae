#pragma once

#include "../../src/AST.h"
#include <memory>
#include <stdexcept>
#include <string>

namespace Felidae::NativeCsv {

class Error : public std::runtime_error {
public:
    explicit Error(const std::string& message) : std::runtime_error(message) {}
};

std::shared_ptr<ArrayExpr> parse(const std::string& csvText,
                                 const std::string& typeName,
                                 const std::string& builtinName);

std::string toText(const std::shared_ptr<Expr>& value,
                   const std::string& builtinName);

std::string toFelidaeFacts(const std::shared_ptr<Expr>& value,
                           const std::string& typeName,
                           const std::string& builtinName);

} // namespace Felidae::NativeCsv
