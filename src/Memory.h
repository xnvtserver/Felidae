#pragma once

#include "AST.h"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Felidae {

enum class TemporalState : std::uint8_t {
    Past,
    Current,
    Future
};

enum class TemporalOrigin : std::uint8_t {
    Observed,
    Scheduled,
    Derived,
    Predicted,
    Required
};

struct FactTemporalMetadata {
    // Effective time is Unix seconds when explicitly supplied by a recognized
    // field, otherwise registration time. Sequence makes same-second writes
    // deterministic without altering the user-visible fact schema.
    std::int64_t effectiveTime = 0;
    std::int64_t registrationTime = 0;
    std::uint64_t registrationSequence = 0;
    bool hasExplicitEffectiveTime = false;
    TemporalOrigin origin = TemporalOrigin::Observed;
    std::string lineageKey;
};

struct FactRecord {
    // Fact ids are append-only and never reused.  Vector positions are an
    // implementation detail; retaining this id makes source reloads,
    // selections and future external stores independent of compaction.
    std::uint64_t id = 0;
    std::string type;
    SymbolId typeId = 0;
    std::string parentType;
    SymbolId parentTypeId = 0;
    std::vector<std::uint64_t> parentFactIds;
    std::vector<SymbolId> designations;
    std::filesystem::path origin;
    std::size_t stableHash = 0;
    // Logical identity and physical row version are intentionally separate.
    // Updating a fact retains id and publishes a later immutable row version.
    std::uint64_t rowVersion = 1;
    std::uint64_t visibleGeneration = 0;
    FactTemporalMetadata temporal;
    // Stable position inside the immutable relation root. This is distinct
    // from the global row index and lets column access remain contiguous.
    std::size_t relationOrdinal = 0;
    bool active = true;
};

struct FactMemoryStats {
    std::size_t activeFacts = 0;
    std::size_t tombstonedFacts = 0;
    std::size_t relations = 0;
    std::size_t snapshots = 0;
    std::size_t relationRows = 0;
    std::size_t relationColumnValues = 0;
    std::size_t internedValues = 0;
    std::size_t adaptiveEqualityIndexes = 0;
    std::size_t adaptiveIndexBuildMicros = 0;
    std::size_t rowVersions = 0;
    std::size_t temporalLineages = 0;
    std::size_t temporalPastFacts = 0;
    std::size_t temporalFutureFacts = 0;
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

    const FactRecord& fact(size_t index) const;
    std::shared_ptr<MapExpr> factValue(size_t index,
                                       std::uint64_t snapshotGeneration = 0) const;
    std::vector<Arg> factArguments(size_t index) const;
    bool isActive(size_t index) const;
    std::vector<size_t> activeFactIndexes() const;
    bool hasActiveRelation(const std::string& type, SymbolId typeId = 0) const;
    std::uint64_t generation() const;
    std::uint64_t relationGeneration(const std::string& type,
                                     SymbolId typeId = 0) const;
    std::uint64_t hierarchyGeneration() const;
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
    const std::vector<FactRelationship>& outgoingRelationships(std::uint64_t factId) const;
    const std::vector<FactRelationship>& incomingRelationships(std::uint64_t factId) const;

    size_t addFact(std::string type,
                   std::string parentType,
                   std::shared_ptr<MapExpr> value,
                   std::filesystem::path origin = {},
                   std::optional<std::uint64_t> logicalId = std::nullopt,
                   std::uint64_t rowVersion = 1,
                   std::vector<std::uint64_t> parentFactIds = {},
                   std::vector<SymbolId> designations = {});
    std::vector<size_t> designationIndexes(const std::vector<SymbolId>& designations,
                                           std::uint64_t snapshotGeneration = 0) const;
    bool hasDesignation(SymbolId designation,
                        std::uint64_t snapshotGeneration = 0) const;
    TemporalState temporalState(size_t index,
                                std::int64_t reasoningTime = 0,
                                std::uint64_t snapshotGeneration = 0) const;
    std::vector<size_t> currentFactIndexes(const std::vector<size_t>& candidates,
                                            std::uint64_t snapshotGeneration = 0) const;
    std::vector<size_t> relevantPastFactIndexes(const std::string& type,
                                                SymbolId typeId = 0,
                                                std::uint64_t snapshotGeneration = 0) const;
    void setParent(const std::string& child, const std::string& parent);
    void setParent(const std::string& child,
                   const std::string& parent,
                   std::filesystem::path origin);
    const std::unordered_map<std::string, std::string>& parents() const;
    std::vector<std::string> parentsOf(const std::string& child) const;
    const std::vector<SymbolId>& parentsOf(SymbolId childId) const;
    std::vector<std::pair<std::string, std::string>> hierarchyEdges() const;
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

    // Copying a store catalog must not copy its fact rows.  The catalog keeps
    // a small directory of fixed-size immutable batches; a writer detaches
    // only the batch it appends to or changes.  Snapshot catalogs retain the
    // previous batch pointers, so row positions remain valid for their full
    // lifetime.
    struct FactRows {
        static constexpr std::size_t BatchSize = 1024;

        std::vector<std::shared_ptr<std::vector<FactRecord>>> batches;
        std::size_t count = 0;

        std::size_t size() const { return count; }
        const FactRecord& at(std::size_t index) const;
        FactRecord& mutableAt(std::size_t index);
        FactRecord& append(FactRecord record);
    };

    // Fact ids are monotonic, so a paged directory is both cheaper than a
    // hash table and naturally copy-on-write at page granularity.  It maps a
    // stable FactId to the row position visible in this catalog generation.
    struct FactIdDirectory {
        static constexpr std::size_t PageSize = 1024;
        static constexpr std::size_t Missing = static_cast<std::size_t>(-1);

        std::vector<std::shared_ptr<std::vector<std::size_t>>> pages;

        std::optional<std::size_t> find(std::uint64_t id) const;
        void assign(std::uint64_t id, std::size_t row);
        void clear() { pages.clear(); }
    };

    struct AttachmentData {
        std::unordered_map<std::uint64_t, std::vector<FactDependency>> dependenciesBySource;
        std::unordered_map<std::uint64_t, std::vector<FactRelationship>> relationshipsBySource;
        std::unordered_map<std::uint64_t, std::vector<FactRelationship>> relationshipsByTarget;
    };

    struct Data {
        struct ValueArena {
            std::unordered_map<std::size_t,
                std::vector<std::shared_ptr<const Expr>>> valuesByHash;
            std::size_t valueCount = 0;
        };

        struct Relation {
            struct RowList {
                static constexpr std::size_t Missing = static_cast<std::size_t>(-1);
                std::size_t first = Missing;
                std::vector<std::size_t> additional;

                void append(std::size_t row) {
                    if (first == Missing) first = row;
                    else additional.push_back(row);
                }
                template <typename Callback>
                void forEach(const Callback& callback) const {
                    if (first != Missing) callback(first);
                    for (std::size_t row : additional) callback(row);
                }
            };

            enum class ColumnKind : std::uint8_t {
                Empty,
                String,
                Number,
                Bool,
                Nil,
                Structured,
                Mixed
            };

            struct FieldColumn {
                struct EqualityIndex {
                    std::mutex mutex;
                    bool built = false;
                    std::size_t queryCount = 0;
                    std::unordered_map<std::size_t, RowList> rowsByHash;
                };

                ColumnKind kind = ColumnKind::Empty;
                // Rows are append ordered. Repeated field values (including
                // array projections) repeat the row index and occupy adjacent
                // positions, so lookup is a cache-friendly equal range.
                std::vector<std::size_t> rows;
                std::vector<std::shared_ptr<const Expr>> values;
                std::vector<std::uint64_t> presence;
                // This derived cache is built only after a query demonstrates
                // demand. It is separate from immutable column data and is
                // detached before a changed relation updates a warm index.
                std::shared_ptr<EqualityIndex> equalityIndex =
                    std::make_shared<EqualityIndex>();

                template <typename Callback>
                void forRow(std::size_t row, const Callback& callback) const {
                    const auto begin = std::lower_bound(rows.begin(), rows.end(), row);
                    for (auto current = begin; current != rows.end() && *current == row; ++current) {
                        const auto offset = static_cast<std::size_t>(current - rows.begin());
                        callback(values[offset]);
                    }
                }

                bool present(std::size_t relationOrdinal) const {
                    const std::size_t word = relationOrdinal / 64;
                    const std::size_t bit = relationOrdinal % 64;
                    return word < presence.size() &&
                           (presence[word] & (std::uint64_t{1} << bit)) != 0;
                }
            };

            SymbolId typeId = 0;
            std::string type;
            std::uint64_t generation = 0;
            std::vector<size_t> rows;
            std::unordered_map<SymbolId, std::string> fieldNames;
            std::unordered_map<SymbolId, FieldColumn> columns;
            // Source field order is kept in one relation-local packed vector.
            // Each row stores only an offset/count pair.
            std::vector<std::size_t> fieldOrderOffsets;
            std::vector<std::size_t> fieldOrderCounts;
            std::vector<SymbolId> fieldOrder;
            std::vector<std::size_t> fieldValueCounts;
            std::vector<std::uint8_t> fieldArrayFlags;
        };

        FactRows facts;
        std::uint64_t nextFactId = 1;
        std::uint64_t nextTemporalSequence = 1;
        std::uint64_t generation = 1;
        std::uint64_t hierarchyGeneration = 1;
        FactIdDirectory factIndexById;
        std::unordered_map<std::filesystem::path, std::shared_ptr<std::vector<size_t>>> factsByOrigin;
        std::unordered_map<std::string, std::string> parentOf;
        std::unordered_map<std::string, std::filesystem::path> parentOrigin;
        std::unordered_map<std::string, std::vector<std::string>> additionalParentsOf;
        std::unordered_map<std::string, std::filesystem::path> additionalParentOrigins;
        std::unordered_map<SymbolId, std::vector<SymbolId>> childrenByParent;
        std::unordered_map<SymbolId, std::vector<SymbolId>> parentsByChild;
        std::unordered_map<SymbolId, std::vector<std::string>> typeNamesById;
        std::unordered_map<SymbolId, std::shared_ptr<std::vector<size_t>>> designationIndexes;
        std::unordered_map<std::string, std::shared_ptr<std::vector<size_t>>> temporalLineageIndexes;
        // Relation roots are independently copy-on-write.  Publishing a
        // change to one fact type keeps every other relation's columns and
        // indexes shared with existing snapshots.
        std::unordered_map<SymbolId, std::shared_ptr<Relation>> relations;
        std::shared_ptr<AttachmentData> attachments = std::make_shared<AttachmentData>();
        std::shared_ptr<ValueArena> valueArena = std::make_shared<ValueArena>();
    };

    std::shared_ptr<Data> data_;
    std::unordered_map<std::uint64_t, std::shared_ptr<const Data>> snapshots_;
    std::unordered_map<SymbolId, std::unordered_map<std::string, std::vector<size_t>>> compatibleFactCache_;
    std::unordered_map<PropertyQueryKey, std::vector<size_t>, PropertyQueryKeyHash> propertyQueryCache_;
    mutable std::size_t adaptiveEqualityIndexes_ = 0;
    mutable std::size_t adaptiveIndexBuildMicros_ = 0;

    static bool literalIndexKey(const std::shared_ptr<Expr>& value, std::string& out);
    static bool literalIndexHash(const std::shared_ptr<Expr>& value, std::size_t& out);
    static std::size_t stableExprHash(const std::shared_ptr<Expr>& value);
    static bool structurallyEqual(const std::shared_ptr<Expr>& left,
                                  const std::shared_ptr<Expr>& right);
    static bool factSatisfiesPattern(const MapExpr& fact, const MapExpr& pattern);
    static FactTemporalMetadata temporalMetadataFor(const std::string& type,
                                                     std::uint64_t logicalId,
                                                     const std::shared_ptr<MapExpr>& value,
                                                     std::int64_t registrationTime,
                                                     std::uint64_t registrationSequence);
    static bool parseTemporalValue(const std::shared_ptr<Expr>& value, std::int64_t& out);
    static TemporalOrigin temporalOriginFor(const std::shared_ptr<MapExpr>& value);
    TemporalState temporalStateInData(const Data& store,
                                      size_t index,
                                      std::int64_t reasoningTime) const;
    static bool isCompatibleTypeInData(const Data& data,
                                       const std::string& actual,
                                       const std::string& expected);
    const Data& dataForSnapshot(std::uint64_t snapshotGeneration) const;
    void ensureUnique();
    void ensureAttachmentsUnique();
    std::shared_ptr<const Expr> internValue(const std::shared_ptr<Expr>& value);
    std::shared_ptr<MapExpr> materializeFact(const Data& store, std::size_t index) const;
    Data::Relation& writableRelation(SymbolId typeId);
    void indexFact(size_t index, const std::shared_ptr<MapExpr>& value);
    void deactivateFact(size_t index);
    void compactInactiveIfSafe();
    void invalidateCachesForFact(const FactRecord& fact,
                                 const std::shared_ptr<MapExpr>& value = {});
    void invalidateCachesForType(const std::string& type, SymbolId typeId);
    void rememberTypeName(SymbolId id, const std::string& name);
    void rebuildIndexes(const std::vector<std::shared_ptr<MapExpr>>& values);
};

} // namespace Felidae
