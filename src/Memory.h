#pragma once

#include "AST.h"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Felidae {

struct FactRecord {
    // Fact ids are append-only and never reused.  Vector positions are an
    // implementation detail; retaining this id makes source reloads,
    // selections and future external stores independent of compaction.
    std::uint64_t id = 0;
    std::string type;
    SymbolId typeId = 0;
    std::string parentType;
    SymbolId parentTypeId = 0;
    std::shared_ptr<MapExpr> value;
    std::filesystem::path origin;
    std::size_t stableHash = 0;
    bool active = true;
};

struct FactMemoryStats {
    std::size_t activeFacts = 0;
    std::size_t tombstonedFacts = 0;
    std::size_t relations = 0;
    std::size_t snapshots = 0;
    std::uint64_t generation = 0;
};

// Attachments deliberately live beside facts rather than in their serialized
// MapExpr values.  That keeps structural equality/source export unchanged
// while aliases of the same logical fact observe the same knowledge.
struct FactDependency {
    std::shared_ptr<MapExpr> required;
};

struct FactRelationship {
    std::uint64_t sourceId = 0;
    std::uint64_t targetId = 0;
    std::shared_ptr<MapExpr> relationship;
    std::shared_ptr<Expr> degree;
    std::shared_ptr<Expr> confidence;
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
    bool isActive(size_t index) const;
    std::vector<size_t> activeFactIndexes() const;
    std::uint64_t generation() const;
    FactMemoryStats stats() const;
    // A selection captures an immutable logical view.  It remains valid
    // until explicitly released by the runtime/library owner.
    std::uint64_t captureSnapshot();
    bool releaseSnapshot(std::uint64_t snapshotGeneration);
    std::vector<size_t> selectionIndexes(const std::string& type,
                                         const std::string& property = {},
                                         const std::shared_ptr<Expr>& value = nullptr,
                                         std::uint64_t snapshotGeneration = 0) const;
    const FactRecord& snapshotFact(std::uint64_t snapshotGeneration, size_t index) const;

    std::optional<std::uint64_t> logicalFactId(const std::shared_ptr<Expr>& value) const;
    std::shared_ptr<MapExpr> factValueById(std::uint64_t id) const;
    bool addDependency(std::uint64_t sourceId, std::shared_ptr<MapExpr> required);
    bool addRelationship(std::uint64_t sourceId,
                         std::uint64_t targetId,
                         std::shared_ptr<MapExpr> relationship,
                         std::shared_ptr<Expr> degree,
                         std::shared_ptr<Expr> confidence);
    std::vector<std::shared_ptr<MapExpr>> missingDependencies(std::uint64_t sourceId) const;
    bool hasDependencyCycle(std::uint64_t sourceId) const;
    std::vector<FactRelationship> relationshipsFor(std::uint64_t factId) const;

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
    // Prefer the automatic, relation-aware invalidation performed by the
    // mutation API.  This remains for callers that intentionally replace a
    // whole logical store.
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
        struct Relation {
            SymbolId typeId = 0;
            std::string type;
            std::uint64_t generation = 0;
            std::vector<size_t> rows;
            // These columns mirror the logical fact fields.  The MapExpr is
            // retained only for source fidelity and interop; fact matching
            // and index planning use SymbolId-indexed relation metadata.
            std::unordered_map<SymbolId, std::vector<size_t>> rowsByField;
            std::unordered_map<SymbolId,
                std::unordered_map<std::size_t, std::vector<size_t>>> equalityIndexes;
        };

        std::vector<FactRecord> facts;
        std::uint64_t nextFactId = 1;
        std::uint64_t generation = 1;
        std::unordered_map<SymbolId, std::vector<size_t>> factsByType;
        std::unordered_map<std::uint64_t, size_t> factIndexById;
        std::unordered_map<std::filesystem::path, std::vector<size_t>> factsByOrigin;
        std::unordered_map<std::string, std::string> parentOf;
        std::unordered_map<std::string, std::filesystem::path> parentOrigin;
        std::unordered_map<SymbolId, std::vector<SymbolId>> childrenByParent;
        std::unordered_map<SymbolId, std::vector<std::string>> typeNamesById;
        std::unordered_map<SymbolId, Relation> relations;
        std::unordered_map<std::uint64_t, std::vector<FactDependency>> dependenciesBySource;
        std::unordered_map<std::uint64_t, std::vector<FactRelationship>> relationshipsBySource;
        std::unordered_map<std::uint64_t, std::vector<FactRelationship>> relationshipsByTarget;
    };

    std::shared_ptr<Data> data_;
    std::unordered_map<std::uint64_t, std::shared_ptr<const Data>> snapshots_;
    std::unordered_map<SymbolId, std::unordered_map<std::string, std::vector<size_t>>> compatibleFactCache_;
    std::unordered_map<PropertyQueryKey, std::vector<size_t>, PropertyQueryKeyHash> propertyQueryCache_;

    static bool literalIndexKey(const std::shared_ptr<Expr>& value, std::string& out);
    static bool literalIndexHash(const std::shared_ptr<Expr>& value, std::size_t& out);
    static std::size_t stableExprHash(const std::shared_ptr<Expr>& value);
    static bool structurallyEqual(const std::shared_ptr<Expr>& left,
                                  const std::shared_ptr<Expr>& right);
    static bool factSatisfiesPattern(const MapExpr& fact, const MapExpr& pattern);
    static bool isCompatibleTypeInData(const Data& data,
                                       const std::string& actual,
                                       const std::string& expected);
    const Data& dataForSnapshot(std::uint64_t snapshotGeneration) const;
    void ensureUnique();
    void indexFact(size_t index);
    void deactivateFact(size_t index);
    void compactInactiveIfSafe();
    void invalidateCachesForFact(const FactRecord& fact);
    void invalidateCachesForType(const std::string& type, SymbolId typeId);
    void rememberTypeName(SymbolId id, const std::string& name);
    void rebuildIndexes();
};

} // namespace Felidae
