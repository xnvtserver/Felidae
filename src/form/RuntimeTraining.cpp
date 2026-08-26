#include "RuntimeTraining.h"
#include "SemanticOperation.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>

namespace Felidae {
namespace {
using Json = nlohmann::json;
constexpr std::size_t kMaximumRecords = 1'000'000;
constexpr std::size_t kMaximumItems = 1'000'000;

void requireJsonlPath(const std::filesystem::path& path) {
    if (path.extension() != ".jsonl") throw IrError("runtime training datasets must use the .jsonl extension");
}

std::uint64_t unsignedValue(const Json& value, const char* name) {
    if (!value.is_number_unsigned()) throw IrError(std::string("runtime JSONL ") + name + " must be an unsigned integer");
    return value.get<std::uint64_t>();
}

std::uint64_t member(const Json& object, const char* name) {
    if (!object.contains(name)) throw IrError(std::string("runtime JSONL record is missing ") + name);
    return unsignedValue(object.at(name), name);
}

template <typename T> T bounded(std::uint64_t value, const char* name) {
    if (value > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
        throw IrError(std::string("runtime JSONL ") + name + " exceeds the supported ID range");
    }
    return static_cast<T>(value);
}

RuntimeValueKind valueKind(std::uint64_t value, const char* name) {
    if (value < static_cast<std::uint64_t>(RuntimeValueKind::Nil) || value > static_cast<std::uint64_t>(RuntimeValueKind::Symbol)) {
        throw IrError(std::string("runtime JSONL has an invalid ") + name);
    }
    return static_cast<RuntimeValueKind>(value);
}

RuntimeTrainingTargetKind targetKind(std::uint64_t value) {
    if (value < static_cast<std::uint64_t>(RuntimeTrainingTargetKind::InputReference) ||
        value > static_cast<std::uint64_t>(RuntimeTrainingTargetKind::NumericTruth)) {
        throw IrError("runtime JSONL has an invalid target kind");
    }
    return static_cast<RuntimeTrainingTargetKind>(value);
}

RuntimeValueKind targetValueKind(const RuntimeTrainingRecord& record) {
    switch (record.targetKind) {
    case RuntimeTrainingTargetKind::InputReference:
        if (record.targetValue >= record.inputKinds.size()) {
            throw IrError("runtime JSONL input-reference target is unavailable");
        }
        return record.inputKinds[record.targetValue];
    case RuntimeTrainingTargetKind::FactFromInput:
        if (record.targetValue >= record.inputKinds.size() ||
            record.inputKinds[record.targetValue] != RuntimeValueKind::Fact) {
            throw IrError("runtime JSONL fact target does not reference a fact input");
        }
        return RuntimeValueKind::Fact;
    case RuntimeTrainingTargetKind::DegreeMilli:
        return RuntimeValueKind::Degree;
    case RuntimeTrainingTargetKind::Nil:
        return RuntimeValueKind::Nil;
    case RuntimeTrainingTargetKind::NumericTruth:
        return RuntimeValueKind::Number;
    }
    throw IrError("runtime JSONL target kind is invalid");
}

void validateTrainingContract(const RuntimeTrainingRecord& record) {
    if (!isKnownSemanticOperation(record.operationId) ||
        !semanticOperationAcceptsArity(record.operationId,
                                       record.inputKinds.size())) {
        throw IrError("runtime JSONL semantic operation contract is invalid");
    }
    const auto input = record.inputKinds.front();
    const auto output = targetValueKind(record);
    switch (static_cast<SemanticOperationId>(record.operationId)) {
    case SemanticOperationId::Identity:
        if (output != input) throw IrError("runtime Identity teacher changes value kind");
        return;
    case SemanticOperationId::SelectFact:
    case SemanticOperationId::DeriveFact:
        if (input != RuntimeValueKind::Fact ||
            (output != RuntimeValueKind::Fact && output != RuntimeValueKind::Nil)) {
            throw IrError("runtime fact-operation teacher has invalid kinds");
        }
        return;
    case SemanticOperationId::EvaluateDegree:
        if ((input != RuntimeValueKind::Number && input != RuntimeValueKind::Degree) ||
            output != RuntimeValueKind::Degree) {
            throw IrError("runtime degree-operation teacher has invalid kinds");
        }
        return;
    }
    throw IrError("runtime JSONL semantic operation ID is invalid");
}

template <typename T> std::vector<T> ids(const Json& object, const char* name) {
    if (!object.contains(name) || !object.at(name).is_array()) throw IrError(std::string("runtime JSONL record requires array ") + name);
    const auto& source = object.at(name);
    if (source.size() > kMaximumItems) throw IrError(std::string("runtime JSONL ") + name + " is too large");
    std::vector<T> output;
    output.reserve(source.size());
    for (const auto& value : source) output.push_back(bounded<T>(unsignedValue(value, name), name));
    return output;
}

std::vector<std::pair<IrSymbolRef, IrSymbolRef>> hierarchyEdges(const Json& object) {
    if (!object.contains("hierarchy_edges") || !object.at("hierarchy_edges").is_array()) {
        throw IrError("runtime JSONL record requires array hierarchy_edges");
    }
    const auto& source = object.at("hierarchy_edges");
    if (source.size() > kMaximumItems) throw IrError("runtime JSONL hierarchy_edges is too large");
    std::vector<std::pair<IrSymbolRef, IrSymbolRef>> result;
    result.reserve(source.size());
    for (const auto& edge : source) {
        if (!edge.is_array() || edge.size() != 2) throw IrError("runtime JSONL hierarchy edge must contain child and parent IDs");
        const auto child = bounded<IrSymbolRef>(unsignedValue(edge[0], "hierarchy edge child"), "hierarchy edge child");
        const auto parent = bounded<IrSymbolRef>(unsignedValue(edge[1], "hierarchy edge parent"), "hierarchy edge parent");
        if (child == 0 || parent == 0) throw IrError("runtime JSONL hierarchy edge is invalid");
        result.emplace_back(child, parent);
    }
    return result;
}

std::vector<std::pair<IrSymbolRef, std::uint32_t>> factTypeCounts(const Json& object) {
    if (!object.contains("fact_type_counts") || !object.at("fact_type_counts").is_array()) {
        throw IrError("runtime JSONL record requires array fact_type_counts");
    }
    const auto& source = object.at("fact_type_counts");
    if (source.size() > kMaximumItems) throw IrError("runtime JSONL fact_type_counts is too large");
    std::vector<std::pair<IrSymbolRef, std::uint32_t>> result;
    result.reserve(source.size());
    for (const auto& item : source) {
        if (!item.is_array() || item.size() != 2) throw IrError("runtime JSONL fact-type count must contain type ID and count");
        const auto type = bounded<IrSymbolRef>(unsignedValue(item[0], "fact-type count type"), "fact-type count type");
        if (type == 0) throw IrError("runtime JSONL fact-type count has an invalid type");
        result.emplace_back(type, bounded<std::uint32_t>(unsignedValue(item[1], "fact-type count"), "fact-type count"));
    }
    if (!std::is_sorted(result.begin(), result.end()) ||
        std::adjacent_find(result.begin(), result.end(), [](const auto& left, const auto& right) { return left.first == right.first; }) != result.end()) {
        throw IrError("runtime JSONL fact_type_counts must be sorted and unique");
    }
    return result;
}
} // namespace

void verifyRuntimeTrainingRecord(const RuntimeTrainingRecord& record) {
    validateTrainingContract(record);
}

void writeRuntimeTrainingDataset(const std::filesystem::path& path, std::span<const RuntimeTrainingRecord> records) {
    requireJsonlPath(path);
    if (records.size() > kMaximumRecords) throw IrError("runtime dataset has too many records");
    const auto temporary = path.string() + ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) throw IrError("cannot write runtime JSONL dataset");
    for (const auto& record : records) {
        if (!isKnownSemanticOperation(record.operationId) ||
            !semanticOperationAcceptsArity(record.operationId, record.inputKinds.size()) ||
            record.inputKinds.size() > kMaximumItems ||
            record.factTypes.size() > kMaximumItems || record.factTypeCounts.size() > kMaximumItems ||
            record.hierarchyEdges.size() > kMaximumItems) throw IrError("runtime JSONL record is invalid or too large");
        for (const auto kind : record.inputKinds) (void)valueKind(static_cast<std::uint64_t>(kind), "input value kind");
        for (const auto type : record.factTypes) if (type == 0) throw IrError("runtime JSONL fact type is invalid");
        if (!std::is_sorted(record.factTypeCounts.begin(), record.factTypeCounts.end()) ||
            std::adjacent_find(record.factTypeCounts.begin(), record.factTypeCounts.end(), [](const auto& left, const auto& right) { return left.first == right.first; }) != record.factTypeCounts.end()) {
            throw IrError("runtime JSONL fact_type_counts must be sorted and unique");
        }
        for (const auto& [type, count] : record.factTypeCounts) {
            (void)count;
            if (type == 0) throw IrError("runtime JSONL fact-type count is invalid");
        }
        for (const auto& [child, parent] : record.hierarchyEdges) {
            if (child == 0 || parent == 0) throw IrError("runtime JSONL hierarchy edge is invalid");
        }
        const auto target = targetKind(static_cast<std::uint64_t>(record.targetKind));
        if ((target == RuntimeTrainingTargetKind::NumericTruth && record.targetValue > 1) ||
            (target == RuntimeTrainingTargetKind::DegreeMilli && record.targetValue > 1000)) {
            throw IrError("runtime JSONL target value is invalid");
        }
        verifyRuntimeTrainingRecord(record);
        Json inputKinds = Json::array();
        for (const auto kind : record.inputKinds) inputKinds.push_back(static_cast<std::uint8_t>(kind));
        Json factTypes = Json::array();
        for (const auto type : record.factTypes) factTypes.push_back(type);
        Json typeCounts = Json::array();
        for (const auto& [type, count] : record.factTypeCounts) typeCounts.push_back({type, count});
        Json edges = Json::array();
        for (const auto& [child, parent] : record.hierarchyEdges) edges.push_back({child, parent});
        output << Json{{"schema_version", kRuntimeTrainingSchemaVersion}, {"operation_id", record.operationId},
                       {"input_kinds", std::move(inputKinds)}, {"fact_types", std::move(factTypes)},
                       {"fact_type_counts", std::move(typeCounts)},
                       {"hierarchy_edges", std::move(edges)}, {"target_kind", static_cast<std::uint8_t>(target)},
                       {"target_value", record.targetValue}}.dump() << '\n';
        if (!output) throw IrError("cannot write runtime JSONL dataset");
    }
    output.close();
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporary, path, error);
    }
    if (error) throw IrError("cannot finalize runtime JSONL dataset");
}

std::vector<RuntimeTrainingRecord> loadRuntimeTrainingDataset(const std::filesystem::path& path) {
    requireJsonlPath(path);
    std::ifstream input(path);
    if (!input) throw IrError("cannot read runtime JSONL dataset");
    std::vector<RuntimeTrainingRecord> records;
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (line.empty()) continue;
        try {
            const auto object = Json::parse(line);
            if (!object.is_object()) throw IrError("record must be an object");
            if (member(object, "schema_version") != kRuntimeTrainingSchemaVersion) throw IrError("schema version is incompatible");
            RuntimeTrainingRecord record;
            record.operationId = bounded<std::uint16_t>(member(object, "operation_id"), "operation_id");
            if (!isKnownSemanticOperation(record.operationId)) {
                throw IrError("semantic operation ID is invalid");
            }
            const auto rawInputKinds = ids<std::uint8_t>(object, "input_kinds");
            for (const auto kind : rawInputKinds) record.inputKinds.push_back(valueKind(kind, "input value kind"));
            if (!semanticOperationAcceptsArity(record.operationId,
                                               record.inputKinds.size())) {
                throw IrError("semantic operation arity is invalid");
            }
            record.factTypes = ids<IrSymbolRef>(object, "fact_types");
            for (const auto type : record.factTypes) if (type == 0) throw IrError("fact type is invalid");
            record.factTypeCounts = factTypeCounts(object);
            record.hierarchyEdges = hierarchyEdges(object);
            record.targetKind = targetKind(member(object, "target_kind"));
            record.targetValue = bounded<std::uint32_t>(member(object, "target_value"), "target_value");
            if ((record.targetKind == RuntimeTrainingTargetKind::NumericTruth && record.targetValue > 1) ||
                (record.targetKind == RuntimeTrainingTargetKind::DegreeMilli && record.targetValue > 1000)) {
                throw IrError("target value is invalid");
            }
            verifyRuntimeTrainingRecord(record);
            records.push_back(std::move(record));
            if (records.size() > kMaximumRecords) throw IrError("runtime dataset has too many records");
        } catch (const nlohmann::json::exception& error) {
            throw IrError("runtime JSONL line " + std::to_string(lineNumber) + ": " + error.what());
        } catch (const IrError& error) {
            throw IrError("runtime JSONL line " + std::to_string(lineNumber) + ": " + error.what());
        }
    }
    if (!input.eof()) throw IrError("cannot read runtime JSONL dataset");
    return records;
}
} // namespace Felidae
