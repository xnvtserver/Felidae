#pragma once

// Unified variable-width executable IR shared by compiler and RegisterVm.
#include <cstddef>
#include <cstdint>
namespace Felidae { using SentencePieceId = std::size_t; }
#include "form/RegisterVm.h"

namespace Felidae {

enum class IrOpcode : std::uint8_t {
    End = 0,
    LoadConst,
    LoadSymbol,
    StoreSymbol,
    Move,
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Compare,
    Jump,
    JumpIfFalse,
    Call,
    CallNamed,
    CallNative,
    SemanticEval,
    MakeFact,
    MakeArray,
    MakeMap,
    GetField,
    SetField,
    Similarity,
    Membership,
    ForEachFact,
    HierarchyIsA,
    HierarchyCommonAncestors,
    HierarchyLeastCommonAncestors,
    HierarchyMostGeneralAncestors,
    TemporalRank,
    Return,
    Count
};

inline constexpr IrWord kIrOpcodeCount = static_cast<IrWord>(IrOpcode::Count);

enum class IrOperandKind : std::uint8_t { Register = 1, Constant, Symbol, Fact, Jump, Program };
enum class IrComparison : std::uint8_t { Equal = 0, NotEqual, Less, LessEqual, Greater, GreaterEqual };

struct FelidaeIr {
    std::vector<IrWord> words;
    std::vector<IrConstant> constants;
    std::vector<IrConstantKind> constantKinds;
    // Complete SentencePiece sequences are the canonical text representation.
    // IR words carry only a bounded side-table index; UTF-8 exists only at
    // source and display boundaries.
    std::vector<PieceSequence> texts;
    std::vector<IrSymbolRef> symbols;
    std::vector<IrWord> programs;
    std::vector<IrSourceMapEntry> sourceMap;
    std::size_t registerCount = 0;
};

struct IrProcedure {
    FelidaeIr ir;
    std::vector<IrSymbolRef> positionalParameters;
    std::vector<IrSymbolRef> namedParameters;
    IrSourceMapEntry::Span sourceSpan;
};

class IrVerifier {
public:
    static void verify(const FelidaeIr& ir);
};

} // namespace Felidae
