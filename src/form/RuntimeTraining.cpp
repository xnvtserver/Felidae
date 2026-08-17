#include "RuntimeTraining.h"

#include <fstream>
#include <limits>

namespace Felidae {
namespace {
constexpr std::uint32_t kMagic = 0x44545246; // FRTD, little endian.

std::uint32_t checked(std::size_t value, const char* what) {
    if (value > std::numeric_limits<std::uint32_t>::max()) throw IrError(what);
    return static_cast<std::uint32_t>(value);
}
void write32(std::ofstream& output, std::uint32_t value) { output.write(reinterpret_cast<const char*>(&value), sizeof(value)); }
void write64(std::ofstream& output, std::uint64_t value) { output.write(reinterpret_cast<const char*>(&value), sizeof(value)); }
std::uint32_t read32(std::ifstream& input) { std::uint32_t value{}; if (!input.read(reinterpret_cast<char*>(&value), sizeof(value))) throw IrError("runtime dataset is truncated"); return value; }
std::uint64_t read64(std::ifstream& input) { std::uint64_t value{}; if (!input.read(reinterpret_cast<char*>(&value), sizeof(value))) throw IrError("runtime dataset is truncated"); return value; }
template <typename T> void writeIds(std::ofstream& output, const std::vector<T>& ids, const char* what) {
    write32(output, checked(ids.size(), what)); for (const auto id : ids) write64(output, id);
}
template <typename T> std::vector<T> readIds(std::ifstream& input, const char* what) {
    const auto count = read32(input); if (count > 1'000'000) throw IrError(what);
    std::vector<T> ids; ids.reserve(count); for (std::uint32_t i = 0; i < count; ++i) ids.push_back(static_cast<T>(read64(input))); return ids;
}
}

void writeRuntimeTrainingDataset(const std::filesystem::path& path,
                                 std::span<const RuntimeTrainingRecord> records) {
    if (records.size() > 1'000'000) throw IrError("runtime dataset has too many records");
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw IrError("cannot write runtime training dataset");
    write32(output, kMagic); write32(output, kRuntimeTrainingSchemaVersion); write32(output, checked(records.size(), "runtime dataset has too many records"));
    for (const auto& record : records) {
        write64(output, record.moduleEntry); write64(output, record.inputKind); write64(output, record.resultKind);
        writeIds(output, record.relevantFacts, "runtime dataset has too many facts");
        writeIds(output, record.factTypes, "runtime dataset has too many fact types");
        write32(output, checked(record.trace.size(), "runtime dataset trace is too large"));
        for (const auto& event : record.trace) {
            write64(output, event.sequence); write32(output, static_cast<std::uint32_t>(event.kind));
            write64(output, event.symbol); write64(output, event.fact); write64(output, event.callDepth);
        }
    }
    if (!output) throw IrError("cannot finish runtime training dataset");
}

std::vector<RuntimeTrainingRecord> loadRuntimeTrainingDataset(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary); if (!input) throw IrError("cannot read runtime training dataset");
    if (read32(input) != kMagic || read32(input) != kRuntimeTrainingSchemaVersion) throw IrError("runtime training dataset header is incompatible");
    const auto count = read32(input); if (count > 1'000'000) throw IrError("runtime dataset has too many records");
    std::vector<RuntimeTrainingRecord> records; records.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        RuntimeTrainingRecord record; record.moduleEntry = read64(input); record.inputKind = read64(input); record.resultKind = read64(input);
        record.relevantFacts = readIds<IrFactRef>(input, "runtime dataset fact section is too large");
        record.factTypes = readIds<IrSymbolRef>(input, "runtime dataset type section is too large");
        const auto traceCount = read32(input); if (traceCount > 1'000'000) throw IrError("runtime dataset trace section is too large");
        record.trace.reserve(traceCount);
        for (std::uint32_t j = 0; j < traceCount; ++j) {
            VmExecutionTrace event; event.sequence = read64(input); const auto kind = read32(input);
            if (kind > static_cast<std::uint32_t>(VmTraceKind::ExecutionResult)) throw IrError("runtime dataset has an invalid trace kind");
            event.kind = static_cast<VmTraceKind>(kind); event.symbol = read64(input); event.fact = read64(input); event.callDepth = read64(input); record.trace.push_back(event);
        }
        records.push_back(std::move(record));
    }
    if (input.peek() != std::ifstream::traits_type::eof()) throw IrError("runtime dataset has trailing bytes");
    return records;
}
} // namespace Felidae
