#pragma once

#include "RegisterVm.h"
#include "SemanticOperation.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace Felidae {

using IsaWord = std::uint32_t;
using IsaRegister = std::uint8_t;

inline constexpr std::uint32_t kFelidaeIsaVersion = 1;
inline constexpr std::size_t kIsaRegisterLimit = 256;
inline constexpr std::size_t kIsaShortIndexLimit = 1u << 16;

// Opcode values are part of the Felidae ISA v1 ABI. Existing values must
// never be renumbered or reused; incompatible behavior requires a new ISA
// version.
enum class IsaOpcode : std::uint8_t {
    Halt = 0x00,

    LoadConstant = 0x10,
    LoadGlobal = 0x11,
    StoreGlobal = 0x12,
    Move = 0x13,

    Add = 0x20,
    Subtract = 0x21,
    Multiply = 0x22,
    Divide = 0x23,
    Modulo = 0x24,

    CompareEqual = 0x30,
    CompareNotEqual = 0x31,
    CompareLess = 0x32,
    CompareLessEqual = 0x33,
    CompareGreater = 0x34,
    CompareGreaterEqual = 0x35,
    BooleanNot = 0x36,
    BooleanAnd = 0x37,
    BooleanOr = 0x38,

    Jump = 0x40,
    JumpIfFalse = 0x41,

    Call = 0x50,
    CallNative = 0x51,
    Return = 0x52,
    CallNamed = 0x53,

    MakeFact = 0x60,
    GetField = 0x61,
    SetField = 0x62,
    QueryFacts = 0x63,
    MakeArray = 0x64,
    MakeMap = 0x65,
    Similarity = 0x66,
    Membership = 0x67,

    HierarchyIsA = 0x70,
    HierarchyCommonAncestors = 0x71,
    HierarchyLeastCommonAncestors = 0x72,
    HierarchyMostGeneralAncestors = 0x73,

    TemporalRank = 0x78,

    SemanticEval = 0x80,
};

// Semantic operation IDs are a separate permanent ABI namespace. Models may
// select a typed result for one of these operations, but may never emit ISA
// words or invent operation IDs.
struct DecodedIsaWord {
    IsaOpcode opcode = IsaOpcode::Halt;
    std::uint8_t a = 0;
    std::uint8_t b = 0;
    std::uint8_t c = 0;
    std::uint16_t bx = 0;
    std::uint32_t ax = 0;
};

constexpr IsaWord encodeIsaABC(IsaOpcode opcode, std::uint8_t a = 0,
                               std::uint8_t b = 0, std::uint8_t c = 0) noexcept {
    return static_cast<IsaWord>(opcode) | (static_cast<IsaWord>(a) << 8u) |
           (static_cast<IsaWord>(b) << 16u) | (static_cast<IsaWord>(c) << 24u);
}

constexpr IsaWord encodeIsaABx(IsaOpcode opcode, std::uint8_t a,
                               std::uint16_t bx) noexcept {
    return static_cast<IsaWord>(opcode) | (static_cast<IsaWord>(a) << 8u) |
           (static_cast<IsaWord>(bx) << 16u);
}

constexpr IsaWord encodeIsaAx(IsaOpcode opcode, std::uint32_t ax) noexcept {
    return static_cast<IsaWord>(opcode) | ((ax & 0x00ffffffu) << 8u);
}

constexpr DecodedIsaWord decodeIsaWord(IsaWord word) noexcept {
    return {static_cast<IsaOpcode>(word & 0xffu),
            static_cast<std::uint8_t>((word >> 8u) & 0xffu),
            static_cast<std::uint8_t>((word >> 16u) & 0xffu),
            static_cast<std::uint8_t>((word >> 24u) & 0xffu),
            static_cast<std::uint16_t>((word >> 16u) & 0xffffu),
            (word >> 8u) & 0x00ffffffu};
}

struct IsaBlock {
    std::vector<IsaWord> words;
    std::uint16_t registerCount = 0;
    std::vector<IrSourceMapEntry> sourceMap;
    IsaBlock() = default;
    IsaBlock(std::vector<IsaWord> encodedWords,std::uint16_t registers,
             std::vector<IrSourceMapEntry> map = {})
        : words(std::move(encodedWords)),registerCount(registers),sourceMap(std::move(map)) {}
};

struct IsaProgram {
    IsaBlock code;
    std::vector<IrWord> constants;
    std::vector<IrConstantKind> constantKinds;
    std::vector<std::string> texts;
    std::vector<IrSymbolRef> symbols;
    std::vector<IrSourceMapEntry> sourceMap;
};

struct IsaProcedure {
    IsaProgram program;
    std::vector<IrSymbolRef> positionalParameters;
    std::vector<IrSymbolRef> namedParameters;
    IrSourceMapEntry::Span sourceSpan;
};

struct IsaFactType {
    IrSymbolRef symbol = 0;
    std::vector<IrSymbolRef> parents;
    IrSourceMapEntry::Span sourceSpan;
};

struct IsaSymbolName {
    IrSymbolRef symbol = 0;
    std::string name;
};

struct IsaModule {
    std::uint32_t isaVersion = kFelidaeIsaVersion;
    IsaProgram initializer;
    std::vector<IsaProcedure> procedures;
    std::vector<IrSymbolRef> procedureSymbols;
    std::vector<IsaFactType> factTypes;
    // Non-executable display metadata. Instructions continue to reference
    // bounded symbol-pool indexes; the VM never tokenizes these spellings.
    std::vector<IsaSymbolName> symbolNames;
    std::uint16_t entryProcedure = 0;
};

struct IsaVerificationContext {
    std::size_t constantCount = 0;
    std::size_t symbolCount = 0;
    std::size_t procedureCount = 0;
};

class IsaVerifier {
public:
    static void verify(const IsaBlock& block, const IsaVerificationContext& context);
};

void verifyIsaModule(const IsaModule& module);

std::size_t isaInstructionWidth(std::span<const IsaWord> words, std::size_t pc);
bool isKnownIsaOpcode(std::uint8_t opcode) noexcept;
bool isKnownSemanticOperation(std::uint16_t operation) noexcept;
bool semanticOperationAcceptsArity(std::uint16_t operation,
                                   std::size_t inputCount) noexcept;

} // namespace Felidae
