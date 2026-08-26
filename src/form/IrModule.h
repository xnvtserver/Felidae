#pragma once

#include "../FelidaeIr.h"

#include <unordered_map>
#include <vector>
#include <memory>

namespace Felidae {

struct IrFactType {
    IrSymbolRef symbol = 0;
    std::vector<IrSymbolRef> parents;
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
    std::string sentencePieceModelHash;
    IrSymbolRef entryProcedure = 0;
};

class VerifiedIrModule {
public:
    const IrModule& get() const noexcept { return *module_; }
    const IrModule* operator->() const noexcept { return module_.get(); }
private:
    explicit VerifiedIrModule(IrModule module)
        : module_(std::make_shared<const IrModule>(std::move(module))) {}
    std::shared_ptr<const IrModule> module_;
    friend VerifiedIrModule verifyIrModule(IrModule&& module);
};

// Authoritative decoder for compiler-only IR. It validates both fixed and
// variable-width instructions before returning their encoded word count.
std::size_t compilerInstructionWidth(const FelidaeIr& ir, std::size_t pc);
void verifyIrModule(const IrModule& module);
VerifiedIrModule verifyIrModule(IrModule&& module);

} // namespace Felidae
