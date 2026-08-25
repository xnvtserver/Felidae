#pragma once

// Internal compiler IR. Runtime entry points include RegisterVm.h or
// BinaryIsa.h and never accept these instruction words.
#include <cstddef>
namespace Felidae { using SentencePieceId = std::size_t; }
#include "form/RegisterVm.h"

namespace Felidae {

enum class IrOpcode : IrWord {
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

enum class IrOperandKind : IrWord { Register = 1, Constant, Symbol, Fact, Jump, Program };
enum class IrComparison : IrWord { Equal = 0, NotEqual, Less, LessEqual, Greater, GreaterEqual };

struct FelidaeIr {
    std::vector<IrWord> words;
    std::vector<IrWord> constants;
    std::vector<IrConstantKind> constantKinds;
    // Text is a dedicated UTF-8 side table: compiler IR words carry only its
    // bounded index. SentencePiece IDs remain private to the frontend.
    std::vector<std::string> texts;
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
