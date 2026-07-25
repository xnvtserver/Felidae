#include "Memory.h"

#include <algorithm>
#include <deque>
#include <iomanip>
#include <sstream>
#include <set>
#include <utility>

namespace Felidae {

FactMemory::FactMemory() : data_(std::make_shared<Data>()) {}

FactMemory::FactMemory(const FactMemory& other) : data_(other.data_) {}

FactMemory& FactMemory::operator=(const FactMemory& other) {
    if (this != &other) {
        data_ = other.data_;
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

size_t FactMemory::addFact(std::string type,
                           std::string parentType,
                           std::shared_ptr<MapExpr> value,
                           std::filesystem::path origin) {
    const SymbolId typeId = symbolIdForName(type);
    const SymbolId parentTypeId = parentType.empty() ? 0 : symbolIdForName(parentType);
    ensureUnique();
    data_->facts.push_back(FactRecord{
        std::move(type),
        typeId,
        std::move(parentType),
        parentTypeId,
        std::move(value),
        std::move(origin)});
    const size_t index = data_->facts.size() - 1;
    indexFact(index);
    invalidateCaches();
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
    data_->facts.erase(
        std::remove_if(data_->facts.begin(), data_->facts.end(), [&](const FactRecord& fact) {
            return fact.origin == origin;
        }),
        data_->facts.end());
    for (auto it = data_->parentOrigin.begin(); it != data_->parentOrigin.end();) {
        if (it->second == origin) {
            data_->parentOf.erase(it->first);
            it = data_->parentOrigin.erase(it);
        } else {
            ++it;
        }
    }
    rebuildIndexes();
}

bool FactMemory::isCompatibleType(const std::string& actual, const std::string& expected) const {
    if (actual == expected) return true;
    std::set<std::string> seen;
    std::string current = actual;
    while (!current.empty() && !seen.count(current)) {
        seen.insert(current);
        std::string parent;
        auto parentIt = data_->parentOf.find(current);
        if (parentIt != data_->parentOf.end()) parent = parentIt->second;
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
                if (index >= data_->facts.size()) continue;
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
    if (typeId == 0) typeId = symbolIdForName(type);
    if (propertyId == 0) propertyId = symbolIdForName(property);

    PropertyQueryKey queryKey{typeId, propertyId, type, property, valueKey};
    auto cached = propertyQueryCache_.find(queryKey);
    if (cached != propertyQueryCache_.end()) return cached->second;

    std::vector<size_t> indexes;
    std::unordered_set<size_t> seenIndexes;
    auto propertyIt = data_->factsByPropertyValue.find(propertyId);
    if (propertyIt != data_->factsByPropertyValue.end()) {
        auto valueIt = propertyIt->second.find(valueKey);
        if (valueIt != propertyIt->second.end()) {
            for (size_t index : valueIt->second) {
                if (index >= data_->facts.size()) continue;
                const auto& fact = data_->facts[index];
                if (!isCompatibleType(fact.type, type)) continue;
                bool exactProperty = false;
                for (const auto& entry : fact.value->entries) {
                    if (entry.keyId == propertyId && entry.key == property) {
                        exactProperty = true;
                        break;
                    }
                }
                if (exactProperty && seenIndexes.insert(index).second) indexes.push_back(index);
            }
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
    data_->factsByPropertyValue.clear();
    data_->typeNamesById.clear();
    for (size_t index = 0; index < data_->facts.size(); ++index) {
        indexFact(index);
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

void FactMemory::rememberTypeName(SymbolId id, const std::string& name) {
    auto& names = data_->typeNamesById[id];
    if (std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
}

void FactMemory::indexFact(size_t index) {
    auto& fact = data_->facts[index];
    if (fact.typeId == 0) fact.typeId = symbolIdForName(fact.type);
    if (fact.parentTypeId == 0 && !fact.parentType.empty()) {
        fact.parentTypeId = symbolIdForName(fact.parentType);
    }
    rememberTypeName(fact.typeId, fact.type);
    data_->factsByType[fact.typeId].push_back(index);
    if (!fact.origin.empty()) data_->factsByOrigin[fact.origin].push_back(index);
    for (const auto& entry : fact.value->entries) {
        if (entry.keyId == InternalSymbol::TypeId || entry.keyId == InternalSymbol::ParentId) continue;
        std::vector<std::shared_ptr<Expr>> items;
        if (auto array = std::dynamic_pointer_cast<ArrayExpr>(entry.value)) {
            items = array->items;
        } else {
            items.push_back(entry.value);
        }
        for (const auto& item : items) {
            std::string valueKey;
            if (literalIndexKey(item, valueKey)) {
                data_->factsByPropertyValue[entry.keyId][std::move(valueKey)].push_back(index);
            }
        }
    }
}

void FactMemory::ensureUnique() {
    if (data_.use_count() != 1) data_ = std::make_shared<Data>(*data_);
}

} // namespace Felidae
