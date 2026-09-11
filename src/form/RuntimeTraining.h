#pragma once

#include "RegisterVm.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace Felidae {

// Structural tokens share the runtime model's input vocabulary with raw
// SentencePiece IDs. Encoded values set the high bit; RuntimeStateModel
// relocates the low marker into the reserved vocabulary tail. Keep this list
// authoritative for both dataset generation and live inference.
inline constexpr std::uint32_t kRuntimeStructuralEncodingBit = 0x80000000u;
enum class RuntimeStructuralToken : std::uint32_t {
    FactTypes = 8,
    Hierarchy = 9,
    Inputs = 10,
    FactCounts = 11,
    OneFact = 12,
    SeveralFacts = 13,
    ManyFacts = 14,
    InputKindBase = 15,
    ValueStart = 26,
    ValueEnd = 27,
    Nil = 28,
    Number = 29,
    Degree = 30,
    Text = 31,
    Symbol = 32,
    Array = 33,
    Map = 34,
    Fact = 35,
    TextMap = 37,
    NumberBitZero = 38,
    NumberBitOne = 39,
    Field = 40,
};

constexpr std::uint32_t runtimeStructuralToken(RuntimeStructuralToken token) {
    return kRuntimeStructuralEncodingBit |
           static_cast<std::uint32_t>(token);
}
static_assert(static_cast<std::uint32_t>(RuntimeStructuralToken::Field) < 48,
              "runtime structural token exceeds the reserved vocabulary");

// Stable semantic actions produced by the finite runtime GRU vocabulary.
// This is deliberately not VmValue::index() or a model-logit index.
enum class RuntimeTrainingTargetKind : std::uint8_t {
    InputReference = 1,
    FactFromInput = 2,
    DegreeMilli = 3,
    Nil = 4,
    NumericTruth = 5,
    Score = 6,
};

// One verified runtime operation, represented exactly as the current GRU
// sees it: operation identity, ordered input kinds, and a bounded snapshot of
// fact types plus hierarchy edges. Hidden train-only features are forbidden.
struct RuntimeTrainingRecord {
    std::uint16_t operationId = 0;
    std::vector<RuntimeValueKind> inputKinds;
    // Canonical value sequences: SentencePiece IDs are stored directly;
    // structural markers use the high bit and are relocated to the model's
    // reserved tail vocabulary by RuntimeStateModel.
    std::vector<std::vector<std::uint32_t>> inputValues;
    std::vector<PieceSequence> factTypes;
    std::vector<std::pair<PieceSequence, std::uint32_t>> factTypeCounts;
    std::vector<std::pair<PieceSequence, PieceSequence>> hierarchyEdges;
    RuntimeTrainingTargetKind targetKind = RuntimeTrainingTargetKind::Nil;
    std::uint32_t targetValue = 0;
    double targetScore = 0.0;
};

std::vector<std::uint32_t>
runtimeValueEncoding(const VmValue &value,
                     std::span<const PieceSequence> symbolTable);

struct RuntimeKnowledgePieces {
    std::vector<PieceSequence> factTypes;
    std::vector<std::pair<PieceSequence, std::uint32_t>> factTypeCounts;
    std::vector<std::pair<PieceSequence, PieceSequence>> hierarchyEdges;
};

// Convert module-local runtime symbol references at the single boundary where
// the SSM consumes them. Both live inference and dataset extraction use this
// path, so their model inputs cannot drift apart.
RuntimeKnowledgePieces runtimeKnowledgePieces(
    const VmKnowledgeSnapshot& knowledge,
    std::span<const PieceSequence> symbolTable);

// JSON Lines v9: one self-describing record per line. The
// schema value is repeated deliberately: lines can be validated or streamed
// independently and no legacy binary header needs to be retained.
inline constexpr std::uint32_t kRuntimeTrainingSchemaVersion = 9;
void verifyRuntimeTrainingRecord(const RuntimeTrainingRecord& record);
void writeRuntimeTrainingDataset(const std::filesystem::path& path,
                                 std::span<const RuntimeTrainingRecord> records);
std::vector<RuntimeTrainingRecord> loadRuntimeTrainingDataset(const std::filesystem::path& path);

} // namespace Felidae
