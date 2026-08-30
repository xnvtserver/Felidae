#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace Felidae {

// Stable semantic IDs shared by the parser, executable IR, verifier, and VM.
// These values identify operations; source spelling remains SentencePiece
// data and is resolved only by the compiler.
enum class BuiltinId : std::uint16_t {
  Unknown = 0,
  Throw,
  Type,
  Instanceof,
  Count,
  Sum,
  Average,
  Min,
  Max,
  Sort,
  Search,
  Contains,
  Lower,
  Upper,
  Length,
  ParseDoc,
  StrLen,
  StrContains,
  StrConcat,
  StrJoin,
  StrLower,
  StrUpper,
  StrTrim,
  StrSplit,
  StrReplace,
  StrStartsWith,
  StrEndsWith,
  ArrayGet,
  ArrayLen,
  ArrayPush,
  FnArray,
  FnPair,
  FnTuple,
  PairFirst,
  PairSecond,
  ConsoleReadLine,
  ConsoleInput,
  ConsoleInputNumber,
  ConsoleWriteLine,
  ConsoleWrite,
  SystemPrint,
  SystemPrintf,
  SystemRun,
  FileReadFile,
  FileReadLines,
  FileReadLine,
  FileWriteFile,
  FileWriteLines,
  FileAppendFile,
  FileExists,
  FileDeleteFile,
  DbSync,
  CommonAncestors,
  LowestCommonAncestor,
  HighestCommonAncestor,
  AncestorAnalysis,
  PropagateFact,
  RelationCompare,
  RelationFind,
  DependencySatisfied,
  JsonObject,
  JsonParse,
  JsonGet,
  JsonHas,
  JsonKeys,
  JsonSet,
  JsonRemove,
  JsonToText,
  VisualizeGraphJson,
  ThreadCreateThread,
  ThreadStart,
  ThreadPause,
  ThreadStop,
  ThreadStatus,
  ThreadResult,
  MathPi,
  MathE,
  MathRandom,
  MathPow,
  MathAtan2,
  MathSqrt,
  MathSin,
  MathCos,
  MathTan,
  MathAsin,
  MathAcos,
  MathAtan,
  MathLog,
  MathLog10,
  MathExp,
  MathAbs,
  MathFloor,
  MathCeil,
  MathRound,
  MathAdd,
  MathSub,
  MathMul,
  MathDiv,
  MathMod,
  ProbabilityMean,
  ProbabilityVariance,
  ProbabilityStddev,
  ProbabilityNormalize,
  ProbabilityEntropy,
  ProbabilityCovariance,
  ProbabilityCorrelation,
  ProbabilityBernoulli,
  ProbabilityBinomialPmf,
  ProbabilityBinomialCdf,
  ProbabilityPoissonPmf,
  ProbabilityPoissonCdf,
  ProbabilityNormalPdf,
  ProbabilityNormalCdf,
  ProbabilityUniformPdf,
  ProbabilityUniformCdf,
  ProbabilitySample,
  ProbabilityWeightedChoice,
  ReasoningContrary,
  ReasoningProve,
  ReasoningGrade,
  ReasoningDecide,
  MlSigmoid,
  MlRelu,
  MlDot,
  MlMeanSquaredError,
  OverloadAnnotation,
  MatcherAnnotation,
  MixfixAnnotation,
  CsvParse,
  CsvToFacts,
  CsvToText,
  CsvToFelidaeFacts,
  GroupValidate,
  GroupClosed,
  GroupAssociative,
  GroupIdentity,
  GroupInverse,
  GroupCommutative,
  GroupAbelian,
  SetUnion,
  SetIntersection,
  SetIntersectionBy,
  SetDifference,
  SetDifferenceBy,
  SetSymmetricDifference,
  SetSymmetricDifferenceBy,
  SetEquals,
  SetEqualsBy,
  SetSubset,
  SetSubsetBy,
  SetSuperset,
  SetDisjoint,
  SetDisjointBy,
  SetCardinality,
  SetContains,
  SetContainsBy,
  // Compiler-synthesized only (see desugarWhereGuardedClauses in
  // IrCodeGenerator.cpp): a where-guarded clause with no `else` compiles its
  // implicit else branch to this call instead of a compile-time rejection,
  // so a failed guard throws a clear runtime error rather than falling
  // through silently. Never spelled directly in source.
  WhereGuardFailed,
  Last = WhereGuardFailed
};

// Operations currently executed as direct RegisterVm library calls. This
// table is the shared arity/name contract for compiler lowering and IR
// verification; extending it must be accompanied by a real implementation.
constexpr std::optional<std::size_t>
builtinOperationArity(BuiltinId operation) noexcept {
  switch (operation) {
  case BuiltinId::Count:
  case BuiltinId::Sum:
  case BuiltinId::Average:
  case BuiltinId::Min:
  case BuiltinId::Max:
  case BuiltinId::Sort:
  case BuiltinId::ArrayLen:
  case BuiltinId::FileReadFile:
  case BuiltinId::JsonParse:
  case BuiltinId::JsonKeys:
  case BuiltinId::JsonToText:
  case BuiltinId::DbSync:
  case BuiltinId::CsvParse:
  case BuiltinId::CsvToText:
  case BuiltinId::SetUnion:
  case BuiltinId::SetIntersection:
  case BuiltinId::SetDifference:
  case BuiltinId::SetSymmetricDifference:
  case BuiltinId::SetEquals:
  case BuiltinId::SetSubset:
  case BuiltinId::SetSuperset:
  case BuiltinId::SetDisjoint:
  case BuiltinId::SetCardinality:
  case BuiltinId::MathSqrt:
  case BuiltinId::MathSin:
  case BuiltinId::MathCos:
  case BuiltinId::MathTan:
  case BuiltinId::MathAsin:
  case BuiltinId::MathAcos:
  case BuiltinId::MathAtan:
  case BuiltinId::MathLog:
  case BuiltinId::MathLog10:
  case BuiltinId::MathExp:
  case BuiltinId::MathAbs:
  case BuiltinId::MathFloor:
  case BuiltinId::MathCeil:
  case BuiltinId::MathRound:
    return 1;
  case BuiltinId::ArrayGet:
  case BuiltinId::JsonGet:
  case BuiltinId::JsonHas:
  case BuiltinId::JsonRemove:
  case BuiltinId::CsvToFacts:
  case BuiltinId::CsvToFelidaeFacts:
  case BuiltinId::GroupClosed:
  case BuiltinId::GroupAssociative:
  case BuiltinId::GroupCommutative:
  case BuiltinId::SetIntersectionBy:
  case BuiltinId::SetDifferenceBy:
  case BuiltinId::SetSymmetricDifferenceBy:
  case BuiltinId::SetEqualsBy:
  case BuiltinId::SetSubsetBy:
  case BuiltinId::SetDisjointBy:
  case BuiltinId::SetContains:
  case BuiltinId::MathRandom:
  case BuiltinId::MathPow:
  case BuiltinId::MathAtan2:
    return 2;
  case BuiltinId::JsonSet:
  case BuiltinId::GroupValidate:
  case BuiltinId::GroupIdentity:
  case BuiltinId::GroupInverse:
  case BuiltinId::GroupAbelian:
  case BuiltinId::SetContainsBy:
  case BuiltinId::PropagateFact:
    return 3;
  case BuiltinId::WhereGuardFailed:
  case BuiltinId::MathPi:
  case BuiltinId::MathE:
    return 0;
  default:
    return std::nullopt;
  }
}

constexpr std::optional<std::size_t>
builtinOperationArgumentIndex(BuiltinId operation,
                              std::string_view name) noexcept {
  switch (operation) {
  case BuiltinId::Count:
  case BuiltinId::Sum:
  case BuiltinId::Average:
  case BuiltinId::Min:
  case BuiltinId::Max:
  case BuiltinId::Sort:
  case BuiltinId::ArrayLen:
    return name == "data" ? std::optional<std::size_t>{0} : std::nullopt;
  case BuiltinId::FileReadFile:
    return name == "file" ? std::optional<std::size_t>{0} : std::nullopt;
  case BuiltinId::ArrayGet:
    if (name == "data")
      return 0;
    return name == "position" ? std::optional<std::size_t>{1}
                               : std::nullopt;
  case BuiltinId::JsonParse:
  case BuiltinId::CsvParse:
    return name == "data" ? std::optional<std::size_t>{0} : std::nullopt;
  case BuiltinId::DbSync:
    return name == "file" ? std::optional<std::size_t>{0} : std::nullopt;
  case BuiltinId::JsonGet:
  case BuiltinId::JsonHas:
  case BuiltinId::JsonRemove:
    if (name == "data")
      return 0;
    return name == "key" ? std::optional<std::size_t>{1} : std::nullopt;
  case BuiltinId::JsonKeys:
  case BuiltinId::JsonToText:
    return name == "data" ? std::optional<std::size_t>{0} : std::nullopt;
  case BuiltinId::JsonSet:
    if (name == "data")
      return 0;
    if (name == "key")
      return 1;
    return name == "value" ? std::optional<std::size_t>{2} : std::nullopt;
  case BuiltinId::CsvToText:
    return name == "data" ? std::optional<std::size_t>{0} : std::nullopt;
  case BuiltinId::CsvToFacts:
  case BuiltinId::CsvToFelidaeFacts:
    if (name == "data")
      return 0;
    return name == "type" ? std::optional<std::size_t>{1} : std::nullopt;
  case BuiltinId::GroupClosed:
  case BuiltinId::GroupAssociative:
  case BuiltinId::GroupCommutative:
  case BuiltinId::GroupValidate:
  case BuiltinId::GroupIdentity:
  case BuiltinId::GroupInverse:
  case BuiltinId::GroupAbelian:
    if (name == "set")
      return 0;
    if (name == "table")
      return 1;
    return name == "identity" &&
                   builtinOperationArity(operation).value_or(0) == 3
               ? std::optional<std::size_t>{2}
               : std::nullopt;
  case BuiltinId::SetUnion:
  case BuiltinId::SetIntersection:
  case BuiltinId::SetDifference:
  case BuiltinId::SetSymmetricDifference:
  case BuiltinId::SetEquals:
  case BuiltinId::SetSubset:
  case BuiltinId::SetSuperset:
  case BuiltinId::SetDisjoint:
    return name == "sets" ? std::optional<std::size_t>{0} : std::nullopt;
  case BuiltinId::SetCardinality:
    return name == "set" ? std::optional<std::size_t>{0} : std::nullopt;
  case BuiltinId::SetIntersectionBy:
  case BuiltinId::SetDifferenceBy:
  case BuiltinId::SetSymmetricDifferenceBy:
  case BuiltinId::SetEqualsBy:
  case BuiltinId::SetSubsetBy:
  case BuiltinId::SetDisjointBy:
    if (name == "sets")
      return 0;
    return name == "fields" ? std::optional<std::size_t>{1} : std::nullopt;
  case BuiltinId::SetContains:
  case BuiltinId::SetContainsBy:
    if (name == "set")
      return 0;
    if (name == "value")
      return 1;
    return operation == BuiltinId::SetContainsBy && name == "fields"
               ? std::optional<std::size_t>{2}
               : std::nullopt;
  case BuiltinId::MathSqrt:
  case BuiltinId::MathSin:
  case BuiltinId::MathCos:
  case BuiltinId::MathTan:
  case BuiltinId::MathAsin:
  case BuiltinId::MathAcos:
  case BuiltinId::MathAtan:
  case BuiltinId::MathLog:
  case BuiltinId::MathLog10:
  case BuiltinId::MathExp:
  case BuiltinId::MathAbs:
  case BuiltinId::MathFloor:
  case BuiltinId::MathCeil:
  case BuiltinId::MathRound:
    return name == "value" ? std::optional<std::size_t>{0} : std::nullopt;
  case BuiltinId::MathRandom:
    if (name == "min")
      return 0;
    return name == "max" ? std::optional<std::size_t>{1} : std::nullopt;
  case BuiltinId::MathPow:
    if (name == "base")
      return 0;
    return name == "exponent" ? std::optional<std::size_t>{1} : std::nullopt;
  case BuiltinId::MathAtan2:
    if (name == "y")
      return 0;
    return name == "x" ? std::optional<std::size_t>{1} : std::nullopt;
  case BuiltinId::PropagateFact:
    if (name == "parent")
      return 0;
    if (name == "child")
      return 1;
    return name == "changes" ? std::optional<std::size_t>{2} : std::nullopt;
  default:
    return std::nullopt;
  }
}

inline std::optional<std::vector<std::size_t>>
builtinOperationArgumentOrder(BuiltinId operation,
                              std::span<const std::string_view> names) {
  const auto arity = builtinOperationArity(operation);
  if (!arity || names.size() != *arity)
    return std::nullopt;
  const bool named = !names.empty() && !names.front().empty();
  std::vector<std::size_t> order;
  order.reserve(*arity);
  std::vector<bool> seen(*arity);
  for (std::size_t index = 0; index < *arity; ++index) {
    if (named != !names[index].empty())
      return std::nullopt;
    const auto target =
        named ? builtinOperationArgumentIndex(operation, names[index])
              : std::optional<std::size_t>{index};
    if (!target || seen[*target])
      return std::nullopt;
    seen[*target] = true;
    order.push_back(*target);
  }
  return order;
}

} // namespace Felidae
