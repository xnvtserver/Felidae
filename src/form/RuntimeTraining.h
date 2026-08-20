#pragma once

#include "RegisterVm.h"

#include <filesystem>
#include <vector>

namespace Felidae {

// Versioned, integer-only sample for runtime-model training. It represents
// observed verified execution, not a raw .bin byte sequence. A model builder
// may use the opcode stream as an additional feature, but this record keeps
// the deterministic result and fact/hierarchy context authoritative.
struct RuntimeTrainingRecord {
    IrSymbolRef moduleEntry = 0;
    IrWord inputKind = 0;
    IrWord resultKind = 0;
    std::vector<IrFactRef> relevantFacts;
    std::vector<IrSymbolRef> factTypes;
    std::vector<VmExecutionTrace> trace;
};

inline constexpr std::uint32_t kRuntimeTrainingSchemaVersion = 1;
void writeRuntimeTrainingDataset(const std::filesystem::path& path,
                                 std::span<const RuntimeTrainingRecord> records);
std::vector<RuntimeTrainingRecord> loadRuntimeTrainingDataset(const std::filesystem::path& path);

} // namespace Felidae
