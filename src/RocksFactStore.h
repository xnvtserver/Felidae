#pragma once

#include "AST.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rocksdb { class DB; class WriteBatch; }

namespace Felidae {

struct RocksFactStoreSharedState;

struct StoredFact {
    std::uint64_t id = 0;
    std::uint64_t version = 1;
    std::string type;
    std::vector<std::shared_ptr<Expr>> key;
    std::shared_ptr<MapExpr> value;
    std::string parentType;
    std::string origin;
    std::vector<std::uint64_t> parentFactIds;
    std::vector<std::string> designations;
    std::shared_ptr<MapExpr> temporalMetadata;
};

struct StoredLink {
    std::uint64_t id = 0;
    std::uint64_t source = 0;
    std::uint64_t target = 0;
    std::shared_ptr<MapExpr> properties;
};

// Class-level graph edge. Unlike StoredLink, its endpoints are type buckets,
// not individual persistent fact identities.
struct StoredClassEdge {
    std::uint64_t id = 0;
    std::string sourceType;
    std::string targetType;
    std::string direction;
    std::shared_ptr<MapExpr> properties;
};

struct StoredFactIndex {
    std::vector<std::string> fields;
    std::vector<std::shared_ptr<Expr>> values;
};

struct StoredSourceLocator {
    std::string className;
    std::string functionName;
    std::string file;
    std::uint64_t line = 0;
    std::string sourceFingerprint;
    std::string schemaFingerprint;
};

struct RocksFactStoreStats {
    std::uint64_t pointReads = 0;
    std::uint64_t typeScans = 0;
    std::uint64_t fullScans = 0;
    std::uint64_t indexScans = 0;
    std::uint64_t linkScans = 0;
    std::uint64_t factRowsScanned = 0;
    std::uint64_t indexRowsScanned = 0;
    std::uint64_t linksVisited = 0;
    std::uint64_t factWrites = 0;
    std::uint64_t linkWrites = 0;
};

using RocksFactStoreValues = std::map<std::string, std::uint64_t>;

// Durable fact/graph storage. User types are logical buckets encoded into a
// fixed set of ordered key prefixes; they never become RocksDB column
// families. All operations that change a node and its indexes/edges are
// committed through one synchronous WriteBatch.
class RocksFactStore {
public:
    using FactVisitor = std::function<bool(const StoredFact&)>;
    using LinkVisitor = std::function<bool(const StoredLink&)>;
    using ClassEdgeVisitor = std::function<bool(const StoredClassEdge&)>;
    using TypeParentVisitor = std::function<bool(const std::string&, const std::string&)>;
    using SchemaVisitor = std::function<bool(const std::string&, const std::string&)>;

    explicit RocksFactStore(const std::filesystem::path& directory);
    ~RocksFactStore();
    RocksFactStore(const RocksFactStore&) = delete;
    RocksFactStore& operator=(const RocksFactStore&) = delete;

    const std::filesystem::path& directory() const noexcept { return directory_; }
    RocksFactStoreStats stats() const noexcept { return stats_; }
    RocksFactStoreValues databaseStatistics() const;
    RocksFactStoreValues configuration() const;
    RocksFactStoreValues configure(const RocksFactStoreValues& changes);
    bool inTransaction() const noexcept { return static_cast<bool>(transaction_); }
    void beginTransaction();
    void commitTransaction();
    void rollbackTransaction() noexcept;

    std::optional<StoredFact> findFact(const std::string& type,
                                       const std::vector<std::shared_ptr<Expr>>& key) const;
    std::optional<StoredFact> findFactById(std::uint64_t id) const;
    StoredFact insertFact(const std::string& type,
                          const std::vector<std::shared_ptr<Expr>>& key,
                          const std::shared_ptr<MapExpr>& value,
                          bool idempotent);
    StoredFact insertFact(StoredFact fact, bool idempotent);
    StoredFact updateFact(const StoredFact& replacement);
    StoredFact replaceFact(const std::string& oldType,
                           const std::vector<std::shared_ptr<Expr>>& oldKey,
                           StoredFact replacement);
    void deleteFact(const std::string& type,
                    const std::vector<std::shared_ptr<Expr>>& key);
    void scanFacts(const std::string& type, std::size_t limit,
                   const FactVisitor& visitor) const;
    void scanAllFacts(std::size_t limit, const FactVisitor& visitor) const;
    // Index maintenance is part of the owning record transaction. Calling
    // either method without beginTransaction() is a contract violation.
    void addFactIndexes(const std::string& type, std::uint64_t factId,
                        const std::vector<StoredFactIndex>& indexes);
    void removeFactIndexes(const std::string& type, std::uint64_t factId,
                           const std::vector<StoredFactIndex>& indexes);
    void scanFactIndex(const std::string& type,
                       const StoredFactIndex& index,
                       std::size_t limit,
                       const FactVisitor& visitor) const;
    bool hasDesignation(const std::string& designation) const;
    void scanDesignation(const std::string& designation,
                         std::size_t limit,
                         const FactVisitor& visitor) const;

    void registerSchema(const std::string& type, const std::string& fingerprint);
    void promoteSchemalessSchema(const std::string& type,
                                 const std::string& expectedSchemalessFingerprint,
                                 const std::string& classFingerprint);
    std::optional<std::string> schemaFingerprint(const std::string& type) const;
    void scanSchemas(const SchemaVisitor& visitor) const;
    void registerSourceLocator(const StoredSourceLocator& locator);
    std::optional<StoredSourceLocator> sourceLocator(
        const std::string& className, const std::string& functionName) const;
    void registerTypeParent(const std::string& child, const std::string& parent);
    void scanTypeParents(const TypeParentVisitor& visitor) const;

    StoredLink insertLink(const StoredLink& link);
    StoredLink insertLink(const StoredLink& link, bool idempotent);
    void scanLinks(std::uint64_t nodeId, bool outgoing, std::size_t limit,
                   const LinkVisitor& visitor) const;
    bool hasIncomingLinks(std::uint64_t nodeId) const;
    void deleteOutgoingLinks(std::uint64_t nodeId);
    StoredClassEdge insertClassEdge(const StoredClassEdge& edge,
                                    bool idempotent = true);
    void scanClassEdges(const std::string& type, bool outgoing,
                        std::size_t limit,
                        const ClassEdgeVisitor& visitor) const;
    void scanAllClassEdges(std::size_t limit,
                           const ClassEdgeVisitor& visitor) const;

    static std::string encodeFactKey(
        const std::string& type,
        const std::vector<std::shared_ptr<Expr>>& key);

private:
    std::filesystem::path directory_;
    // All sessions in the database-owner process share the one RocksDB
    // handle required by RocksDB's directory lock. Transaction batches and
    // staged reads remain per session below.
    std::shared_ptr<RocksFactStoreSharedState> sharedState_;
    rocksdb::DB* database_ = nullptr;
    std::unique_ptr<rocksdb::WriteBatch> transaction_;
    std::unique_lock<std::mutex> transactionLock_;
    std::unordered_map<std::string, std::optional<std::string>> stagedValues_;
    std::unordered_map<std::string, std::uint64_t> stagedSequences_;
    mutable RocksFactStoreStats stats_;

    std::uint64_t allocateId(const std::string& sequenceKey);
    std::unique_lock<std::mutex> standaloneWriteLock();
    std::optional<std::string> readValue(const std::string& key) const;
    void putValue(const std::string& key, const std::string& value);
    void deleteValue(const std::string& key);
    void commitBatch(rocksdb::WriteBatch& batch, const char* operation);
};

} // namespace Felidae
