#include "BuiltinRegistry.h"

#include "SentencePieceModel.h"

#include <cstddef>
#include <functional>
#include <sentencepiece.pb.h>
#include <sentencepiece_processor.h>
#include <unordered_map>

namespace Felidae {

namespace {

constexpr BuiltinInfo kBuiltinInfos[] = {
    {BuiltinId::Unknown, "", BuiltinEffect::Volatile},
    {BuiltinId::Throw, "throw", BuiltinEffect::WritesExternalState},
    {BuiltinId::Type, "type", BuiltinEffect::Pure},
    {BuiltinId::Instanceof, "instanceof", BuiltinEffect::Pure},
    {BuiltinId::Count, "count", BuiltinEffect::Pure},
    {BuiltinId::Sum, "sum", BuiltinEffect::Pure},
    {BuiltinId::Average, "average", BuiltinEffect::Pure},
    {BuiltinId::Min, "min", BuiltinEffect::Pure},
    {BuiltinId::Max, "max", BuiltinEffect::Pure},
    {BuiltinId::Sort, "sort", BuiltinEffect::Pure},
    {BuiltinId::Search, "search", BuiltinEffect::Pure},
    {BuiltinId::Contains, "contains", BuiltinEffect::Pure},
    {BuiltinId::Lower, "lower", BuiltinEffect::Pure},
    {BuiltinId::Upper, "upper", BuiltinEffect::Pure},
    {BuiltinId::Length, "length", BuiltinEffect::Pure},
    {BuiltinId::ParseDoc, "ParseDoc", BuiltinEffect::Pure},
    {BuiltinId::StrLen, "str:len", BuiltinEffect::Pure},
    {BuiltinId::StrContains, "str:contains", BuiltinEffect::Pure},
    {BuiltinId::StrConcat, "str:concat", BuiltinEffect::Pure},
    {BuiltinId::StrJoin, "str:join", BuiltinEffect::Pure},
    {BuiltinId::StrLower, "str:lower", BuiltinEffect::Pure},
    {BuiltinId::StrUpper, "str:upper", BuiltinEffect::Pure},
    {BuiltinId::StrTrim, "str:trim", BuiltinEffect::Pure},
    {BuiltinId::StrSplit, "str:split", BuiltinEffect::Pure},
    {BuiltinId::StrReplace, "str:replace", BuiltinEffect::Pure},
    {BuiltinId::StrStartsWith, "str:startsWith", BuiltinEffect::Pure},
    {BuiltinId::StrEndsWith, "str:endsWith", BuiltinEffect::Pure},
    {BuiltinId::ArrayGet, "array:get", BuiltinEffect::Pure},
    {BuiltinId::ArrayLen, "array:len", BuiltinEffect::Pure},
    {BuiltinId::ArrayPush, "array:push", BuiltinEffect::Pure},
    {BuiltinId::FnArray, "fn:array", BuiltinEffect::Pure},
    {BuiltinId::FnPair, "fn:pair", BuiltinEffect::Pure},
    {BuiltinId::FnTuple, "fn:tuple", BuiltinEffect::Pure},
    {BuiltinId::PairFirst, "pair:first", BuiltinEffect::Pure},
    {BuiltinId::PairSecond, "pair:second", BuiltinEffect::Pure},
    {BuiltinId::ConsoleReadLine, "console:readLine",
     BuiltinEffect::ReadsExternalState},
    {BuiltinId::ConsoleInput, "console:input",
     BuiltinEffect::ReadsExternalState},
    {BuiltinId::ConsoleInputNumber, "console:inputNumber",
     BuiltinEffect::ReadsExternalState},
    {BuiltinId::ConsoleWriteLine, "console:writeLine",
     BuiltinEffect::WritesExternalState},
    {BuiltinId::ConsoleWrite, "console:write",
     BuiltinEffect::WritesExternalState},
    {BuiltinId::SystemPrint, "system:print",
     BuiltinEffect::WritesExternalState},
    {BuiltinId::SystemPrintf, "system:printf",
     BuiltinEffect::WritesExternalState},
    {BuiltinId::SystemRun, "system:run", BuiltinEffect::Volatile},
    {BuiltinId::FileReadFile, "file:readFile",
     BuiltinEffect::ReadsExternalState},
    {BuiltinId::FileReadLines, "file:readLines",
     BuiltinEffect::ReadsExternalState},
    {BuiltinId::FileReadLine, "file:readLine",
     BuiltinEffect::ReadsExternalState},
    {BuiltinId::FileWriteFile, "file:writeFile",
     BuiltinEffect::WritesExternalState},
    {BuiltinId::FileWriteLines, "file:writeLines",
     BuiltinEffect::WritesExternalState},
    {BuiltinId::FileAppendFile, "file:appendFile",
     BuiltinEffect::WritesExternalState},
    {BuiltinId::FileExists, "file:exists", BuiltinEffect::ReadsExternalState},
    {BuiltinId::FileDeleteFile, "file:deleteFile",
     BuiltinEffect::WritesExternalState},
    {BuiltinId::DbSync, "db:sync", BuiltinEffect::WritesExternalState},
    {BuiltinId::CommonAncestors, "commonAncestors", BuiltinEffect::Pure},
    {BuiltinId::LowestCommonAncestor, "lowestCommonAncestor",
     BuiltinEffect::Pure},
    {BuiltinId::HighestCommonAncestor, "highestCommonAncestor",
     BuiltinEffect::Pure},
    {BuiltinId::AncestorAnalysis, "ancestorAnalysis", BuiltinEffect::Pure},
    // Mutates the fact store (see the RegisterVm.cpp dispatch case) --
    // WritesExternalState, not Pure, matching db.sync/csv.toFacts.
    {BuiltinId::PropagateFact, "propagateFact",
     BuiltinEffect::WritesExternalState},
    {BuiltinId::RelationCompare, "Relation:compare", BuiltinEffect::Pure},
    {BuiltinId::RelationFind, "Relation:find", BuiltinEffect::Pure},
    {BuiltinId::DependencySatisfied, "Dependency:satisfied",
     BuiltinEffect::Pure},
    {BuiltinId::JsonObject, "json:object", BuiltinEffect::Pure},
    {BuiltinId::JsonParse, "json:parse", BuiltinEffect::Pure},
    {BuiltinId::JsonGet, "json:get", BuiltinEffect::Pure},
    {BuiltinId::JsonHas, "json:has", BuiltinEffect::Pure},
    {BuiltinId::JsonKeys, "json:keys", BuiltinEffect::Pure},
    {BuiltinId::JsonSet, "json:set", BuiltinEffect::Pure},
    {BuiltinId::JsonRemove, "json:remove", BuiltinEffect::Pure},
    {BuiltinId::JsonToText, "json:toText", BuiltinEffect::Pure},
    {BuiltinId::VisualizeGraphJson, "visualize:graphJson",
     BuiltinEffect::ReadsExternalState},
    {BuiltinId::ThreadCreateThread, "thread:createThread",
     BuiltinEffect::WritesExternalState},
    {BuiltinId::ThreadStart, "thread:start",
     BuiltinEffect::WritesExternalState},
    {BuiltinId::ThreadPause, "thread:pause",
     BuiltinEffect::WritesExternalState},
    {BuiltinId::ThreadStop, "thread:stop", BuiltinEffect::WritesExternalState},
    {BuiltinId::ThreadStatus, "thread:status",
     BuiltinEffect::ReadsExternalState},
    {BuiltinId::ThreadResult, "thread:result",
     BuiltinEffect::ReadsExternalState},
    {BuiltinId::MathPi, "math:pi", BuiltinEffect::Pure},
    {BuiltinId::MathE, "math:e", BuiltinEffect::Pure},
    {BuiltinId::MathRandom, "math:random", BuiltinEffect::Volatile},
    {BuiltinId::MathPow, "math:pow", BuiltinEffect::Pure},
    {BuiltinId::MathAtan2, "math:atan2", BuiltinEffect::Pure},
    {BuiltinId::MathSqrt, "math:sqrt", BuiltinEffect::Pure},
    {BuiltinId::MathSin, "math:sin", BuiltinEffect::Pure},
    {BuiltinId::MathCos, "math:cos", BuiltinEffect::Pure},
    {BuiltinId::MathTan, "math:tan", BuiltinEffect::Pure},
    {BuiltinId::MathAsin, "math:asin", BuiltinEffect::Pure},
    {BuiltinId::MathAcos, "math:acos", BuiltinEffect::Pure},
    {BuiltinId::MathAtan, "math:atan", BuiltinEffect::Pure},
    {BuiltinId::MathLog, "math:log", BuiltinEffect::Pure},
    {BuiltinId::MathLog10, "math:log10", BuiltinEffect::Pure},
    {BuiltinId::MathExp, "math:exp", BuiltinEffect::Pure},
    {BuiltinId::MathAbs, "math:abs", BuiltinEffect::Pure},
    {BuiltinId::MathFloor, "math:floor", BuiltinEffect::Pure},
    {BuiltinId::MathCeil, "math:ceil", BuiltinEffect::Pure},
    {BuiltinId::MathRound, "math:round", BuiltinEffect::Pure},
    {BuiltinId::MathAdd, "math:add", BuiltinEffect::Pure},
    {BuiltinId::MathSub, "math:sub", BuiltinEffect::Pure},
    {BuiltinId::MathMul, "math:mul", BuiltinEffect::Pure},
    {BuiltinId::MathDiv, "math:div", BuiltinEffect::Pure},
    {BuiltinId::MathMod, "math:mod", BuiltinEffect::Pure},
    {BuiltinId::ProbabilityMean, "probability:mean", BuiltinEffect::Pure},
    {BuiltinId::ProbabilityVariance, "probability:variance",
     BuiltinEffect::Pure},
    {BuiltinId::ProbabilityStddev, "probability:stddev", BuiltinEffect::Pure},
    {BuiltinId::ProbabilityNormalize, "probability:normalize",
     BuiltinEffect::Pure},
    {BuiltinId::ProbabilityEntropy, "probability:entropy", BuiltinEffect::Pure},
    {BuiltinId::ProbabilityCovariance, "probability:covariance",
     BuiltinEffect::Pure},
    {BuiltinId::ProbabilityCorrelation, "probability:correlation",
     BuiltinEffect::Pure},
    {BuiltinId::ProbabilityBernoulli, "probability:bernoulli",
     BuiltinEffect::Volatile},
    {BuiltinId::ProbabilityBinomialPmf, "probability:binomialPmf",
     BuiltinEffect::Pure},
    {BuiltinId::ProbabilityBinomialCdf, "probability:binomialCdf",
     BuiltinEffect::Pure},
    {BuiltinId::ProbabilityPoissonPmf, "probability:poissonPmf",
     BuiltinEffect::Pure},
    {BuiltinId::ProbabilityPoissonCdf, "probability:poissonCdf",
     BuiltinEffect::Pure},
    {BuiltinId::ProbabilityNormalPdf, "probability:normalPdf",
     BuiltinEffect::Pure},
    {BuiltinId::ProbabilityNormalCdf, "probability:normalCdf",
     BuiltinEffect::Pure},
    {BuiltinId::ProbabilityUniformPdf, "probability:uniformPdf",
     BuiltinEffect::Pure},
    {BuiltinId::ProbabilityUniformCdf, "probability:uniformCdf",
     BuiltinEffect::Pure},
    {BuiltinId::ProbabilitySample, "probability:sample",
     BuiltinEffect::Volatile},
    {BuiltinId::ProbabilityWeightedChoice, "probability:weightedChoice",
     BuiltinEffect::Volatile},
    {BuiltinId::ReasoningContrary, "Reasoning:contrary",
     BuiltinEffect::WritesExternalState},
    {BuiltinId::ReasoningProve, "Reasoning:prove",
     BuiltinEffect::ReadsExternalState},
    {BuiltinId::ReasoningGrade, "Reasoning:grade", BuiltinEffect::Pure},
    {BuiltinId::ReasoningDecide, "Reasoning:decide",
     BuiltinEffect::ReadsExternalState},
    {BuiltinId::MlSigmoid, "ml:sigmoid", BuiltinEffect::Pure},
    {BuiltinId::MlRelu, "ml:relu", BuiltinEffect::Pure},
    {BuiltinId::MlDot, "ml:dot", BuiltinEffect::Pure},
    {BuiltinId::MlMeanSquaredError, "ml:meanSquaredError", BuiltinEffect::Pure},
    {BuiltinId::OverloadAnnotation, "overload", BuiltinEffect::Pure},
    {BuiltinId::MatcherAnnotation, "matcher", BuiltinEffect::Pure},
    {BuiltinId::MixfixAnnotation, "mixfix", BuiltinEffect::Pure},
    {BuiltinId::CsvParse, "csv:parse", BuiltinEffect::Pure},
    {BuiltinId::CsvToFacts, "csv:toFacts", BuiltinEffect::Pure},
    {BuiltinId::CsvToText, "csv:toText", BuiltinEffect::Pure},
    {BuiltinId::CsvToFelidaeFacts, "csv:toFelidaeFacts", BuiltinEffect::Pure},
    {BuiltinId::GroupValidate, "Group:validate", BuiltinEffect::Pure},
    {BuiltinId::GroupClosed, "Group:closed", BuiltinEffect::Pure},
    {BuiltinId::GroupAssociative, "Group:associative", BuiltinEffect::Pure},
    {BuiltinId::GroupIdentity, "Group:identity", BuiltinEffect::Pure},
    {BuiltinId::GroupInverse, "Group:inverse", BuiltinEffect::Pure},
    {BuiltinId::GroupCommutative, "Group:commutative", BuiltinEffect::Pure},
    {BuiltinId::GroupAbelian, "Group:abelian", BuiltinEffect::Pure},
    {BuiltinId::SetUnion, "Set:union", BuiltinEffect::Pure},
    {BuiltinId::SetIntersection, "Set:intersection", BuiltinEffect::Pure},
    {BuiltinId::SetIntersectionBy, "Set:intersectionBy", BuiltinEffect::Pure},
    {BuiltinId::SetDifference, "Set:difference", BuiltinEffect::Pure},
    {BuiltinId::SetDifferenceBy, "Set:differenceBy", BuiltinEffect::Pure},
    {BuiltinId::SetSymmetricDifference, "Set:symmetricDifference",
     BuiltinEffect::Pure},
    {BuiltinId::SetSymmetricDifferenceBy, "Set:symmetricDifferenceBy",
     BuiltinEffect::Pure},
    {BuiltinId::SetEquals, "Set:equals", BuiltinEffect::Pure},
    {BuiltinId::SetEqualsBy, "Set:equalsBy", BuiltinEffect::Pure},
    {BuiltinId::SetSubset, "Set:subset", BuiltinEffect::Pure},
    {BuiltinId::SetSubsetBy, "Set:subsetBy", BuiltinEffect::Pure},
    {BuiltinId::SetSuperset, "Set:superset", BuiltinEffect::Pure},
    {BuiltinId::SetDisjoint, "Set:disjoint", BuiltinEffect::Pure},
    {BuiltinId::SetDisjointBy, "Set:disjointBy", BuiltinEffect::Pure},
    {BuiltinId::SetCardinality, "Set:cardinality", BuiltinEffect::Pure},
    {BuiltinId::SetContains, "Set:contains", BuiltinEffect::Pure},
    {BuiltinId::SetContainsBy, "Set:containsBy", BuiltinEffect::Pure},
    {BuiltinId::WhereGuardFailed, "where:guardFailed",
     BuiltinEffect::WritesExternalState}};

constexpr std::size_t builtinInfoCount() {
  return static_cast<std::size_t>(BuiltinId::Last) + 1;
}

static_assert(sizeof(kBuiltinInfos) / sizeof(kBuiltinInfos[0]) ==
                  builtinInfoCount(),
              "BuiltinId and builtin registry table must stay in lockstep");

const BuiltinInfo &builtinInfo(BuiltinId id) {
  const auto index = static_cast<std::size_t>(id);
  if (index >= builtinInfoCount())
    return kBuiltinInfos[0];
  const BuiltinInfo &info = kBuiltinInfos[index];
  return info.id == id ? info : kBuiltinInfos[0];
}

const std::unordered_map<std::string_view, BuiltinId> &builtinsByName() {
  static const std::unordered_map<std::string_view, BuiltinId> builtins = [] {
    std::unordered_map<std::string_view, BuiltinId> map;
    map.reserve(builtinInfoCount() - 1);
    map.max_load_factor(0.7f);
    for (std::size_t i = 1; i < builtinInfoCount(); ++i) {
      const BuiltinInfo *info = kBuiltinInfos + i;
      map.emplace(info->name, info->id);
    }
    return map;
  }();
  return builtins;
}

struct PieceSequenceHash {
  std::size_t operator()(const std::vector<TokenId::Id> &ids) const noexcept {
    std::size_t hash = ids.size();
    for (const auto id : ids) {
      hash ^= std::hash<TokenId::Id>{}(id) + 0x9e3779b9U + (hash << 6U) +
              (hash >> 2U);
    }
    return hash;
  }
};

std::vector<TokenId::Id> encodeBuiltinPieceIds(const std::string &spelling) {
  sentencepiece::SentencePieceText encoded;
  const auto status = felidaeSentencePieceModel().Encode(spelling, &encoded);
  if (!status.ok()) {
    throw std::runtime_error("Unable to encode Felidae builtin vocabulary");
  }
  std::vector<TokenId::Id> ids;
  ids.reserve(static_cast<std::size_t>(encoded.pieces_size()));
  for (const auto &piece : encoded.pieces()) {
    if (piece.id() == TokenId::UNKNOWN) {
      throw std::runtime_error(
          "Felidae builtin vocabulary encoded as unknown piece");
    }
    ids.push_back(static_cast<TokenId::Id>(piece.id()));
  }
  return ids;
}

const std::unordered_map<std::vector<TokenId::Id>, BuiltinId,
                         PieceSequenceHash> &
builtinsByPieceIds() {
  static const std::unordered_map<std::vector<TokenId::Id>, BuiltinId,
                                  PieceSequenceHash>
      builtins = [] {
        std::unordered_map<std::vector<TokenId::Id>, BuiltinId,
                           PieceSequenceHash>
            map;
        map.reserve((builtinInfoCount() - 1) * 2U);
        map.max_load_factor(0.7F);
        for (std::size_t index = 1; index < builtinInfoCount(); ++index) {
          const auto &info = kBuiltinInfos[index];
          const std::string canonical(info.name);
          map.emplace(encodeBuiltinPieceIds(canonical), info.id);
          std::string dotted = canonical;
          for (char &character : dotted) {
            if (character == ':')
              character = '.';
          }
          map.emplace(encodeBuiltinPieceIds(dotted), info.id);
        }
        return map;
      }();
  return builtins;
}

} // namespace

BuiltinId builtinIdForName(const std::string &name) {
  return builtinIdForName(std::string_view(name));
}

BuiltinId builtinIdForName(std::string_view name) {
  auto found = builtinsByName().find(name);
  return found == builtinsByName().end() ? BuiltinId::Unknown : found->second;
}

BuiltinId builtinIdForName(const char *name) {
  return name ? builtinIdForName(std::string_view(name)) : BuiltinId::Unknown;
}

BuiltinId builtinIdForPieceIds(const std::vector<TokenId::Id> &pieceIds) {
  const auto &builtins = builtinsByPieceIds();
  const auto found = builtins.find(pieceIds);
  return found == builtins.end() ? BuiltinId::Unknown : found->second;
}

bool isBuiltinFunctionName(const std::string &name) {
  return builtinIdForName(name) != BuiltinId::Unknown;
}

const char *builtinName(BuiltinId id) { return builtinInfo(id).name; }

BuiltinEffect builtinEffect(BuiltinId id) { return builtinInfo(id).effect; }

BuiltinEffect builtinEffect(const std::string &name) {
  return builtinEffect(builtinIdForName(name));
}

bool isBuiltinPure(BuiltinId id) {
  return builtinEffect(id) == BuiltinEffect::Pure;
}

bool isBuiltinPure(const std::string &name) {
  return isBuiltinPure(builtinIdForName(name));
}

const std::vector<BuiltinInfo> &allBuiltins() {
  static const std::vector<BuiltinInfo> builtins = [] {
    std::vector<BuiltinInfo> list;
    list.reserve(builtinInfoCount() - 1);
    for (std::size_t i = 1; i < builtinInfoCount(); ++i)
      list.push_back(kBuiltinInfos[i]);
    return list;
  }();
  return builtins;
}

} // namespace Felidae
