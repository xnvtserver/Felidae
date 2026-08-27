#include "IrModule.h"

#include <set>
#include <unordered_map>
#include <unordered_set>

namespace Felidae {

std::span<const PieceId> irSymbolPieces(std::span<const PieceSequence> symbols,
                                        IrSymbolRef symbol) {
  if (symbol == 0 || symbol > symbols.size()) {
    throw IrError("IR module symbol index is outside its PieceId table");
  }
  return symbols[static_cast<std::size_t>(symbol - 1)];
}

std::span<const PieceId> irSymbolPieces(const IrModule &module,
                                        IrSymbolRef symbol) {
  return irSymbolPieces(module.symbolTable, symbol);
}

static void validateIrModule(const IrModule &module) {
  if (module.symbolTable.empty())
    throw IrError("IR module has no SentencePiece symbol table");
  if (module.sentencePieceModelIdentity.empty())
    throw IrError("IR module has no SentencePiece model identity");
  std::set<PieceSequence> uniqueSymbols;
  for (const auto &pieces : module.symbolTable) {
    if (pieces.empty() || !uniqueSymbols.insert(pieces).second) {
      throw IrError("IR module symbol table is empty or duplicated");
    }
  }
  const auto requireSymbol = [&](IrSymbolRef symbol) {
    (void)irSymbolPieces(module, symbol);
  };
  const auto verifyProgramSymbols = [&](const FelidaeIr &ir) {
    for (const auto symbol : ir.symbols)
      requireSymbol(symbol);
  };
  verifyProgramSymbols(module.ir);
  requireSymbol(module.entryProcedure);
  for (const auto &[symbol, procedure] : module.procedures) {
    requireSymbol(symbol);
    verifyProgramSymbols(procedure.ir);
    for (const auto parameter : procedure.positionalParameters)
      requireSymbol(parameter);
    for (const auto parameter : procedure.namedParameters)
      requireSymbol(parameter);
  }
  for (const auto &type : module.factTypes) {
    requireSymbol(type.symbol);
    for (const auto parent : type.parents)
      requireSymbol(parent);
  }
  IrVerifier::verify(module.ir);
  if (module.entryProcedure == 0 ||
      !module.procedures.contains(module.entryProcedure))
    throw IrError("compiler IR entry procedure is invalid");
  const auto verifyCalls = [&](const FelidaeIr &ir) {
    for (std::size_t pc = 0; pc < ir.words.size();) {
      const auto opcode = static_cast<IrOpcode>(ir.words[pc]);
      if (opcode == IrOpcode::Call || opcode == IrOpcode::CallNamed ||
          opcode == IrOpcode::ForEachFact) {
        const auto operand = opcode == IrOpcode::ForEachFact ? 3u : 2u;
        const auto index = static_cast<std::size_t>(ir.words[pc + operand]);
        if (index >= ir.symbols.size() ||
            !module.procedures.contains(ir.symbols[index]))
          throw IrError("compiler IR call references an unknown procedure");
      }
      pc += irInstructionWidth(ir, pc);
    }
  };
  verifyCalls(module.ir);
  for (const auto &[symbol, procedure] : module.procedures) {
    if (symbol == 0)
      throw IrError("compiler IR procedure symbol is invalid");
    if (procedure.positionalParameters.size() !=
        procedure.namedParameters.size())
      throw IrError("compiler IR procedure parameter metadata is inconsistent");
    std::unordered_set<IrSymbolRef> positional, named;
    for (const auto parameter : procedure.positionalParameters)
      if (parameter == 0 || !positional.insert(parameter).second)
        throw IrError("compiler IR positional parameter metadata is invalid");
    for (const auto parameter : procedure.namedParameters)
      if (parameter == 0 || !named.insert(parameter).second)
        throw IrError("compiler IR named parameter metadata is invalid");
    IrVerifier::verify(procedure.ir);
    verifyCalls(procedure.ir);
  }
  std::unordered_map<IrSymbolRef, const IrFactType *> types;
  for (const auto &type : module.factTypes)
    if (type.symbol == 0 || !types.emplace(type.symbol, &type).second)
      throw IrError("compiler IR fact type is invalid or duplicated");
  for (const auto &type : module.factTypes) {
    std::unordered_set<IrSymbolRef> parents;
    for (const auto parent : type.parents)
      if (parent == 0 || parent == type.symbol || !types.contains(parent) ||
          !parents.insert(parent).second)
        throw IrError("compiler IR hierarchy parent is invalid");
  }
  std::unordered_set<IrSymbolRef> visiting, visited;
  const auto visit = [&](auto &&self, IrSymbolRef type) -> void {
    if (visited.contains(type))
      return;
    if (!visiting.insert(type).second)
      throw IrError("compiler IR hierarchy contains a cycle");
    for (const auto parent : types.at(type)->parents)
      self(self, parent);
    visiting.erase(type);
    visited.insert(type);
  };
  for (const auto &[symbol, _] : types)
    visit(visit, symbol);
}

VerifiedIrModule verifyIrModule(IrModule &&module) {
  validateIrModule(module);
  return VerifiedIrModule(std::move(module));
}

} // namespace Felidae
