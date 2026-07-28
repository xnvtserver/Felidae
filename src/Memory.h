#pragma once

#include "AST.h"
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Felidae {

struct FactRecord {
    std::string type;
    SymbolId typeId = 0;
    std::string parentType;
    SymbolId parentTypeId = 0;
    std::shared_ptr<MapExpr> value;
    std::filesystem::path origin;
    std::size_t stableHash = 0;
};

class FactMemory {
public:
    FactMemory();
    FactMemory(const FactMemory& other);
    FactMemory& operator=(const FactMemory& other);
    FactMemory(FactMemory&&) noexcept = default;
    FactMemory& operator=(FactMemory&&) noexcept = default;

    const std::vector<FactRecord>& facts() const;
    const FactRecord& fact(size_t index) const;

    size_t addFact(std::string type,
                   std::string parentType,
                   std::shared_ptr<MapExpr> value,
                   std::filesystem::path origin = {});
    void setParent(const std::string& child, const std::string& parent);
    void setParent(const std::string& child,
                   const std::string& parent,
                   std::filesystem::path origin);
    const std::unordered_map<std::string, std::string>& parents() const;
    const std::unordered_map<std::string, std::filesystem::path>& parentOrigins() const;
    std::vector<size_t> factIndexesFromOrigin(const std::filesystem::path& origin) const;
    bool hasOrigin(const std::filesystem::path& origin) const;
    void removeOrigin(const std::filesystem::path& origin);
    bool isCompatibleType(const std::string& actual, const std::string& expected) const;
    const std::vector<size_t>& compatibleFactIndexes(const std::string& type, SymbolId typeId = 0);
    const std::vector<size_t>& propertyFactIndexes(const std::string& type,
                                                   SymbolId typeId,
                                                   const std::string& property,
                                                   SymbolId propertyId,
                                                   const std::shared_ptr<Expr>& value);
    void invalidateCaches();

private:
    struct PropertyQueryKey {
        SymbolId typeId = 0;
        SymbolId propertyId = 0;
        std::string type;
        std::string property;
        std::string value;

        bool operator==(const PropertyQueryKey& other) const {
            return typeId == other.typeId &&
                   propertyId == other.propertyId &&
                   type == other.type &&
                   property == other.property &&
                   value == other.value;
        }
    };
    struct PropertyQueryKeyHash {
        std::size_t operator()(const PropertyQueryKey& key) const;
    };

    struct Data {
        std::vector<FactRecord> facts;
        std::unordered_map<SymbolId, std::vector<size_t>> factsByType;
        std::unordered_map<std::filesystem::path, std::vector<size_t>> factsByOrigin;
        std::unordered_map<SymbolId, std::unordered_map<std::size_t, std::vector<size_t>>> factsByPropertyValue;
        std::unordered_map<std::string, std::string> parentOf;
        std::unordered_map<std::string, std::filesystem::path> parentOrigin;
        std::unordered_map<SymbolId, std::vector<SymbolId>> childrenByParent;
        std::unordered_map<SymbolId, std::vector<std::string>> typeNamesById;
    };

    std::shared_ptr<Data> data_;
    std::unordered_map<SymbolId, std::unordered_map<std::string, std::vector<size_t>>> compatibleFactCache_;
    std::unordered_map<PropertyQueryKey, std::vector<size_t>, PropertyQueryKeyHash> propertyQueryCache_;

    static bool literalIndexKey(const std::shared_ptr<Expr>& value, std::string& out);
    static bool literalIndexHash(const std::shared_ptr<Expr>& value, std::size_t& out);
    static std::size_t stableExprHash(const std::shared_ptr<Expr>& value);
    void ensureUnique();
    void indexFact(size_t index);
    void rememberTypeName(SymbolId id, const std::string& name);
    void rebuildIndexes();
};

} // namespace Felidae
