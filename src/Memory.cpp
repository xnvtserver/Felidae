#include "Memory.h"

#include <algorithm>
#include <cstring>
#include <deque>
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

const std::vector<FactRecord>& FactMemory::facts() const {
    return data_->facts;
}

const FactRecord& FactMemory::fact(size_t index) const {
    return data_->facts.at(index);
}

bool FactMemory::isActive(size_t index) const {
    return index < data_->facts.size() && data_->facts[index].active;
}

std::vector<size_t> FactMemory::activeFactIndexes() const {
    std::vector<size_t> indexes;
    indexes.reserve(data_->facts.size());
    for (size_t index = 0; index < data_->facts.size(); ++index) {
        if (data_->facts[index].active) indexes.push_back(index);
    }
    return indexes;
}

std::uint64_t FactMemory::generation() const {
    return data_->generation;
}

FactMemoryStats FactMemory::stats() const {
    FactMemoryStats result;
    result.relations = data_->relations.size();
    result.snapshots = snapshots_.size();
    result.generation = data_->generation;
    for (const auto& fact : data_->facts) {
        if (fact.active) ++result.activeFacts;
        else ++result.tombstonedFacts;
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
            const std::vector<size_t>* candidates = &relation->second.rows;
            if (useEqualityIndex) {
                const auto column = relation->second.equalityIndexes.find(propertyId);
                if (column == relation->second.equalityIndexes.end()) candidates = nullptr;
                else {
                    const auto matches = column->second.find(valueHash);
                    candidates = matches == column->second.end() ? nullptr : &matches->second;
                }
            }
            if (candidates) {
                for (size_t index : *candidates) {
                    if (index >= store.facts.size()) continue;
                    const auto& fact = store.facts[index];
                    if (!fact.active || !isCompatibleTypeInData(store, fact.type, type)) continue;
                    if (!property.empty()) {
                        bool exact = false;
                        for (const auto& entry : fact.value->entries) {
                            if (entry.keyId != propertyId || entry.key != property) continue;
                            if (!value) exact = true;
                            else if (auto array = std::dynamic_pointer_cast<ArrayExpr>(entry.value)) {
                                for (const auto& item : array->items) {
                                    std::string left;
                                    std::string right;
                                    if (literalIndexKey(item, left) && literalIndexKey(value, right) && left == right) {
                                        exact = true;
                                        break;
                                    }
                                }
                            } else {
                                std::string left;
                                std::string right;
                                exact = literalIndexKey(entry.value, left) && literalIndexKey(value, right) && left == right;
                            }
                            break;
                        }
                        if (!exact) continue;
                    }
                    if (seenRows.insert(index).second) result.push_back(index);
                }
            }
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
                           std::filesystem::path origin) {
    const SymbolId typeId = symbolIdForName(type);
    const SymbolId parentTypeId = parentType.empty() ? 0 : symbolIdForName(parentType);
    ensureUnique();
    data_->facts.push_back(FactRecord{
        data_->nextFactId++,
        std::move(type),
        typeId,
        std::move(parentType),
        parentTypeId,
        std::move(value),
        std::move(origin),
        0,
        true});
    const size_t index = data_->facts.size() - 1;
    data_->facts[index].stableHash = stableExprHash(data_->facts[index].value);
    indexFact(index);
    ++data_->generation;
    invalidateCachesForFact(data_->facts[index]);
    return index;
}

void FactMemory::setParent(const std::string& child, const std::string& parent) {
    ensureUnique();
    const SymbolId childId = symbolIdForName(child);
    const SymbolId parentId = symbolIdForName(parent);
    rememberTypeName(childId, child);
    rememberTypeName(parentId, parent);
    data_->parentOf[child] = parent;
    auto& children = data_->childrenByParent[parentId];
    if (std::find(children.begin(), children.end(), childId) == children.end()) {
        children.push_back(childId);
    }
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
    if (!origin.empty()) data_->parentOrigin[child] = std::move(origin);
}

const std::unordered_map<std::string, std::string>& FactMemory::parents() const {
    return data_->parentOf;
}

const std::unordered_map<std::string, std::filesystem::path>& FactMemory::parentOrigins() const {
    return data_->parentOrigin;
}

std::vector<size_t> FactMemory::factIndexesFromOrigin(const std::filesystem::path& origin) const {
    auto found = data_->factsByOrigin.find(origin);
    if (found == data_->factsByOrigin.end()) return {};
    return found->second;
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
        for (size_t index : found->second) deactivateFact(index);
        data_->factsByOrigin.erase(found);
    }
    for (auto it = data_->parentOrigin.begin(); it != data_->parentOrigin.end();) {
        if (it->second == origin) {
            data_->parentOf.erase(it->first);
            it = data_->parentOrigin.erase(it);
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
    ++data_->generation;
    invalidateCaches();
    compactInactiveIfSafe();
}

bool FactMemory::isCompatibleType(const std::string& actual, const std::string& expected) const {
    return isCompatibleTypeInData(*data_, actual, expected);
}

bool FactMemory::isCompatibleTypeInData(const Data& data,
                                        const std::string& actual,
                                        const std::string& expected) {
    if (actual == expected) return true;
    std::set<std::string> seen;
    std::string current = actual;
    while (!current.empty() && !seen.count(current)) {
        seen.insert(current);
        std::string parent;
        auto parentIt = data.parentOf.find(current);
        if (parentIt != data.parentOf.end()) parent = parentIt->second;
        if (parent == expected) return true;
        current = parent;
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

        auto direct = data_->factsByType.find(current);
        if (direct != data_->factsByType.end()) {
            for (size_t index : direct->second) {
                if (!isActive(index)) continue;
                if (isCompatibleType(data_->facts[index].type, type)) {
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
    std::size_t valueHash = 0;
    if (!literalIndexHash(value, valueHash)) return empty;
    if (typeId == 0) typeId = symbolIdForName(type);
    if (propertyId == 0) propertyId = symbolIdForName(property);

    PropertyQueryKey queryKey{typeId, propertyId, type, property, valueKey};
    auto cached = propertyQueryCache_.find(queryKey);
    if (cached != propertyQueryCache_.end()) return cached->second;

    std::vector<size_t> indexes;
    std::unordered_set<size_t> seenIndexes;
    // Read relation-local equality indexes instead of scanning a global
    // property bucket.  This keeps a query limited to the selected type and
    // its declared descendants, which is important for high-cardinality
    // analytical stores where field names are reused by many fact types.
    std::deque<SymbolId> pending;
    std::unordered_set<SymbolId> seenTypes;
    pending.push_back(typeId);
    while (!pending.empty()) {
        const SymbolId current = pending.front();
        pending.pop_front();
        if (!seenTypes.insert(current).second) continue;
        auto relation = data_->relations.find(current);
        if (relation != data_->relations.end()) {
            const auto field = relation->second.equalityIndexes.find(propertyId);
            if (field != relation->second.equalityIndexes.end()) {
                const auto matches = field->second.find(valueHash);
                if (matches != field->second.end()) {
                    for (size_t index : matches->second) {
                        if (!isActive(index)) continue;
                        const auto& fact = data_->facts[index];
                        if (!isCompatibleType(fact.type, type)) continue;
                        bool exactProperty = false;
                        for (const auto& entry : fact.value->entries) {
                            if (entry.keyId != propertyId || entry.key != property) continue;
                            if (auto array = std::dynamic_pointer_cast<ArrayExpr>(entry.value)) {
                                for (const auto& item : array->items) {
                                    std::string indexedKey;
                                    if (literalIndexKey(item, indexedKey) && indexedKey == valueKey) {
                                        exactProperty = true;
                                        break;
                                    }
                                }
                            } else {
                                std::string indexedKey;
                                exactProperty = literalIndexKey(entry.value, indexedKey) && indexedKey == valueKey;
                            }
                            break;
                        }
                        if (exactProperty && seenIndexes.insert(index).second) indexes.push_back(index);
                    }
                }
            }
        }
        auto children = data_->childrenByParent.find(current);
        if (children != data_->childrenByParent.end()) {
            for (SymbolId child : children->second) pending.push_back(child);
        }
    }
    auto inserted = propertyQueryCache_.emplace(std::move(queryKey), std::move(indexes));
    return inserted.first->second;
}

void FactMemory::invalidateCaches() {
    compatibleFactCache_.clear();
    propertyQueryCache_.clear();
}

void FactMemory::rebuildIndexes() {
    data_->factsByType.clear();
    data_->factsByOrigin.clear();
    data_->typeNamesById.clear();
    data_->relations.clear();
    for (size_t index = 0; index < data_->facts.size(); ++index) {
        if (data_->facts[index].active) indexFact(index);
    }
    data_->childrenByParent.clear();
    for (const auto& entry : data_->parentOf) {
        const SymbolId childId = symbolIdForName(entry.first);
        const SymbolId parentId = symbolIdForName(entry.second);
        rememberTypeName(childId, entry.first);
        rememberTypeName(parentId, entry.second);
        data_->childrenByParent[parentId].push_back(childId);
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

void FactMemory::indexFact(size_t index) {
    auto& fact = data_->facts[index];
    if (!fact.active || !fact.value) return;
    if (fact.typeId == 0) fact.typeId = symbolIdForName(fact.type);
    if (fact.parentTypeId == 0 && !fact.parentType.empty()) {
        fact.parentTypeId = symbolIdForName(fact.parentType);
    }
    rememberTypeName(fact.typeId, fact.type);
    data_->factsByType[fact.typeId].push_back(index);
    if (!fact.origin.empty()) data_->factsByOrigin[fact.origin].push_back(index);
    auto& relation = data_->relations[fact.typeId];
    if (relation.type.empty()) {
        relation.typeId = fact.typeId;
        relation.type = fact.type;
    }
    relation.generation = data_->generation;
    relation.rows.push_back(index);
    for (const auto& entry : fact.value->entries) {
        if (entry.keyId == InternalSymbol::TypeId || entry.keyId == InternalSymbol::ParentId) continue;
        relation.rowsByField[entry.keyId].push_back(index);
        std::vector<std::shared_ptr<Expr>> items;
        if (auto array = std::dynamic_pointer_cast<ArrayExpr>(entry.value)) {
            items = array->items;
        } else {
            items.push_back(entry.value);
        }
        for (const auto& item : items) {
            std::size_t valueHash = 0;
            if (literalIndexHash(item, valueHash)) {
                relation.equalityIndexes[entry.keyId][valueHash].push_back(index);
            }
        }
    }
}

void FactMemory::deactivateFact(size_t index) {
    if (!isActive(index)) return;
    FactRecord& fact = data_->facts[index];
    invalidateCachesForFact(fact);
    fact.active = false;
}

void FactMemory::compactInactiveIfSafe() {
    // Selections refer to vector positions in their captured Data.  Never
    // compact while a snapshot is retained; db.release makes that lifetime
    // explicit for callers that keep long-lived selections.
    if (!snapshots_.empty()) return;
    const auto tombstones = static_cast<std::size_t>(std::count_if(
        data_->facts.begin(), data_->facts.end(),
        [](const FactRecord& fact) { return !fact.active; }));
    if (tombstones == 0) return;
    constexpr std::size_t MinTombstonesBeforeCompaction = 128;
    if (tombstones < MinTombstonesBeforeCompaction &&
        tombstones * 2 < data_->facts.size()) return;

    ensureUnique();
    data_->facts.erase(
        std::remove_if(data_->facts.begin(), data_->facts.end(),
                       [](const FactRecord& fact) { return !fact.active; }),
        data_->facts.end());
    ++data_->generation;
    rebuildIndexes();
}

void FactMemory::invalidateCachesForFact(const FactRecord& fact) {
    invalidateCachesForType(fact.type, fact.typeId);

    // Property selections are precise enough to invalidate only queries for
    // values present in the changed fact.  Queries for other fields/values
    // remain reusable across fact registrations.
    for (auto cache = propertyQueryCache_.begin(); cache != propertyQueryCache_.end();) {
        const auto& key = cache->first;
        if (!isCompatibleType(fact.type, key.type) || !fact.value) {
            ++cache;
            continue;
        }
        bool matches = false;
        for (const auto& entry : fact.value->entries) {
            if (entry.keyId != key.propertyId) continue;
            std::vector<std::shared_ptr<Expr>> values;
            if (auto array = std::dynamic_pointer_cast<ArrayExpr>(entry.value)) values = array->items;
            else values.push_back(entry.value);
            for (const auto& value : values) {
                std::string literal;
                if (literalIndexKey(value, literal) && literal == key.value) {
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

} // namespace Felidae
