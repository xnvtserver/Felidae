#include "RuntimeTraining.h"
#include "IrModule.h"
#include "SemanticOperation.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>
#include <numeric>
#include <string>
#include <system_error>

namespace Felidae {
namespace {
using Json = nlohmann::json;
constexpr std::size_t kMaximumRecords = 1'000'000;
constexpr std::size_t kMaximumItems = 1'000'000;
constexpr std::uint32_t kStructuralEncodingBit = 0x80000000u;
constexpr std::uint32_t structural(std::uint32_t marker) {
  return kStructuralEncodingBit | marker;
}

void requireJsonlPath(const std::filesystem::path &path) {
  if (path.extension() != ".jsonl")
    throw IrError("runtime training datasets must use the .jsonl extension");
}

std::uint64_t unsignedValue(const Json &value, const char *name) {
  if (!value.is_number_unsigned())
    throw IrError(std::string("runtime JSONL ") + name +
                  " must be an unsigned integer");
  return value.get<std::uint64_t>();
}

std::uint64_t member(const Json &object, const char *name) {
  if (!object.contains(name))
    throw IrError(std::string("runtime JSONL record is missing ") + name);
  return unsignedValue(object.at(name), name);
}

template <typename T> T bounded(std::uint64_t value, const char *name) {
  if (value > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
    throw IrError(std::string("runtime JSONL ") + name +
                  " exceeds the supported ID range");
  }
  return static_cast<T>(value);
}

RuntimeValueKind valueKind(std::uint64_t value, const char *name) {
  if (value < static_cast<std::uint64_t>(RuntimeValueKind::Nil) ||
      value > static_cast<std::uint64_t>(RuntimeValueKind::TextMap)) {
    throw IrError(std::string("runtime JSONL has an invalid ") + name);
  }
  return static_cast<RuntimeValueKind>(value);
}

RuntimeTrainingTargetKind targetKind(std::uint64_t value) {
  if (value < static_cast<std::uint64_t>(
                  RuntimeTrainingTargetKind::InputReference) ||
      value >
          static_cast<std::uint64_t>(RuntimeTrainingTargetKind::Score)) {
    throw IrError("runtime JSONL has an invalid target kind");
  }
  return static_cast<RuntimeTrainingTargetKind>(value);
}

RuntimeValueKind targetValueKind(const RuntimeTrainingRecord &record) {
  switch (record.targetKind) {
  case RuntimeTrainingTargetKind::InputReference:
    if (record.targetValue >= record.inputKinds.size()) {
      throw IrError("runtime JSONL input-reference target is unavailable");
    }
    return record.inputKinds[record.targetValue];
  case RuntimeTrainingTargetKind::FactFromInput:
    if (record.targetValue >= record.inputKinds.size() ||
        record.inputKinds[record.targetValue] != RuntimeValueKind::Fact) {
      throw IrError(
          "runtime JSONL fact target does not reference a fact input");
    }
    return RuntimeValueKind::Fact;
  case RuntimeTrainingTargetKind::DegreeMilli:
    return RuntimeValueKind::Degree;
  case RuntimeTrainingTargetKind::Nil:
    return RuntimeValueKind::Nil;
  case RuntimeTrainingTargetKind::NumericTruth:
    return RuntimeValueKind::Number;
  case RuntimeTrainingTargetKind::Score:
    return RuntimeValueKind::Number;
  }
  throw IrError("runtime JSONL target kind is invalid");
}

void validateTrainingContract(const RuntimeTrainingRecord &record) {
  if (!isKnownSemanticOperation(record.operationId) ||
      !semanticOperationAcceptsArity(record.operationId,
                                     record.inputKinds.size())) {
    throw IrError("runtime JSONL semantic operation contract is invalid");
  }
  const auto input = record.inputKinds.front();
  const auto output = targetValueKind(record);
  switch (static_cast<SemanticOperationId>(record.operationId)) {
  case SemanticOperationId::Identity:
    if (output != input)
      throw IrError("runtime Identity teacher changes value kind");
    return;
  case SemanticOperationId::SelectFact:
  case SemanticOperationId::DeriveFact:
    if (input != RuntimeValueKind::Fact ||
        (output != RuntimeValueKind::Fact && output != RuntimeValueKind::Nil)) {
      throw IrError("runtime fact-operation teacher has invalid kinds");
    }
    return;
  case SemanticOperationId::EvaluateDegree:
    if ((input != RuntimeValueKind::Number &&
         input != RuntimeValueKind::Degree) ||
        output != RuntimeValueKind::Degree) {
      throw IrError("runtime degree-operation teacher has invalid kinds");
    }
    return;
  case SemanticOperationId::Suggest:
    if (record.targetKind != RuntimeTrainingTargetKind::Score ||
        output != RuntimeValueKind::Number)
      throw IrError("runtime Suggest teacher must describe a numeric score");
    return;
  }
  throw IrError("runtime JSONL semantic operation ID is invalid");
}

template <typename T> std::vector<T> ids(const Json &object, const char *name) {
  if (!object.contains(name) || !object.at(name).is_array())
    throw IrError(std::string("runtime JSONL record requires array ") + name);
  const auto &source = object.at(name);
  if (source.size() > kMaximumItems)
    throw IrError(std::string("runtime JSONL ") + name + " is too large");
  std::vector<T> output;
  output.reserve(source.size());
  for (const auto &value : source)
    output.push_back(bounded<T>(unsignedValue(value, name), name));
  return output;
}

PieceSequence pieceSequence(const Json &value, const char *name) {
  if (!value.is_array() || value.empty()) {
    throw IrError(std::string("runtime JSONL ") + name +
                  " must be a non-empty SentencePiece ID array");
  }
  if (value.size() > kMaximumItems) {
    throw IrError(std::string("runtime JSONL ") + name + " is too large");
  }
  PieceSequence result;
  result.reserve(value.size());
  for (const auto &piece : value) {
    result.push_back(bounded<PieceId>(unsignedValue(piece, name), name));
  }
  return result;
}

std::vector<PieceSequence> pieceSequences(const Json &object,
                                          const char *name) {
  if (!object.contains(name) || !object.at(name).is_array()) {
    throw IrError(std::string("runtime JSONL record requires array ") + name);
  }
  const auto &source = object.at(name);
  if (source.size() > kMaximumItems) {
    throw IrError(std::string("runtime JSONL ") + name + " is too large");
  }
  std::vector<PieceSequence> result;
  result.reserve(source.size());
  for (const auto &value : source)
    result.push_back(pieceSequence(value, name));
  return result;
}

std::vector<std::pair<PieceSequence, PieceSequence>>
hierarchyEdges(const Json &object) {
  if (!object.contains("hierarchy_edges") ||
      !object.at("hierarchy_edges").is_array()) {
    throw IrError("runtime JSONL record requires array hierarchy_edges");
  }
  const auto &source = object.at("hierarchy_edges");
  if (source.size() > kMaximumItems)
    throw IrError("runtime JSONL hierarchy_edges is too large");
  std::vector<std::pair<PieceSequence, PieceSequence>> result;
  result.reserve(source.size());
  for (const auto &edge : source) {
    if (!edge.is_array() || edge.size() != 2)
      throw IrError("runtime JSONL hierarchy edge must contain child and "
                    "parent SentencePiece sequences");
    result.emplace_back(pieceSequence(edge[0], "hierarchy edge child"),
                        pieceSequence(edge[1], "hierarchy edge parent"));
  }
  return result;
}

std::vector<std::pair<PieceSequence, std::uint32_t>>
factTypeCounts(const Json &object) {
  if (!object.contains("fact_type_counts") ||
      !object.at("fact_type_counts").is_array()) {
    throw IrError("runtime JSONL record requires array fact_type_counts");
  }
  const auto &source = object.at("fact_type_counts");
  if (source.size() > kMaximumItems)
    throw IrError("runtime JSONL fact_type_counts is too large");
  std::vector<std::pair<PieceSequence, std::uint32_t>> result;
  result.reserve(source.size());
  for (const auto &item : source) {
    if (!item.is_array() || item.size() != 2)
      throw IrError("runtime JSONL fact-type count must contain a "
                    "SentencePiece sequence and count");
    result.emplace_back(
        pieceSequence(item[0], "fact-type count type"),
        bounded<std::uint32_t>(unsignedValue(item[1], "fact-type count"),
                               "fact-type count"));
  }
  if (!std::is_sorted(result.begin(), result.end()) ||
      std::adjacent_find(result.begin(), result.end(),
                         [](const auto &left, const auto &right) {
                           return left.first == right.first;
                         }) != result.end()) {
    throw IrError("runtime JSONL fact_type_counts must be sorted and unique");
  }
  return result;
}
} // namespace

std::vector<std::uint32_t>
runtimeValueEncoding(const VmValue &value,
                     std::span<const PieceSequence> symbolTable) {
  constexpr std::size_t kMaximumEncodedTokens = 4096;
  std::vector<std::uint32_t> result;
  const auto appendSymbol = [&](IrSymbolRef symbol) {
    const auto pieces = irSymbolPieces(symbolTable, symbol);
    result.insert(result.end(), pieces.begin(), pieces.end());
  };
  const auto orderedFields = [&](const auto &fields) {
    std::vector<std::size_t> order(fields.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](std::size_t left,
                                              std::size_t right) {
      const auto leftPieces = irSymbolPieces(symbolTable, fields[left].first);
      const auto rightPieces = irSymbolPieces(symbolTable, fields[right].first);
      return std::lexicographical_compare(
          leftPieces.begin(), leftPieces.end(), rightPieces.begin(),
          rightPieces.end());
    });
    return order;
  };
  const auto appendNumber = [&](double number, std::uint32_t marker) {
    result.push_back(structural(marker));
    const auto bits = std::bit_cast<std::uint64_t>(number);
    for (int bit = 63; bit >= 0; --bit)
      result.push_back(structural((bits & (std::uint64_t{1} << bit)) ? 39 : 38));
  };
  const auto append = [&](const auto &self, const VmValue &item,
                          std::size_t depth) -> void {
    if (depth > 32 || result.size() > kMaximumEncodedTokens)
      throw IrError("runtime SSM value encoding exceeds its bound");
    if (std::holds_alternative<VmNil>(item)) {
      result.push_back(structural(28));
    } else if (const auto number = std::get_if<double>(&item)) {
      appendNumber(*number, 29);
    } else if (const auto degree = std::get_if<VmDegree>(&item)) {
      appendNumber(degree->value, 30);
    } else if (const auto text = std::get_if<VmText>(&item)) {
      result.push_back(structural(31));
      result.insert(result.end(), text->pieces.begin(), text->pieces.end());
    } else if (const auto symbol = std::get_if<VmSymbol>(&item)) {
      result.push_back(structural(32));
      appendSymbol(symbol->value);
    } else if (const auto array = std::get_if<VmArrayPtr>(&item)) {
      if (!*array)
        throw IrError("runtime SSM cannot encode a null array");
      result.push_back(structural(33));
      for (const auto &value : (*array)->values)
        self(self, value, depth + 1);
      result.push_back(structural(27));
    } else if (const auto map = std::get_if<VmMapPtr>(&item)) {
      if (!*map)
        throw IrError("runtime SSM cannot encode a null map");
      result.push_back(structural(34));
      for (const auto index : orderedFields((*map)->entries)) {
        const auto &[field, value] = (*map)->entries[index];
        result.push_back(structural(40));
        appendSymbol(field);
        self(self, value, depth + 1);
      }
      result.push_back(structural(27));
    } else if (const auto fact = std::get_if<VmFactPtr>(&item)) {
      if (!*fact)
        throw IrError("runtime SSM cannot encode a null fact");
      result.push_back(structural(35));
      appendSymbol((*fact)->type);
      for (const auto index : orderedFields((*fact)->fields)) {
        const auto &[field, value] = (*fact)->fields[index];
        result.push_back(structural(40));
        appendSymbol(field);
        self(self, value, depth + 1);
      }
      result.push_back(structural(27));
    } else if (const auto tensor = std::get_if<VmTensorPtr>(&item)) {
      if (!*tensor || !(*tensor)->storage)
        throw IrError("runtime SSM cannot encode a null tensor");
      throw IrError("runtime SSM tensor inputs are unsupported; pass an "
                    "explicit fact or numeric summary");
    } else if (const auto map = std::get_if<VmTextMapPtr>(&item)) {
      if (!*map)
        throw IrError("runtime SSM cannot encode a null text map");
      result.push_back(structural(37));
      auto entries = (*map)->entries;
      std::sort(entries.begin(), entries.end(),
                [](const auto &left, const auto &right) {
                  return left.first < right.first;
                });
      for (const auto &[key, value] : entries) {
        result.push_back(structural(40));
        result.insert(result.end(), key.begin(), key.end());
        self(self, value, depth + 1);
      }
      result.push_back(structural(27));
    }
    if (result.size() > kMaximumEncodedTokens)
      throw IrError("runtime SSM value encoding exceeds its bound");
  };
  result.push_back(structural(26));
  append(append, value, 0);
  result.push_back(structural(27));
  return result;
}

void verifyRuntimeTrainingRecord(const RuntimeTrainingRecord &record) {
  if (record.inputKinds.size() > kMaximumItems ||
      record.inputValues.size() != record.inputKinds.size() ||
      record.factTypes.size() > kMaximumItems ||
      record.factTypeCounts.size() > kMaximumItems ||
      record.hierarchyEdges.size() > kMaximumItems) {
    throw IrError("runtime JSONL record is too large");
  }
  for (const auto kind : record.inputKinds) {
    (void)valueKind(static_cast<std::uint64_t>(kind), "input value kind");
  }
  for (const auto &value : record.inputValues)
    if (value.empty() || value.size() > 4096)
      throw IrError("runtime JSONL input value encoding is invalid");
  const auto validSequence = [](const PieceSequence &pieces) {
    if (pieces.empty())
      throw IrError("runtime JSONL symbol has no SentencePiece IDs");
  };
  for (const auto &type : record.factTypes)
    validSequence(type);
  for (const auto &[type, count] : record.factTypeCounts) {
    (void)count;
    validSequence(type);
  }
  for (const auto &[child, parent] : record.hierarchyEdges) {
    validSequence(child);
    validSequence(parent);
  }
  if (!std::is_sorted(record.factTypeCounts.begin(),
                      record.factTypeCounts.end()) ||
      std::adjacent_find(record.factTypeCounts.begin(),
                         record.factTypeCounts.end(),
                         [](const auto &left, const auto &right) {
                           return left.first == right.first;
                         }) != record.factTypeCounts.end()) {
    throw IrError("runtime JSONL fact_type_counts must be sorted and unique");
  }
  const auto target = targetKind(static_cast<std::uint64_t>(record.targetKind));
  if ((target == RuntimeTrainingTargetKind::NumericTruth &&
       record.targetValue > 1) ||
      (target == RuntimeTrainingTargetKind::DegreeMilli &&
       record.targetValue > 1000)) {
    throw IrError("runtime JSONL target value is invalid");
  }
  if (target == RuntimeTrainingTargetKind::Score &&
      !std::isfinite(record.targetScore))
    throw IrError("runtime JSONL score target must be finite");
  validateTrainingContract(record);
}

RuntimeKnowledgePieces
runtimeKnowledgePieces(const VmKnowledgeSnapshot &knowledge,
                       std::span<const PieceSequence> symbolTable) {
  const auto copySymbol = [&](IrSymbolRef symbol) {
    const auto pieces = irSymbolPieces(symbolTable, symbol);
    return PieceSequence(pieces.begin(), pieces.end());
  };
  RuntimeKnowledgePieces result;
  result.factTypes.reserve(knowledge.factTypes.size());
  for (const auto type : knowledge.factTypes)
    result.factTypes.push_back(copySymbol(type));
  result.factTypeCounts.reserve(knowledge.factTypeCounts.size());
  for (const auto &[type, count] : knowledge.factTypeCounts) {
    result.factTypeCounts.emplace_back(copySymbol(type), count);
  }
  result.hierarchyEdges.reserve(knowledge.hierarchyEdges.size());
  for (const auto &[child, parent] : knowledge.hierarchyEdges) {
    result.hierarchyEdges.emplace_back(copySymbol(child), copySymbol(parent));
  }
  std::sort(result.factTypes.begin(), result.factTypes.end());
  std::sort(result.factTypeCounts.begin(), result.factTypeCounts.end());
  std::sort(result.hierarchyEdges.begin(), result.hierarchyEdges.end());
  return result;
}

void writeRuntimeTrainingDataset(
    const std::filesystem::path &path,
    std::span<const RuntimeTrainingRecord> records) {
  requireJsonlPath(path);
  if (records.size() > kMaximumRecords)
    throw IrError("runtime dataset has too many records");
  const auto temporary = path.string() + ".tmp";
  std::ofstream output(temporary, std::ios::trunc);
  if (!output)
    throw IrError("cannot write runtime JSONL dataset");
  for (const auto &record : records) {
    verifyRuntimeTrainingRecord(record);
    Json inputKinds = Json::array();
    for (const auto kind : record.inputKinds)
      inputKinds.push_back(static_cast<std::uint8_t>(kind));
    Json inputValues = Json::array();
    for (const auto &value : record.inputValues)
      inputValues.push_back(value);
    Json factTypes = Json::array();
    for (const auto &type : record.factTypes)
      factTypes.push_back(type);
    Json typeCounts = Json::array();
    for (const auto &[type, count] : record.factTypeCounts)
      typeCounts.push_back({type, count});
    Json edges = Json::array();
    for (const auto &[child, parent] : record.hierarchyEdges)
      edges.push_back({child, parent});
    Json serialized{{"schema_version", kRuntimeTrainingSchemaVersion},
                   {"operation_id", record.operationId},
                   {"input_kinds", std::move(inputKinds)},
                   {"input_values", std::move(inputValues)},
                   {"fact_types", std::move(factTypes)},
                   {"fact_type_counts", std::move(typeCounts)},
                   {"hierarchy_edges", std::move(edges)},
                   {"target_kind",
                    static_cast<std::uint8_t>(record.targetKind)},
                   {"target_value", record.targetValue}};
    if (record.targetKind == RuntimeTrainingTargetKind::Score)
      serialized["target_score"] = record.targetScore;
    output << serialized.dump() << '\n';
    if (!output)
      throw IrError("cannot write runtime JSONL dataset");
  }
  output.close();
  std::error_code error;
  std::filesystem::rename(temporary, path, error);
  if (error) {
    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(temporary, path, error);
  }
  if (error)
    throw IrError("cannot finalize runtime JSONL dataset");
}

std::vector<RuntimeTrainingRecord>
loadRuntimeTrainingDataset(const std::filesystem::path &path) {
  requireJsonlPath(path);
  std::ifstream input(path);
  if (!input)
    throw IrError("cannot read runtime JSONL dataset");
  std::vector<RuntimeTrainingRecord> records;
  std::string line;
  std::size_t lineNumber = 0;
  while (std::getline(input, line)) {
    ++lineNumber;
    if (line.empty())
      continue;
    try {
      const auto object = Json::parse(line);
      if (!object.is_object())
        throw IrError("record must be an object");
      if (member(object, "schema_version") != kRuntimeTrainingSchemaVersion)
        throw IrError("schema version is incompatible");
      RuntimeTrainingRecord record;
      record.operationId = bounded<std::uint16_t>(
          member(object, "operation_id"), "operation_id");
      const auto rawInputKinds = ids<std::uint8_t>(object, "input_kinds");
      for (const auto kind : rawInputKinds)
        record.inputKinds.push_back(valueKind(kind, "input value kind"));
      if (!object.contains("input_values") ||
          !object.at("input_values").is_array())
        throw IrError("runtime JSONL record requires array input_values");
      for (const auto &encoded : object.at("input_values")) {
        if (!encoded.is_array())
          throw IrError("runtime JSONL input value must be an ID array");
        std::vector<std::uint32_t> value;
        value.reserve(encoded.size());
        for (const auto &token : encoded)
          value.push_back(bounded<std::uint32_t>(
              unsignedValue(token, "input_values"), "input_values"));
        record.inputValues.push_back(std::move(value));
      }
      record.factTypes = pieceSequences(object, "fact_types");
      record.factTypeCounts = factTypeCounts(object);
      record.hierarchyEdges = hierarchyEdges(object);
      record.targetKind = targetKind(member(object, "target_kind"));
      record.targetValue = bounded<std::uint32_t>(
          member(object, "target_value"), "target_value");
      if (record.targetKind == RuntimeTrainingTargetKind::Score) {
        if (!object.contains("target_score") ||
            !object.at("target_score").is_number())
          throw IrError("runtime JSONL score record requires target_score");
        record.targetScore = object.at("target_score").get<double>();
      }
      verifyRuntimeTrainingRecord(record);
      records.push_back(std::move(record));
      if (records.size() > kMaximumRecords)
        throw IrError("runtime dataset has too many records");
    } catch (const nlohmann::json::exception &error) {
      throw IrError("runtime JSONL line " + std::to_string(lineNumber) + ": " +
                    error.what());
    } catch (const IrError &error) {
      throw IrError("runtime JSONL line " + std::to_string(lineNumber) + ": " +
                    error.what());
    }
  }
  if (!input.eof())
    throw IrError("cannot read runtime JSONL dataset");
  return records;
}
} // namespace Felidae
