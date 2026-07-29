#include "Memory.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <deque>
#include <functional>
#include <iomanip>
#include <sstream>
#include <set>
#include <stdexcept>
#include <utility>

namespace Felidae {

FactMemory::FactMemory() : data_(std::make_shared<Data>()) {}

FactMemory::FactMemory(const FactMemory& other) : data_(other.data_) {}

FactMemory& FactMemory::operator=(const FactMemory& other) {
    if (this != &other) {
        data_ = other.data_;
        snapshots_.clear();
        invalidateCaches();
    }
    return *this;
}

const FactRecord& FactMemory::FactRows::at(std::size_t index) const {
    if (index >= count) throw std::out_of_range("Fact row index out of range");
    const std::size_t batchIndex = index / BatchSize;
    const std::size_t offset = index % BatchSize;
    return batches.at(batchIndex)->at(offset);
}

FactRecord& FactMemory::FactRows::mutableAt(std::size_t index) {
    if (index >= count) throw std::out_of_range("Fact row index out of range");
    const std::size_t batchIndex = index / BatchSize;
    const std::size_t offset = index % BatchSize;
    auto& batch = batches.at(batchIndex);
    if (batch.use_count() != 1) batch = std::make_shared<std::vector<FactRecord>>(*batch);
    return batch->at(offset);
}

FactRecord& FactMemory::FactRows::append(FactRecord record) {
    const std::size_t batchIndex = count / BatchSize;
    if (batchIndex == batches.size()) {
        batches.push_back(std::make_shared<std::vector<FactRecord>>());
    } else if (batches[batchIndex].use_count() != 1) {
        batches[batchIndex] = std::make_shared<std::vector<FactRecord>>(*batches[batchIndex]);
    }
    auto& batch = *batches[batchIndex];
    batch.push_back(std::move(record));
    ++count;
    return batch.back();
}

std::optional<std::size_t> FactMemory::FactIdDirectory::find(std::uint64_t id) const {
    if (id == 0) return std::nullopt;
    const std::size_t pageIndex = static_cast<std::size_t>((id - 1) / PageSize);
    const std::size_t offset = static_cast<std::size_t>((id - 1) % PageSize);
    if (pageIndex >= pages.size() || !pages[pageIndex]) return std::nullopt;
    const std::size_t row = pages[pageIndex]->at(offset);
    return row == Missing ? std::nullopt : std::optional<std::size_t>(row);
}

void FactMemory::FactIdDirectory::assign(std::uint64_t id, std::size_t row) {
    if (id == 0) throw std::invalid_argument("FactId must be non-zero");
    const std::size_t pageIndex = static_cast<std::size_t>((id - 1) / PageSize);
    const std::size_t offset = static_cast<std::size_t>((id - 1) % PageSize);
    if (pageIndex >= pages.size()) pages.resize(pageIndex + 1);
    auto& page = pages[pageIndex];
    if (!page) {
        page = std::make_shared<std::vector<std::size_t>>(PageSize, Missing);
    } else if (page.use_count() != 1) {
        page = std::make_shared<std::vector<std::size_t>>(*page);
    }
    page->at(offset) = row;
}

const FactRecord& FactMemory::fact(size_t index) const {
    return data_->facts.at(index);
}

std::shared_ptr<MapExpr> FactMemory::materializeFact(
    const Data& store,
    std::size_t index) const {
    const auto& record = store.facts.at(index);
    const auto relation = store.relations.find(record.typeId);
    if (relation == store.relations.end() || !relation->second) return {};
    const auto& root = *relation->second;
    if (record.relationOrdinal >= root.fieldOrderOffsets.size() ||
        record.relationOrdinal >= root.fieldOrderCounts.size()) return {};

    std::vector<MapEntry> entries;
    entries.emplace_back(
        internalSymbolString(InternalSymbolKind::Type),
        std::make_shared<StringExpr>(record.type));
    if (!record.parentType.empty()) {
        entries.emplace_back(
            internalSymbolString(InternalSymbolKind::Parent),
            std::make_shared<StringExpr>(record.parentType));
    }

    const std::size_t offset = root.fieldOrderOffsets[record.relationOrdinal];
    const std::size_t count = root.fieldOrderCounts[record.relationOrdinal];
    std::unordered_map<SymbolId, std::size_t> consumed;
    entries.reserve(entries.size() + count);
    for (std::size_t position = 0; position < count; ++position) {
        const std::size_t orderIndex = offset + position;
        const SymbolId fieldId = root.fieldOrder[orderIndex];
        const auto name = root.fieldNames.find(fieldId);
        const auto column = root.columns.find(fieldId);
        if (name == root.fieldNames.end() || column == root.columns.end()) continue;
        const std::size_t first = consumed[fieldId];
        const std::size_t valueCount =
            orderIndex < root.fieldValueCounts.size()
                ? root.fieldValueCounts[orderIndex] : 1;
        std::vector<std::shared_ptr<Expr>> values;
        values.reserve(valueCount);
        std::size_t current = 0;
        column->second.forRow(index, [&](const std::shared_ptr<const Expr>& value) {
            if (current >= first && current < first + valueCount && value) {
                values.push_back(std::const_pointer_cast<Expr>(value)->clone());
            }
            ++current;
        });
        consumed[fieldId] += valueCount;
        const bool wasArray =
            orderIndex < root.fieldArrayFlags.size() &&
            root.fieldArrayFlags[orderIndex] != 0;
        if (wasArray) {
            entries.emplace_back(name->second, std::make_shared<ArrayExpr>(std::move(values)));
        } else if (!values.empty()) {
            entries.emplace_back(name->second, std::move(values.front()));
        }
    }

    // Inherited values are a resolved view, never duplicated into the child
    // relation. The first active row of each declared parent remains the
    // language's type-default fact, matching the prior constructor behavior.
    std::vector<std::string> parentTypes;
    const auto primaryParent = store.parentOf.find(record.type);
    if (primaryParent != store.parentOf.end()) {
        parentTypes.push_back(primaryParent->second);
    } else if (!record.parentType.empty()) {
        parentTypes.push_back(record.parentType);
    }
    const auto additionalParents =
        store.additionalParentsOf.find(record.type);
    if (additionalParents != store.additionalParentsOf.end()) {
        parentTypes.insert(
            parentTypes.end(),
            additionalParents->second.begin(),
            additionalParents->second.end());
    }
    for (const auto& parentType : parentTypes) {
        const SymbolId parentId = symbolIdForName(parentType);
        const auto parentRelation = store.relations.find(parentId);
        if (parentRelation == store.relations.end() ||
            !parentRelation->second) continue;
        const auto parentRow = std::find_if(
            parentRelation->second->rows.begin(),
            parentRelation->second->rows.end(),
            [&](const std::size_t candidate) {
                return candidate < store.facts.size() &&
                    store.facts.at(candidate).active &&
                    store.facts.at(candidate).type == parentType;
            });
        if (parentRow == parentRelation->second->rows.end()) continue;
        const auto parent = materializeFact(store, *parentRow);
        if (!parent) continue;
        for (const auto& inherited : parent->entries) {
            if (inherited.keyId == InternalSymbol::TypeId ||
                inherited.keyId == InternalSymbol::ParentId) continue;
            const auto existing = std::find_if(
                entries.begin(), entries.end(),
                [&](const MapEntry& entry) {
                    return entry.keyId == inherited.keyId &&
                           entry.key == inherited.key;
                });
            if (existing == entries.end()) {
                entries.emplace_back(
                    inherited.key, inherited.value->clone());
            }
        }
    }
    auto result = std::make_shared<MapExpr>(std::move(entries));
    result->factIdentity = record.id;
    return result;
}

std::shared_ptr<MapExpr> FactMemory::factValue(
    size_t index,
    std::uint64_t snapshotGeneration) const {
    return materializeFact(dataForSnapshot(snapshotGeneration), index);
}

std::vector<Arg> FactMemory::factArguments(size_t index) const {
    const auto& record = data_->facts.at(index);
    if (!record.parentType.empty() ||
        data_->parentOf.count(record.type) != 0 ||
        data_->additionalParentsOf.count(record.type) != 0) {
        const auto resolved = materializeFact(*data_, index);
        std::vector<Arg> args;
        if (!resolved) return args;
        for (const auto& entry : resolved->entries) {
            if (entry.keyId == InternalSymbol::TypeId ||
                entry.keyId == InternalSymbol::ParentId) continue;
            if (const auto array =
                    std::dynamic_pointer_cast<ArrayExpr>(entry.value)) {
                for (const auto& item : array->items) {
                    args.emplace_back(entry.key, item);
                }
            } else {
                args.emplace_back(entry.key, entry.value);
            }
        }
        return args;
    }
    const auto relation = data_->relations.find(record.typeId);
    if (relation == data_->relations.end() || !relation->second ||
        record.relationOrdinal >= relation->second->fieldOrderOffsets.size() ||
        record.relationOrdinal >= relation->second->fieldOrderCounts.size()) return {};
    const std::size_t offset =
        relation->second->fieldOrderOffsets[record.relationOrdinal];
    const std::size_t count =
        relation->second->fieldOrderCounts[record.relationOrdinal];
    std::vector<Arg> args;
    args.reserve(count);
    std::unordered_map<SymbolId, std::size_t> consumed;
    for (std::size_t position = 0; position < count; ++position) {
        const std::size_t orderIndex = offset + position;
        const SymbolId fieldId = relation->second->fieldOrder[orderIndex];
        const auto name = relation->second->fieldNames.find(fieldId);
        const auto column = relation->second->columns.find(fieldId);
        if (name == relation->second->fieldNames.end() ||
            column == relation->second->columns.end()) continue;
        const std::size_t first = consumed[fieldId];
        const std::size_t valueCount =
            orderIndex < relation->second->fieldValueCounts.size()
                ? relation->second->fieldValueCounts[orderIndex] : 1;
        std::size_t current = 0;
        column->second.forRow(index, [&](const std::shared_ptr<const Expr>& value) {
            if (current >= first && current < first + valueCount && value) {
                args.push_back(Arg{name->second, std::const_pointer_cast<Expr>(value)});
            }
            ++current;
        });
        consumed[fieldId] += valueCount;
    }
    return args;
}

bool FactMemory::isActive(size_t index) const {
    return index < data_->facts.size() && data_->facts.at(index).active;
}

std::vector<size_t> FactMemory::activeFactIndexes() const {
    std::vector<size_t> indexes;
    indexes.reserve(data_->facts.size());
    for (size_t index = 0; index < data_->facts.size(); ++index) {
        if (data_->facts.at(index).active) indexes.push_back(index);
    }
    return indexes;
}

bool FactMemory::hasActiveRelation(const std::string& type, SymbolId typeId) const {
    if (typeId == 0) typeId = symbolIdForName(type);
    const auto relation = data_->relations.find(typeId);
    if (relation == data_->relations.end() || !relation->second || relation->second->type != type) return false;
    return std::any_of(relation->second->rows.begin(), relation->second->rows.end(),
                       [&](size_t index) { return index < data_->facts.size() && data_->facts.at(index).active; });
}

std::uint64_t FactMemory::generation() const {
    return data_->generation;
}

std::uint64_t FactMemory::relationGeneration(
    const std::string& type,
    SymbolId typeId) const {
    if (typeId == 0) typeId = symbolIdForName(type);
    const auto relation = data_->relations.find(typeId);
    if (relation == data_->relations.end() || !relation->second ||
        relation->second->type != type) return 0;
    return relation->second->generation;
}

std::uint64_t FactMemory::hierarchyGeneration() const {
    return data_->hierarchyGeneration;
}

FactMemoryStats FactMemory::stats() const {
    FactMemoryStats result;
    result.relations = data_->relations.size();
    result.snapshots = snapshots_.size();
    result.generation = data_->generation;
    result.adaptiveEqualityIndexes = adaptiveEqualityIndexes_;
    result.adaptiveIndexBuildMicros = adaptiveIndexBuildMicros_;
    for (std::size_t index = 0; index < data_->facts.size(); ++index) {
        const auto& fact = data_->facts.at(index);
        ++result.rowVersions;
        if (fact.active) ++result.activeFacts;
        else ++result.tombstonedFacts;
    }
    for (const auto& entry : data_->relations) {
        if (!entry.second) continue;
        result.relationRows += entry.second->rows.size();
        for (const auto& column : entry.second->columns) {
            result.relationColumnValues += column.second.values.size();
        }
    }
    if (data_->valueArena) {
        result.internedValues = data_->valueArena->valueCount;
    }
    return result;
}

std::uint64_t FactMemory::captureSnapshot() {
    const std::uint64_t snapshot = data_->generation;
    snapshots_[snapshot] = data_;
    return snapshot;
}

bool FactMemory::releaseSnapshot(std::uint64_t snapshotGeneration) {
    if (snapshotGeneration == 0) return false;
    const bool released = snapshots_.erase(snapshotGeneration) != 0;
    if (released) compactInactiveIfSafe();
    return released;
}

const FactMemory::Data& FactMemory::dataForSnapshot(std::uint64_t snapshotGeneration) const {
    if (snapshotGeneration == 0 || snapshotGeneration == data_->generation) return *data_;
    const auto found = snapshots_.find(snapshotGeneration);
    if (found == snapshots_.end()) {
        throw std::runtime_error("FactSelection snapshot expired; materialize or recreate the selection");
    }
    return *found->second;
}

const FactRecord& FactMemory::snapshotFact(std::uint64_t snapshotGeneration, size_t index) const {
    return dataForSnapshot(snapshotGeneration).facts.at(index);
}

bool FactMemory::structurallyEqual(const std::shared_ptr<Expr>& left,
                                   const std::shared_ptr<Expr>& right) {
    if (!left || !right || left->kind() != right->kind()) return false;
    if (const auto l = std::dynamic_pointer_cast<StringExpr>(left)) {
        return l->value == std::static_pointer_cast<StringExpr>(right)->value;
    }
    if (const auto l = std::dynamic_pointer_cast<NumberExpr>(left)) {
        return l->value == std::static_pointer_cast<NumberExpr>(right)->value;
    }
    if (const auto l = std::dynamic_pointer_cast<BoolExpr>(left)) {
        return l->value == std::static_pointer_cast<BoolExpr>(right)->value;
    }
    if (std::dynamic_pointer_cast<NilExpr>(left)) return true;
    if (const auto l = std::dynamic_pointer_cast<ArrayExpr>(left)) {
        const auto r = std::static_pointer_cast<ArrayExpr>(right);
        if (l->items.size() != r->items.size()) return false;
        for (std::size_t index = 0; index < l->items.size(); ++index) {
            if (!structurallyEqual(l->items[index], r->items[index])) return false;
        }
        return true;
    }
    if (const auto l = std::dynamic_pointer_cast<MapExpr>(left)) {
        const auto r = std::static_pointer_cast<MapExpr>(right);
        if (l->entries.size() != r->entries.size()) return false;
        for (const auto& wanted : l->entries) {
            const auto found = std::find_if(
                r->entries.begin(), r->entries.end(),
                [&](const MapEntry& item) {
                    return wanted.keyId == item.keyId &&
                           wanted.key == item.key;
                });
            if (found == r->entries.end() ||
                !structurallyEqual(wanted.value, found->value)) return false;
        }
        return true;
    }
    if (const auto l = std::dynamic_pointer_cast<TermExpr>(left)) {
        const auto r = std::static_pointer_cast<TermExpr>(right);
        if (l->nameId != r->nameId || l->name != r->name ||
            l->args.size() != r->args.size()) return false;
        for (std::size_t index = 0; index < l->args.size(); ++index) {
            if (l->args[index].nameId != r->args[index].nameId ||
                l->args[index].name != r->args[index].name ||
                !structurallyEqual(
                    l->args[index].value, r->args[index].value)) return false;
        }
        return true;
    }
    return left->debug() == right->debug();
}

bool FactMemory::factSatisfiesPattern(const MapExpr& fact, const MapExpr& pattern) {
    for (const auto& wanted : pattern.entries) {
        if (wanted.key == internalSymbolString(InternalSymbolKind::Parent)) continue;
        bool matched = false;
        for (const auto& actual : fact.entries) {
            if (actual.keyId != wanted.keyId || actual.key != wanted.key) continue;
            matched = structurallyEqual(actual.value, wanted.value);
            break;
        }
        if (!matched) return false;
    }
    return true;
}

std::optional<std::uint64_t> FactMemory::logicalFactId(const std::shared_ptr<Expr>& value) const {
    const auto pattern = std::dynamic_pointer_cast<MapExpr>(value);
    if (!pattern) return std::nullopt;
    if (pattern->factIdentity != 0) {
        const auto indexed = data_->factIndexById.find(pattern->factIdentity);
        if (!indexed || !data_->facts.at(*indexed).active) {
            return std::nullopt;
        }
        return pattern->factIdentity;
    }
    const auto typeValue = std::dynamic_pointer_cast<StringExpr>(
        [&]() -> std::shared_ptr<Expr> {
            for (const auto& entry : pattern->entries) {
                if (entry.key == internalSymbolString(InternalSymbolKind::Type)) return entry.value;
            }
            return nullptr;
        }());
    if (!typeValue || typeValue->value == "Comparison") return std::nullopt;

    // Choose the most selective literal predicate without assigning semantic
    // meaning to any user field name.  `id` is not special in Felidae: every
    // fact schema is free to use its own fields or no scalar key at all.
    std::vector<size_t> rows;
    bool hasLiteralPredicate = false;
    for (const auto& entry : pattern->entries) {
        if (entry.key == internalSymbolString(InternalSymbolKind::Type) ||
            entry.key == internalSymbolString(InternalSymbolKind::Parent)) continue;
        std::string ignored;
        if (!literalIndexKey(entry.value, ignored)) continue;
        const auto candidates = selectionIndexes(typeValue->value, entry.key, entry.value);
        if (!hasLiteralPredicate || candidates.size() < rows.size()) rows = candidates;
        hasLiteralPredicate = true;
    }
    if (!hasLiteralPredicate) rows = selectionIndexes(typeValue->value);
    std::optional<std::uint64_t> matched;
    for (size_t index : rows) {
        const auto& fact = data_->facts.at(index);
        if (!fact.active) continue;
        const auto materialized = materializeFact(*data_, index);
        if (!materialized || !factSatisfiesPattern(*materialized, *pattern)) continue;
        // A reconstructed map has no durable reference to one of several
        // structurally equal rows.  Refuse to choose arbitrarily: callers can
        // obtain a fact through Fact.all/select, which carries this hidden id.
        if (matched) return std::nullopt;
        matched = fact.id;
    }
    return matched;
}

std::shared_ptr<MapExpr> FactMemory::factValueById(std::uint64_t id) const {
    const auto indexed = data_->factIndexById.find(id);
    if (!indexed) return {};
    const auto& fact = data_->facts.at(*indexed);
    if (!fact.active) return {};
    return materializeFact(*data_, *indexed);
}

bool FactMemory::addDependency(std::uint64_t sourceId, std::shared_ptr<MapExpr> required) {
    if (sourceId == 0 || !required) return false;
    ensureUnique();
    ensureAttachmentsUnique();
    auto& dependencies = data_->attachments->dependenciesBySource[sourceId];
    for (const auto& dependency : dependencies) {
        if (structurallyEqual(dependency.required, required)) return true;
    }
    dependencies.push_back(FactDependency{std::move(required)});
    ++data_->generation;
    return true;
}

bool FactMemory::addRelationship(std::uint64_t sourceId,
                                 std::uint64_t targetId,
                                 std::shared_ptr<MapExpr> relationship,
                                 std::shared_ptr<Expr> degree,
                                 std::shared_ptr<Expr> confidence) {
    if (sourceId == 0 || targetId == 0 || !relationship) return false;
    ensureUnique();
    ensureAttachmentsUnique();
    auto& outgoing = data_->attachments->relationshipsBySource[sourceId];
    for (const auto& existing : outgoing) {
        if (existing.targetId == targetId &&
            structurallyEqual(existing.relationship, relationship) &&
            structurallyEqual(existing.degree, degree) &&
            structurallyEqual(existing.confidence, confidence)) {
            return true;
        }
    }
    FactRelationship record{sourceId, targetId, std::move(relationship), std::move(degree), std::move(confidence)};
    outgoing.push_back(record);
    data_->attachments->relationshipsByTarget[targetId].push_back(record);
    ++data_->generation;
    return true;
}

std::vector<std::shared_ptr<MapExpr>> FactMemory::missingDependencies(std::uint64_t sourceId) const {
    std::vector<std::shared_ptr<MapExpr>> missing;
    const auto found = data_->attachments->dependenciesBySource.find(sourceId);
    if (found == data_->attachments->dependenciesBySource.end()) return missing;
    for (const auto& dependency : found->second) {
        bool satisfied = false;
        std::string requiredType;
        for (const auto& entry : dependency.required->entries) {
            if (entry.key == internalSymbolString(InternalSymbolKind::Type)) {
                if (const auto type = std::dynamic_pointer_cast<StringExpr>(entry.value)) {
                    requiredType = type->value;
                }
            }
        }
        std::vector<size_t> candidates;
        bool hasLiteralPredicate = false;
        for (const auto& entry : dependency.required->entries) {
            if (entry.key == internalSymbolString(InternalSymbolKind::Type)) continue;
            if (entry.key == internalSymbolString(InternalSymbolKind::Parent)) continue;
            std::string ignored;
            if (!literalIndexKey(entry.value, ignored)) continue;
            if (requiredType.empty()) continue;
            const auto indexed = selectionIndexes(requiredType, entry.key, entry.value);
            if (!hasLiteralPredicate || indexed.size() < candidates.size()) candidates = indexed;
            hasLiteralPredicate = true;
        }
        if (!hasLiteralPredicate) {
            candidates = requiredType.empty() ? activeFactIndexes() : selectionIndexes(requiredType);
        }
        for (size_t candidate : candidates) {
            const auto& fact = data_->facts.at(candidate);
            const auto materialized =
                fact.active ? materializeFact(*data_, candidate) : nullptr;
            if (materialized &&
                factSatisfiesPattern(*materialized, *dependency.required)) {
                satisfied = true;
                break;
            }
        }
        if (!satisfied) {
            missing.push_back(std::static_pointer_cast<MapExpr>(dependency.required->clone()));
        }
    }
    return missing;
}

bool FactMemory::hasDependencyCycle(std::uint64_t sourceId) const {
    std::unordered_map<std::uint64_t, const FactRecord*> byId;
    byId.reserve(data_->facts.size());
    for (std::size_t index = 0; index < data_->facts.size(); ++index) {
        const auto& fact = data_->facts.at(index);
        if (fact.active) byId.emplace(fact.id, &fact);
    }
    std::unordered_set<std::uint64_t> visiting;
    std::unordered_set<std::uint64_t> visited;
    std::function<bool(std::uint64_t)> visit = [&](std::uint64_t current) {
        if (!visiting.insert(current).second) return true;
        if (visited.count(current)) {
            visiting.erase(current);
            return false;
        }
        const auto dependencies = data_->attachments->dependenciesBySource.find(current);
        if (dependencies != data_->attachments->dependenciesBySource.end()) {
            for (const auto& dependency : dependencies->second) {
                for (const auto& candidate : byId) {
                    const auto row = data_->factIndexById.find(candidate.first);
                    const auto materialized =
                        row ? materializeFact(*data_, *row) : nullptr;
                    if (materialized &&
                        factSatisfiesPattern(*materialized, *dependency.required) &&
                        visit(candidate.first)) {
                        return true;
                    }
                }
            }
        }
        visiting.erase(current);
        visited.insert(current);
        return false;
    };
    return visit(sourceId);
}

std::vector<FactRelationship> FactMemory::relationshipsFor(std::uint64_t factId) const {
    std::vector<FactRelationship> result;
    const auto outgoing = data_->attachments->relationshipsBySource.find(factId);
    if (outgoing != data_->attachments->relationshipsBySource.end()) {
        result.insert(result.end(), outgoing->second.begin(), outgoing->second.end());
    }
    const auto incoming = data_->attachments->relationshipsByTarget.find(factId);
    if (incoming != data_->attachments->relationshipsByTarget.end()) {
        result.insert(result.end(), incoming->second.begin(), incoming->second.end());
    }
    return result;
}

std::vector<size_t> FactMemory::selectionIndexes(const std::string& type,
                                                  const std::string& property,
                                                  const std::shared_ptr<Expr>& value,
                                                  std::uint64_t snapshotGeneration) const {
    const Data& store = dataForSnapshot(snapshotGeneration);
    const SymbolId typeId = symbolIdForName(type);
    const SymbolId propertyId = property.empty() ? 0 : symbolIdForName(property);
    std::size_t valueHash = 0;
    const bool useEqualityIndex = !property.empty() && value && literalIndexHash(value, valueHash);

    std::vector<size_t> result;
    std::unordered_set<size_t> seenRows;
    std::deque<SymbolId> pending;
    std::unordered_set<SymbolId> seenTypes;
    pending.push_back(typeId);
    while (!pending.empty()) {
        const SymbolId current = pending.front();
        pending.pop_front();
        if (!seenTypes.insert(current).second) continue;

        const auto relation = store.relations.find(current);
        if (relation != store.relations.end()) {
            std::optional<Data::Relation::RowList> indexedCandidates;
            const std::vector<size_t>* candidates = &relation->second->rows;
            if (useEqualityIndex) {
                const auto column = relation->second->columns.find(propertyId);
                const bool inheritsFields =
                    store.parentOf.count(relation->second->type) != 0 ||
                    store.additionalParentsOf.count(
                        relation->second->type) != 0;
                if (column == relation->second->columns.end() ||
                    inheritsFields) {
                    // A missing direct column can still resolve through the
                    // hierarchy. Likewise, an index over explicit overrides
                    // cannot exclude rows that inherit the requested value.
                    candidates = &relation->second->rows;
                }
                else {
                    const auto started = std::chrono::steady_clock::now();
                    const auto equality = column->second.equalityIndex;
                    std::lock_guard<std::mutex> lock(equality->mutex);
                    ++equality->queryCount;
                    // A one-off analytical predicate is cheaper as a compact
                    // column scan. Build the retained equality index only
                    // after repeated demand demonstrates reuse.
                    constexpr std::size_t EqualityIndexBuildThreshold = 3;
                    if (!equality->built &&
                        equality->queryCount >= EqualityIndexBuildThreshold) {
                        for (std::size_t offset = 0;
                             offset < column->second.values.size();
                             ++offset) {
                            std::size_t hash = 0;
                            const auto& item = column->second.values[offset];
                            if (item && literalIndexHash(
                                    std::const_pointer_cast<Expr>(item), hash)) {
                                equality->rowsByHash[hash].append(
                                    column->second.rows[offset]);
                            }
                        }
                        equality->built = true;
                        ++adaptiveEqualityIndexes_;
                        adaptiveIndexBuildMicros_ += static_cast<std::size_t>(
                            std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now() - started).count());
                    }
                    if (equality->built) {
                        const auto matches =
                            equality->rowsByHash.find(valueHash);
                        if (matches == equality->rowsByHash.end()) {
                            candidates = nullptr;
                        } else {
                            indexedCandidates = matches->second;
                        }
                    } else {
                        // A cold equality query still scans the compact column,
                        // not every relation row followed by a binary search
                        // back into that column.  The latter turns an O(n)
                        // cold lookup into O(n log n) and dominated million-row
                        // startup queries.
                        Data::Relation::RowList matches;
                        for (std::size_t offset = 0;
                             offset < column->second.values.size();
                             ++offset) {
                            std::size_t hash = 0;
                            const auto& item = column->second.values[offset];
                            if (item && literalIndexHash(
                                    std::const_pointer_cast<Expr>(item), hash) &&
                                hash == valueHash) {
                                matches.append(column->second.rows[offset]);
                            }
                        }
                        indexedCandidates = std::move(matches);
                    }
                }
            }
            const auto visitCandidate = [&](size_t index) {
                    if (index >= store.facts.size()) return;
                    const auto& fact = store.facts.at(index);
                    if (!fact.active || !isCompatibleTypeInData(store, fact.type, type)) return;
                    if (!property.empty()) {
                        const auto column = relation->second->columns.find(propertyId);
                        bool exact = !value;
                        if (column != relation->second->columns.end()) {
                            column->second.forRow(index, [&](const std::shared_ptr<const Expr>& item) {
                                if (exact) return;
                                if (static_cast<bool>(item) &&
                                    (!value || structurallyEqual(
                                        std::const_pointer_cast<Expr>(item), value))) {
                                    exact = true;
                                }
                            });
                        }
                        if (!exact) {
                            const auto resolved =
                                materializeFact(store, index);
                            if (resolved) {
                                const auto field = std::find_if(
                                    resolved->entries.begin(),
                                    resolved->entries.end(),
                                    [&](const MapEntry& entry) {
                                        return entry.keyId == propertyId &&
                                               entry.key == property;
                                    });
                                exact = field != resolved->entries.end() &&
                                    (!value || structurallyEqual(
                                        field->value, value));
                            }
                        }
                        if (!exact) return;
                    }
                    if (seenRows.insert(index).second) result.push_back(index);
            };
            if (indexedCandidates) indexedCandidates->forEach(visitCandidate);
            else if (candidates) for (size_t index : *candidates) visitCandidate(index);
        }
        const auto children = store.childrenByParent.find(current);
        if (children != store.childrenByParent.end()) {
            for (SymbolId child : children->second) pending.push_back(child);
        }
    }
    return result;
}

size_t FactMemory::addFact(std::string type,
                           std::string parentType,
                           std::shared_ptr<MapExpr> value,
                           std::filesystem::path origin,
                           std::optional<std::uint64_t> logicalId,
                           std::uint64_t rowVersion) {
    if (!value) throw std::invalid_argument("Fact value cannot be null");
    const SymbolId typeId = symbolIdForName(type);
    const SymbolId parentTypeId = parentType.empty() ? 0 : symbolIdForName(parentType);
    const std::size_t structuralHash = stableExprHash(value);
    ensureUnique();
    const std::uint64_t factId = logicalId ? *logicalId : data_->nextFactId++;
    if (logicalId && *logicalId >= data_->nextFactId) data_->nextFactId = *logicalId + 1;
    auto& record = data_->facts.append(FactRecord{
        factId,
        std::move(type),
        typeId,
        std::move(parentType),
        parentTypeId,
        std::move(origin),
        structuralHash,
        rowVersion,
        data_->generation + 1,
        0,
        true});
    const size_t index = data_->facts.size() - 1;
    value->factIdentity = record.id;
    indexFact(index, value);
    ++data_->generation;
    invalidateCachesForFact(record, value);
    return index;
}

void FactMemory::setParent(const std::string& child, const std::string& parent) {
    if (child.empty() || parent.empty()) return;
    ensureUnique();
    const auto existing = parentsOf(child);
    if (std::find(existing.begin(), existing.end(), parent) != existing.end()) return;
    if (child == parent || isCompatibleType(parent, child)) {
        throw std::invalid_argument("Inheritance cycle: '" + child + "' cannot extend '" + parent + "'");
    }
    const SymbolId childId = symbolIdForName(child);
    const SymbolId parentId = symbolIdForName(parent);
    rememberTypeName(childId, child);
    rememberTypeName(parentId, parent);
    if (!data_->parentOf.count(child)) data_->parentOf[child] = parent;
    else data_->additionalParentsOf[child].push_back(parent);
    auto& children = data_->childrenByParent[parentId];
    if (std::find(children.begin(), children.end(), childId) == children.end()) {
        children.push_back(childId);
    }
    ++data_->hierarchyGeneration;
    ++data_->generation;
    // A parent declaration changes which rows match both type and property
    // queries.  The child fact may have been registered before this edge was
    // known, so cached property candidates for the parent are stale too.
    invalidateCaches();
}

void FactMemory::setParent(const std::string& child,
                           const std::string& parent,
                           std::filesystem::path origin) {
    setParent(child, parent);
    if (origin.empty()) return;
    if (data_->parentOf[child] == parent) data_->parentOrigin[child] = std::move(origin);
    else data_->additionalParentOrigins[child + "\x1f" + parent] = std::move(origin);
}

const std::unordered_map<std::string, std::string>& FactMemory::parents() const {
    return data_->parentOf;
}

std::vector<std::string> FactMemory::parentsOf(const std::string& child) const {
    std::vector<std::string> result;
    const auto primary = data_->parentOf.find(child);
    if (primary != data_->parentOf.end()) result.push_back(primary->second);
    const auto additional = data_->additionalParentsOf.find(child);
    if (additional != data_->additionalParentsOf.end()) {
        result.insert(result.end(), additional->second.begin(), additional->second.end());
    }
    return result;
}

std::vector<std::pair<std::string, std::string>> FactMemory::hierarchyEdges() const {
    std::vector<std::pair<std::string, std::string>> result;
    for (const auto& entry : data_->parentOf) result.emplace_back(entry.first, entry.second);
    for (const auto& entry : data_->additionalParentsOf) {
        for (const auto& parent : entry.second) result.emplace_back(entry.first, parent);
    }
    return result;
}

const std::unordered_map<std::string, std::filesystem::path>& FactMemory::parentOrigins() const {
    return data_->parentOrigin;
}

std::vector<size_t> FactMemory::factIndexesFromOrigin(const std::filesystem::path& origin) const {
    auto found = data_->factsByOrigin.find(origin);
    if (found == data_->factsByOrigin.end() || !found->second) return {};
    return *found->second;
}

bool FactMemory::hasOrigin(const std::filesystem::path& origin) const {
    return data_->factsByOrigin.count(origin) > 0;
}

void FactMemory::removeOrigin(const std::filesystem::path& origin) {
    if (origin.empty()) return;
    ensureUnique();
    const auto found = data_->factsByOrigin.find(origin);
    if (found != data_->factsByOrigin.end()) {
        // Rows are immutable once published.  Removing a source marks its
        // rows inactive, preserving stable ids held by any live selection.
        // New facts from a sync are appended as a new generation.
        for (size_t index : *found->second) deactivateFact(index);
        data_->factsByOrigin.erase(found);
    }
    for (auto it = data_->additionalParentOrigins.begin(); it != data_->additionalParentOrigins.end();) {
        if (it->second == origin) {
            const std::string edge = it->first;
            const size_t separator = edge.find('\x1f');
            if (separator != std::string::npos) {
                const std::string child = edge.substr(0, separator);
                const std::string parent = edge.substr(separator + 1);
                auto additional = data_->additionalParentsOf.find(child);
                if (additional != data_->additionalParentsOf.end()) {
                    auto& values = additional->second;
                    values.erase(std::remove(values.begin(), values.end(), parent), values.end());
                    if (values.empty()) data_->additionalParentsOf.erase(additional);
                }
            }
            it = data_->additionalParentOrigins.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = data_->parentOrigin.begin(); it != data_->parentOrigin.end();) {
        if (it->second == origin) {
            const std::string child = it->first;
            data_->parentOf.erase(child);
            it = data_->parentOrigin.erase(it);
            const auto additional = data_->additionalParentsOf.find(child);
            if (additional != data_->additionalParentsOf.end() && !additional->second.empty()) {
                const std::string promoted = additional->second.front();
                additional->second.erase(additional->second.begin());
                data_->parentOf[child] = promoted;
                const std::string key = child + "\x1f" + promoted;
                const auto promotedOrigin = data_->additionalParentOrigins.find(key);
                if (promotedOrigin != data_->additionalParentOrigins.end()) {
                    data_->parentOrigin[child] = promotedOrigin->second;
                    data_->additionalParentOrigins.erase(promotedOrigin);
                }
                if (additional->second.empty()) data_->additionalParentsOf.erase(additional);
            }
        } else {
            ++it;
        }
    }
    data_->childrenByParent.clear();
    for (const auto& entry : data_->parentOf) {
        const SymbolId childId = symbolIdForName(entry.first);
        const SymbolId parentId = symbolIdForName(entry.second);
        data_->childrenByParent[parentId].push_back(childId);
    }
    for (const auto& entry : data_->additionalParentsOf) {
        const SymbolId childId = symbolIdForName(entry.first);
        for (const auto& parent : entry.second) {
            data_->childrenByParent[symbolIdForName(parent)].push_back(childId);
        }
    }
    ++data_->generation;
    ++data_->hierarchyGeneration;
    invalidateCaches();
    compactInactiveIfSafe();
}

bool FactMemory::isCompatibleType(const std::string& actual, const std::string& expected) const {
    return isCompatibleTypeInData(*data_, actual, expected);
}

bool FactMemory::isCompatibleTypeInData(const Data& data,
                                        const std::string& actual,
                                        const std::string& expected) {
    if (expected == "Fact") return !actual.empty();
    if (actual == expected) return true;
    std::set<std::string> seen;
    std::vector<std::string> pending{actual};
    for (size_t index = 0; index < pending.size(); ++index) {
        // Copy before appending parents: vector growth must not invalidate the
        // current node while multi-parent traversal is expanding its frontier.
        const std::string current = pending[index];
        if (!seen.insert(current).second) continue;
        const auto primary = data.parentOf.find(current);
        if (primary != data.parentOf.end()) {
            if (primary->second == expected) return true;
            pending.push_back(primary->second);
        }
        const auto additional = data.additionalParentsOf.find(current);
        if (additional != data.additionalParentsOf.end()) {
            for (const auto& parent : additional->second) {
                if (parent == expected) return true;
                pending.push_back(parent);
            }
        }
    }
    return false;
}

const std::vector<size_t>& FactMemory::compatibleFactIndexes(const std::string& type, SymbolId typeId) {
    if (typeId == 0) typeId = symbolIdForName(type);
    auto cacheBucket = compatibleFactCache_.find(typeId);
    if (cacheBucket != compatibleFactCache_.end()) {
        auto cached = cacheBucket->second.find(type);
        if (cached != cacheBucket->second.end()) return cached->second;
    }

    std::vector<size_t> indexes;
    std::deque<SymbolId> pending;
    std::unordered_set<SymbolId> seenTypes;
    pending.push_back(typeId);
    while (!pending.empty()) {
        const SymbolId current = pending.front();
        pending.pop_front();
        if (!seenTypes.insert(current).second) continue;

        const auto direct = data_->relations.find(current);
        if (direct != data_->relations.end() && direct->second) {
            for (size_t index : direct->second->rows) {
                if (!isActive(index)) continue;
                if (isCompatibleType(data_->facts.at(index).type, type)) {
                    indexes.push_back(index);
                }
            }
        }
        auto children = data_->childrenByParent.find(current);
        if (children != data_->childrenByParent.end()) {
            for (SymbolId child : children->second) pending.push_back(child);
        }
    }
    auto inserted = compatibleFactCache_[typeId].emplace(type, std::move(indexes));
    return inserted.first->second;
}

const std::vector<size_t>& FactMemory::propertyFactIndexes(
    const std::string& type,
    SymbolId typeId,
    const std::string& property,
    SymbolId propertyId,
    const std::shared_ptr<Expr>& value) {
    static const std::vector<size_t> empty;
    std::string valueKey;
    if (!literalIndexKey(value, valueKey)) return empty;
    std::size_t ignoredHash = 0;
    if (!literalIndexHash(value, ignoredHash)) return empty;
    if (typeId == 0) typeId = symbolIdForName(type);
    if (propertyId == 0) propertyId = symbolIdForName(property);

    PropertyQueryKey queryKey{typeId, propertyId, type, property, valueKey};
    auto cached = propertyQueryCache_.find(queryKey);
    if (cached != propertyQueryCache_.end()) return cached->second;

    // selectionIndexes owns adaptive index construction and structural
    // confirmation. Keep this cache as the stable query-plan result only.
    std::vector<size_t> indexes =
        selectionIndexes(type, property, value);
    auto inserted = propertyQueryCache_.emplace(std::move(queryKey), std::move(indexes));
    return inserted.first->second;
}

void FactMemory::invalidateCaches() {
    compatibleFactCache_.clear();
    propertyQueryCache_.clear();
}

void FactMemory::rebuildIndexes(
    const std::vector<std::shared_ptr<MapExpr>>& values) {
    data_->factIndexById.clear();
    data_->factsByOrigin.clear();
    data_->typeNamesById.clear();
    data_->relations.clear();
    for (size_t index = 0; index < data_->facts.size(); ++index) {
        if (data_->facts.at(index).active && index < values.size()) {
            indexFact(index, values[index]);
        }
    }
    data_->childrenByParent.clear();
    for (const auto& entry : data_->parentOf) {
        const SymbolId childId = symbolIdForName(entry.first);
        const SymbolId parentId = symbolIdForName(entry.second);
        rememberTypeName(childId, entry.first);
        rememberTypeName(parentId, entry.second);
        data_->childrenByParent[parentId].push_back(childId);
    }
    for (const auto& entry : data_->additionalParentsOf) {
        const SymbolId childId = symbolIdForName(entry.first);
        for (const auto& parent : entry.second) {
            const SymbolId parentId = symbolIdForName(parent);
            rememberTypeName(parentId, parent);
            data_->childrenByParent[parentId].push_back(childId);
        }
    }
    invalidateCaches();
}

std::size_t FactMemory::PropertyQueryKeyHash::operator()(const PropertyQueryKey& key) const {
    std::size_t seed = std::hash<SymbolId>{}(key.typeId);
    seed ^= std::hash<SymbolId>{}(key.propertyId) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
    seed ^= std::hash<std::string>{}(key.type) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
    seed ^= std::hash<std::string>{}(key.property) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
    seed ^= std::hash<std::string>{}(key.value) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
    return seed;
}

bool FactMemory::literalIndexKey(const std::shared_ptr<Expr>& value, std::string& out) {
    if (!value) return false;
    switch (value->kind()) {
        case ExprKind::String:
            out = "s:" + static_cast<const StringExpr&>(*value).value;
            return true;
        case ExprKind::Number: {
            std::ostringstream text;
            text << "n:" << std::setprecision(17) << static_cast<const NumberExpr&>(*value).value;
            out = text.str();
            return true;
        }
        case ExprKind::Bool:
            out = static_cast<const BoolExpr&>(*value).value ? "b:1" : "b:0";
            return true;
        case ExprKind::Nil:
            out = "z:";
            return true;
        default:
            return false;
    }
}

bool FactMemory::literalIndexHash(
    const std::shared_ptr<Expr>& value,
    std::size_t& out) {
    if (!value) return false;
    constexpr std::size_t offset =
        sizeof(std::size_t) == 8 ? 1469598103934665603ULL : 2166136261U;
    constexpr std::size_t prime =
        sizeof(std::size_t) == 8 ? 1099511628211ULL : 16777619U;
    auto mix = [&](const void* data, std::size_t size, unsigned char kind) {
        std::size_t hash = (offset ^ kind) * prime;
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * prime;
        out = hash;
    };
    switch (value->kind()) {
        case ExprKind::String: {
            const auto& text = static_cast<const StringExpr&>(*value).value;
            mix(text.data(), text.size(), 1);
            return true;
        }
        case ExprKind::Number: {
            const double number = static_cast<const NumberExpr&>(*value).value;
            std::uint64_t bits = 0;
            std::memcpy(&bits, &number, sizeof(bits));
            mix(&bits, sizeof(bits), 2);
            return true;
        }
        case ExprKind::Bool: {
            const bool boolean = static_cast<const BoolExpr&>(*value).value;
            mix(&boolean, sizeof(boolean), 3);
            return true;
        }
        case ExprKind::Nil:
            mix(nullptr, 0, 4);
            return true;
        default:
            return false;
    }
}

std::size_t FactMemory::stableExprHash(const std::shared_ptr<Expr>& value) {
    if (!value) return 0;
    std::size_t literal = 0;
    if (literalIndexHash(value, literal)) return literal;
    std::size_t seed = static_cast<std::size_t>(value->kind()) + 0x9e3779b9U;
    auto combine = [&](std::size_t hash) {
        seed ^= hash + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
    };
    if (auto array = std::dynamic_pointer_cast<ArrayExpr>(value)) {
        for (const auto& item : array->items) combine(stableExprHash(item));
    } else if (auto map = std::dynamic_pointer_cast<MapExpr>(value)) {
        for (const auto& entry : map->entries) {
            combine(std::hash<std::string>{}(entry.key));
            combine(stableExprHash(entry.value));
        }
    } else if (auto term = std::dynamic_pointer_cast<TermExpr>(value)) {
        combine(std::hash<std::string>{}(term->name));
        for (const auto& argument : term->args) {
            combine(std::hash<std::string>{}(argument.name));
            combine(stableExprHash(argument.value));
        }
    } else {
        combine(std::hash<std::string>{}(value->debug()));
    }
    return seed;
}

void FactMemory::rememberTypeName(SymbolId id, const std::string& name) {
    auto& names = data_->typeNamesById[id];
    if (std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
}

std::shared_ptr<const Expr> FactMemory::internValue(
    const std::shared_ptr<Expr>& value) {
    if (!value) return {};
    // Numeric columns in analytical stores are commonly high-cardinality
    // measures and identifiers. Hash-consing those values adds a hash-table
    // node and lock/search work without producing sharing.
    if (value->kind() == ExprKind::Number) {
        return std::shared_ptr<const Expr>(value->clone());
    }
    if (!data_->valueArena) data_->valueArena = std::make_shared<Data::ValueArena>();
    const std::size_t hash = stableExprHash(value);
    auto& candidates = data_->valueArena->valuesByHash[hash];
    for (const auto& candidate : candidates) {
        if (candidate && structurallyEqual(
                std::const_pointer_cast<Expr>(candidate), value)) {
            return candidate;
        }
    }
    auto stored = std::shared_ptr<const Expr>(value->clone());
    candidates.push_back(stored);
    ++data_->valueArena->valueCount;
    return stored;
}

void FactMemory::indexFact(
    size_t index,
    const std::shared_ptr<MapExpr>& value) {
    auto& fact = data_->facts.mutableAt(index);
    if (!fact.active || !value) return;
    if (fact.typeId == 0) fact.typeId = symbolIdForName(fact.type);
    if (fact.parentTypeId == 0 && !fact.parentType.empty()) {
        fact.parentTypeId = symbolIdForName(fact.parentType);
    }
    rememberTypeName(fact.typeId, fact.type);
    data_->factIndexById.assign(fact.id, index);
    if (!fact.origin.empty()) {
        auto& rows = data_->factsByOrigin[fact.origin];
        if (!rows) rows = std::make_shared<std::vector<size_t>>();
        else if (rows.use_count() != 1) rows = std::make_shared<std::vector<size_t>>(*rows);
        rows->push_back(index);
    }
    auto& relation = writableRelation(fact.typeId);
    if (relation.type.empty()) {
        relation.typeId = fact.typeId;
        relation.type = fact.type;
    }
    relation.generation = data_->generation + 1;
    fact.relationOrdinal = relation.rows.size();
    relation.rows.push_back(index);
    relation.fieldOrderOffsets.push_back(relation.fieldOrder.size());
    std::size_t visibleFieldCount = 0;
    for (const auto& entry : value->entries) {
        if (entry.keyId == InternalSymbol::TypeId || entry.keyId == InternalSymbol::ParentId) continue;
        ++visibleFieldCount;
        relation.fieldNames.emplace(entry.keyId, entry.key);
        relation.fieldOrder.push_back(entry.keyId);
        std::vector<std::shared_ptr<Expr>> items;
        const auto array = std::dynamic_pointer_cast<ArrayExpr>(entry.value);
        if (array) {
            items = array->items;
        } else {
            items.push_back(entry.value);
        }
        relation.fieldValueCounts.push_back(items.size());
        relation.fieldArrayFlags.push_back(array ? 1U : 0U);
        auto& column = relation.columns[entry.keyId];
        const std::size_t presenceWord = fact.relationOrdinal / 64;
        if (column.presence.size() <= presenceWord) {
            column.presence.resize(presenceWord + 1, 0);
        }
        column.presence[presenceWord] |=
            std::uint64_t{1} << (fact.relationOrdinal % 64);
        for (const auto& item : items) {
            const auto stored = internValue(item);
            column.rows.push_back(index);
            column.values.push_back(stored);
            const auto observedKind = [&]() {
                if (!stored) return Data::Relation::ColumnKind::Empty;
                switch (stored->kind()) {
                    case ExprKind::String: return Data::Relation::ColumnKind::String;
                    case ExprKind::Number: return Data::Relation::ColumnKind::Number;
                    case ExprKind::Bool: return Data::Relation::ColumnKind::Bool;
                    case ExprKind::Nil: return Data::Relation::ColumnKind::Nil;
                    default: return Data::Relation::ColumnKind::Structured;
                }
            }();
            if (column.kind == Data::Relation::ColumnKind::Empty) {
                column.kind = observedKind;
            } else if (observedKind != Data::Relation::ColumnKind::Empty &&
                       column.kind != observedKind) {
                column.kind = Data::Relation::ColumnKind::Mixed;
            }
            if (column.equalityIndex &&
                column.equalityIndex.use_count() != 1) {
                auto detached =
                    std::make_shared<Data::Relation::FieldColumn::EqualityIndex>();
                {
                    std::lock_guard<std::mutex> lock(
                        column.equalityIndex->mutex);
                    detached->built = column.equalityIndex->built;
                    detached->queryCount =
                        column.equalityIndex->queryCount;
                    detached->rowsByHash =
                        column.equalityIndex->rowsByHash;
                }
                column.equalityIndex = std::move(detached);
            }
            if (column.equalityIndex && column.equalityIndex->built) {
                std::size_t valueHash = 0;
                if (literalIndexHash(
                        std::const_pointer_cast<Expr>(stored), valueHash)) {
                    column.equalityIndex->rowsByHash[valueHash].append(index);
                }
            }
        }
    }
    relation.fieldOrderCounts.push_back(visibleFieldCount);
}

void FactMemory::deactivateFact(size_t index) {
    if (!isActive(index)) return;
    FactRecord& fact = data_->facts.mutableAt(index);
    invalidateCachesForFact(fact, materializeFact(*data_, index));
    fact.active = false;
}

void FactMemory::compactInactiveIfSafe() {
    // Selections refer to vector positions in their captured Data.  Never
    // compact while a snapshot is retained; db.release makes that lifetime
    // explicit for callers that keep long-lived selections.
    if (!snapshots_.empty()) return;
    std::size_t tombstones = 0;
    for (std::size_t index = 0; index < data_->facts.size(); ++index) {
        if (!data_->facts.at(index).active) ++tombstones;
    }
    if (tombstones == 0) return;
    constexpr std::size_t MinTombstonesBeforeCompaction = 128;
    if (tombstones < MinTombstonesBeforeCompaction &&
        tombstones * 2 < data_->facts.size()) return;

    ensureUnique();
    FactRows compacted;
    std::vector<std::shared_ptr<MapExpr>> compactedValues;
    compactedValues.reserve(data_->facts.size() - tombstones);
    for (std::size_t index = 0; index < data_->facts.size(); ++index) {
        const auto& fact = data_->facts.at(index);
        if (fact.active) {
            compactedValues.push_back(materializeFact(*data_, index));
            compacted.append(fact);
        }
    }
    data_->facts = std::move(compacted);
    ++data_->generation;
    rebuildIndexes(compactedValues);
}

void FactMemory::invalidateCachesForFact(
    const FactRecord& fact,
    const std::shared_ptr<MapExpr>& value) {
    invalidateCachesForType(fact.type, fact.typeId);

    // Property selections are precise enough to invalidate only queries for
    // values present in the changed fact.  Queries for other fields/values
    // remain reusable across fact registrations.
    for (auto cache = propertyQueryCache_.begin(); cache != propertyQueryCache_.end();) {
        const auto& key = cache->first;
        if (!isCompatibleType(fact.type, key.type) || !value) {
            ++cache;
            continue;
        }
        bool matches = false;
        for (const auto& entry : value->entries) {
            if (entry.keyId != key.propertyId) continue;
            std::vector<std::shared_ptr<Expr>> values;
            if (auto array = std::dynamic_pointer_cast<ArrayExpr>(entry.value)) values = array->items;
            else values.push_back(entry.value);
            for (const auto& itemValue : values) {
                std::string literal;
                if (literalIndexKey(itemValue, literal) && literal == key.value) {
                    matches = true;
                    break;
                }
            }
            if (matches) break;
        }
        if (matches) cache = propertyQueryCache_.erase(cache);
        else ++cache;
    }
}

void FactMemory::invalidateCachesForType(const std::string& type, SymbolId typeId) {
    (void)typeId;
    for (auto bucket = compatibleFactCache_.begin(); bucket != compatibleFactCache_.end();) {
        auto& byName = bucket->second;
        for (auto entry = byName.begin(); entry != byName.end();) {
            if (isCompatibleType(type, entry->first)) entry = byName.erase(entry);
            else ++entry;
        }
        if (byName.empty()) bucket = compatibleFactCache_.erase(bucket);
        else ++bucket;
    }
}

void FactMemory::ensureUnique() {
    if (data_.use_count() != 1) data_ = std::make_shared<Data>(*data_);
}

void FactMemory::ensureAttachmentsUnique() {
    if (!data_->attachments) {
        data_->attachments = std::make_shared<AttachmentData>();
    } else if (data_->attachments.use_count() != 1) {
        data_->attachments = std::make_shared<AttachmentData>(*data_->attachments);
    }
}

FactMemory::Data::Relation& FactMemory::writableRelation(SymbolId typeId) {
    auto& relation = data_->relations[typeId];
    if (!relation) {
        relation = std::make_shared<Data::Relation>();
    } else if (relation.use_count() != 1) {
        relation = std::make_shared<Data::Relation>(*relation);
    }
    return *relation;
}

} // namespace Felidae
