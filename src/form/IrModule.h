#pragma once

#include "../FelidaeIr.h"

#include <unordered_map>
#include <vector>

namespace Felidae {

// AST-free executable module boundary.  The frontend is responsible for
// producing this value; the form VM and binary loader only consume it.
struct IrModule {
    FelidaeIr ir; // module initializer
    std::unordered_map<IrSymbolRef, IrProcedure> procedures;
    IrSymbolRef entryProcedure = 0;
};

// Serialized/link-time representation. Its code and tables are module-wide;
// procedure descriptors retain the bases necessary to materialize independent
// register-frame code blocks for the current VM.
struct IrProcedureMetadata {
    IrSymbolRef symbol = 0;
    std::vector<IrSymbolRef> positionalParameters;
    std::vector<IrSymbolRef> namedParameters;
    std::size_t registerCount = 0;
    std::size_t codeOffset = 0;
    std::size_t codeLength = 0;
    std::size_t constantOffset = 0;
    std::size_t constantCount = 0;
    std::size_t textOffset = 0;
    std::size_t textCount = 0;
    std::size_t symbolOffset = 0;
    std::size_t symbolCount = 0;
    std::size_t programOffset = 0;
    std::size_t programCount = 0;
    IrSourceMapEntry::Span sourceSpan;
    bool deterministicOnly = true;
};

struct LinkedIrModule {
    std::vector<IrWord> code;
    std::vector<IrWord> constants;
    std::vector<IrConstantKind> constantKinds;
    std::vector<std::string> texts;
    std::vector<IrWord> symbols;
    std::vector<IrWord> programs;
    std::vector<IrSourceMapEntry> sourceMap;
    IrProcedureMetadata initializer;
    std::vector<IrProcedureMetadata> procedures;
    IrSymbolRef entryProcedure = 0;
};

void verifyIrModule(const IrModule& module);
LinkedIrModule linkIrModule(const IrModule& module);
IrModule materializeIrModule(const LinkedIrModule& module);

} // namespace Felidae
