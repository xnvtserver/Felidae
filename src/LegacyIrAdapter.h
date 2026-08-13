#pragma once

// Transitional boundary: AST and legacy semantic services are deliberately
// isolated here. FelidaeIr, RegisterVm, and new parsers do not include AST.h.
#include "AST.h"
#include "FelidaeIr.h"
#include "Interpreter.h"

#include <memory>
#include <span>
#include <filesystem>
#include <vector>

namespace Felidae {

struct LegacyIrModule {
    FelidaeIr ir;
    std::vector<std::shared_ptr<Program>> programs;
    std::filesystem::path sourceDirectory;
};

class LegacyAstIrAdapter {
public:
    LegacyIrModule compile(Program program,
                           std::filesystem::path sourceDirectory = {}) const;
};

class LegacyVmRuntime final : public VmRuntime {
public:
    explicit LegacyVmRuntime(const LegacyIrModule& module) : module_(module) {}
    VmValue executeProgram(IrWord programRef, const VmValue& systemInput) override;
    VmValue loadSymbol(IrSymbolRef symbol) override;
    VmValue callSymbol(IrSymbolRef symbol, std::span<const VmValue> arguments) override;
    VmValue callSymbolNamed(IrSymbolRef symbol, std::span<const VmCallArgument> arguments) override;
    bool shouldBranchFalse(const VmValue& value) const override;
    Interpreter& services() noexcept { return services_; }
    bool executedEntry() const noexcept { return executedEntry_; }

private:
    const LegacyIrModule& module_;
    Interpreter services_;
    bool loaded_ = false;
    bool executedEntry_ = false;
};

std::shared_ptr<Expr> legacyExprFromVmValue(const VmValue& value);
VmValue legacyVmValue(std::shared_ptr<Expr> value);

} // namespace Felidae
