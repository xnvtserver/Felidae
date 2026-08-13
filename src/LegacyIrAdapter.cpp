#include "LegacyIrAdapter.h"

#include "Symbol.h"

namespace Felidae {

LegacyIrModule LegacyAstIrAdapter::compile(Program program,
                                           std::filesystem::path sourceDirectory) const {
    LegacyIrModule module;
    auto owned = std::make_shared<Program>(std::move(program));
    module.programs.push_back(owned);
    module.sourceDirectory = std::move(sourceDirectory);
    module.ir.programs.push_back(0);
    module.ir.words = {static_cast<IrWord>(IrOpcode::ExecuteProgram), 0,
                       static_cast<IrWord>(IrOpcode::End)};
    if (!owned->statements.empty()) {
        const auto& span = owned->statements.front()->sourceSpan;
        module.ir.sourceMap.push_back(IrSourceMapEntry{0,
            {span.startLine, span.startColumn, span.endLine, span.endColumn}});
    }
    return module;
}

VmValue legacyVmValue(std::shared_ptr<Expr> value) {
    return VmOpaqueValue{std::move(value)};
}

std::shared_ptr<Expr> legacyExprFromVmValue(const VmValue& value) {
    if (std::holds_alternative<VmNil>(value)) return std::make_shared<NilExpr>();
    if (const auto number = std::get_if<double>(&value)) return std::make_shared<NumberExpr>(*number);
    if (const auto boolean = std::get_if<bool>(&value)) return std::make_shared<BoolExpr>(*boolean);
    if (const auto text = std::get_if<VmText>(&value)) return std::make_shared<StringExpr>(text->utf8);
    if (const auto array = std::get_if<VmArrayPtr>(&value)) {
        std::vector<std::shared_ptr<Expr>> values;
        if (*array) {
            values.reserve((*array)->values.size());
            for (const auto& item : (*array)->values) values.push_back(legacyExprFromVmValue(item));
        }
        return std::make_shared<ArrayExpr>(std::move(values));
    }
    if (const auto map = std::get_if<VmMapPtr>(&value)) {
        std::vector<MapEntry> entries;
        if (*map) {
            entries.reserve((*map)->entries.size());
            for (const auto& [key, item] : (*map)->entries) {
                entries.emplace_back(symbolNameForId(static_cast<SymbolId>(key)),
                                     static_cast<SymbolId>(key), legacyExprFromVmValue(item));
            }
        }
        return std::make_shared<MapExpr>(std::move(entries));
    }
    if (const auto fact = std::get_if<VmFactPtr>(&value)) {
        std::vector<MapEntry> entries;
        if (*fact) {
            entries.reserve((*fact)->fields.size());
            for (const auto& [key, item] : (*fact)->fields) {
                entries.emplace_back(symbolNameForId(static_cast<SymbolId>(key)),
                                     static_cast<SymbolId>(key), legacyExprFromVmValue(item));
            }
        }
        auto result = std::make_shared<MapExpr>(std::move(entries));
        if (*fact) result->factType = symbolNameForId(static_cast<SymbolId>((*fact)->type));
        return result;
    }
    const auto* opaque = std::get_if<VmOpaqueValue>(&value);
    if (!opaque || !opaque->object) throw IrError("VM result is not a legacy runtime value");
    return std::static_pointer_cast<Expr>(opaque->object);
}

VmValue LegacyVmRuntime::executeProgram(IrWord programRef, const VmValue& systemInput) {
    if (programRef >= module_.programs.size() || !module_.programs[programRef]) {
        throw IrError("IR runtime program reference is invalid");
    }
    if (!loaded_) {
        services_.beginModuleTransaction();
        try {
            for (const auto& imported : module_.programs[programRef]->imports) {
                for (const auto& path : imported->paths) {
                    services_.addImport(module_.sourceDirectory, path);
                }
            }
            services_.addProgram(*module_.programs[programRef]);
            services_.commitModuleTransaction();
            loaded_ = true;
        } catch (...) {
            services_.rollbackModuleTransaction();
            throw;
        }
    }
    if (services_.hasMethod("main")) {
        executedEntry_ = true;
        return legacyVmValue(services_.callMain(legacyExprFromVmValue(systemInput)));
    }
    if (services_.hasAutoEntryCall()) {
        executedEntry_ = true;
        return legacyVmValue(services_.callAutoEntry());
    }
    return VmNil{};
}

bool LegacyVmRuntime::shouldBranchFalse(const VmValue& value) const {
    if (const auto* opaque = std::get_if<VmOpaqueValue>(&value)) {
        if (!opaque->object) return true;
        const auto expression = std::static_pointer_cast<Expr>(opaque->object);
        if (std::dynamic_pointer_cast<NilExpr>(expression)) return true;
        if (const auto boolean = std::dynamic_pointer_cast<BoolExpr>(expression)) return !boolean->value;
        return false;
    }
    return VmRuntime::shouldBranchFalse(value);
}

VmValue LegacyVmRuntime::loadSymbol(IrSymbolRef symbol) {
    const auto name = symbolNameForId(static_cast<SymbolId>(symbol));
    if (!services_.hasGlobal(name)) {
        throw IrError("IR references an undefined symbol: " + name);
    }
    const auto value = services_.evaluateGlobal(name);
    if (const auto number = std::dynamic_pointer_cast<NumberExpr>(value)) return number->value;
    if (const auto boolean = std::dynamic_pointer_cast<BoolExpr>(value)) return boolean->value;
    if (std::dynamic_pointer_cast<NilExpr>(value)) return VmNil{};
    return legacyVmValue(value);
}

VmValue LegacyVmRuntime::callSymbol(IrSymbolRef symbol, std::span<const VmValue> arguments) {
    const auto name = symbolNameForId(static_cast<SymbolId>(symbol));
    std::vector<Arg> callArguments;
    callArguments.reserve(arguments.size());
    for (const auto& argument : arguments) callArguments.emplace_back("", legacyExprFromVmValue(argument));
    TermExpr term(name, static_cast<SymbolId>(symbol), std::move(callArguments));
    return legacyVmValue(services_.callValue(term));
}

VmValue LegacyVmRuntime::callSymbolNamed(IrSymbolRef symbol,
                                         std::span<const VmCallArgument> arguments) {
    const auto name = symbolNameForId(static_cast<SymbolId>(symbol));
    std::vector<Arg> callArguments;
    callArguments.reserve(arguments.size());
    for (const auto& argument : arguments) {
        const auto argumentName = argument.name
            ? symbolNameForId(static_cast<SymbolId>(*argument.name)) : std::string{};
        callArguments.emplace_back(argumentName, argument.name ? static_cast<SymbolId>(*argument.name) : 0,
                                   legacyExprFromVmValue(argument.value));
    }
    return legacyVmValue(services_.callValue(TermExpr(
        name, static_cast<SymbolId>(symbol), std::move(callArguments))));
}

} // namespace Felidae
