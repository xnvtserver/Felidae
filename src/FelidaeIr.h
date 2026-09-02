#pragma once

// Unified variable-width executable IR shared by compiler and RegisterVm.
#include <cstddef>
#include <cstdint>
namespace Felidae {
using SentencePieceId = std::size_t;
}
#include "form/NumericOperation.h"
#include "form/RegisterVm.h"
#include "form/TensorOperation.h"

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
  Builtin,
  SemanticEval,
  // [opcode, destination, type-symbol-table index, source-text index]. The
  // source index is one-based; zero creates an in-memory fact. A nonzero
  // source is emitted only for compiler-linked fact-only .fx database rows.
  MakeFact,
  MakeArray,
  MakeMap,
  GetField,
  SetField,
  Similarity,
  Membership,
  ForEachFact,
  FactJoin,
  HierarchyIsA,
  HierarchyCommonAncestors,
  HierarchyLeastCommonAncestors,
  HierarchyMostGeneralAncestors,
  TemporalRank,
  Numeric,
  Tensor,
  Return,
  Count
};

inline constexpr IrWord kIrOpcodeCount = static_cast<IrWord>(IrOpcode::Count);

enum class IrComparison : std::uint8_t {
  Equal = 0,
  NotEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual
};

struct IrConstantEntry {
  IrConstantKind kind = IrConstantKind::Number;
  IrConstant value = 0;
};

struct FelidaeIr {
  std::vector<IrWord> words;
  std::vector<IrConstantEntry> constants;
  // Complete SentencePiece sequences are the canonical text representation.
  // IR words carry only a bounded side-table index; UTF-8 exists only at
  // source and display boundaries.
  std::vector<PieceSequence> texts;
  std::vector<IrSymbolRef> symbols;
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
  static void verify(const FelidaeIr &ir);
};

} // namespace Felidae
