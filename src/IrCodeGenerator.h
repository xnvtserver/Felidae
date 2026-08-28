#pragma once

// The syntax-shaped HIR is compiler-front-end input only. FelidaeIr and
// RegisterVm do not include this header and never receive HIR nodes.
#include "AST.h"
#include "form/IrModule.h"

#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace Felidae {

class IrCodeGenerator {
public:
  static FelidaeIr lowerExpression(
      const std::shared_ptr<Expr> &expression,
      const std::unordered_set<SymbolId> &factTypes = {},
      const std::unordered_map<SymbolId, SymbolId> &factDesignations = {});
  static FelidaeIr lowerGlobalBinding(
      const GlobalBindingStmt &binding,
      const std::unordered_set<SymbolId> &factTypes = {},
      const std::unordered_map<SymbolId, SymbolId> &factDesignations = {});
  static FelidaeIr lowerEntryMethod(
      const ClauseStmt &method,
      const std::unordered_set<SymbolId> &factTypes = {},
      const std::unordered_map<SymbolId, SymbolId> &factDesignations = {});
  IrModule compile(Program program) const;
};

} // namespace Felidae
