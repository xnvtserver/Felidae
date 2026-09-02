#pragma once

#include "../FelidaeIr.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace Felidae {

struct IrFactType {
  IrSymbolRef symbol = 0;
  std::vector<IrSymbolRef> parents;
  std::vector<std::vector<IrSymbolRef>> indexes;
  IrSourceMapEntry::Span sourceSpan;
};

// AST-free executable module shared by compiler, FELBIR writer, and VM.
struct IrModule {
  FelidaeIr ir; // module initializer
  std::unordered_map<IrSymbolRef, IrProcedure> procedures;
  std::vector<IrFactType> factTypes;
  // One-based runtime SymbolIndex values address this table. Each entry is
  // the complete SentencePiece sequence; hashes are never symbol identity.
  std::vector<PieceSequence> symbolTable;
  std::string sentencePieceModelIdentity;
  IrSymbolRef entryProcedure = 0;
};

class VerifiedIrModule {
public:
  const IrModule &get() const noexcept { return *module_; }
  const IrModule *operator->() const noexcept { return module_.get(); }

private:
  explicit VerifiedIrModule(IrModule module)
      : module_(std::make_shared<const IrModule>(std::move(module))) {}
  std::shared_ptr<const IrModule> module_;
  friend VerifiedIrModule verifyIrModule(IrModule &&module);
};

// Authoritative executable-IR instruction decoder. It validates both fixed
// and variable-width instructions before returning their encoded word count.
std::size_t irInstructionWidth(const FelidaeIr &ir, std::size_t pc);
std::span<const PieceId> irSymbolPieces(std::span<const PieceSequence> symbols,
                                        IrSymbolRef symbol);
std::span<const PieceId> irSymbolPieces(const IrModule &module,
                                        IrSymbolRef symbol);
VerifiedIrModule verifyIrModule(IrModule &&module);

} // namespace Felidae
