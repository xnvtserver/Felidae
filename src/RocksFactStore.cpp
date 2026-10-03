#include "RocksFactStore.h"

#include <rocksdb/db.h>
#include <rocksdb/iterator.h>
#include <rocksdb/options.h>
#include <rocksdb/write_batch.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <unordered_set>

namespace Felidae {

struct RocksFactStoreSharedState {
    std::unique_ptr<rocksdb::DB> database;
    std::mutex writeMutex;
};

namespace {

constexpr char kNodePrefix = 'N';
constexpr char kSchemaPrefix = 'S';
constexpr char kSourceLocatorPrefix = 'C';
constexpr char kTypeParentPrefix = 'H';
constexpr char kNodeIdPrefix = 'D';
constexpr char kIndexPrefix = 'X';
constexpr char kDesignationPrefix = 'Q';
constexpr char kLinkPrefix = 'R';
constexpr char kOutgoingPrefix = 'O';
constexpr char kIncomingPrefix = 'I';
constexpr char kClassEdgePrefix = 'G';
constexpr char kClassOutgoingPrefix = 'U';
constexpr char kClassIncomingPrefix = 'V';
constexpr char kProvenancePrefix = 'P';
constexpr char kTemporalPrefix = 'T';
constexpr char kMetadataPrefix = 'M';
// V8 adds a per-record designation index so named fact selections survive a
// restart without rebuilding an in-memory fact catalog. Earlier stores must
// be explicitly reimported.
constexpr std::uint32_t kStoreFormatVersion = 8;
constexpr std::string_view kConfigurationPrefix = "Mconfig/";

std::mutex databaseRegistryMutex;
std::unordered_map<std::string, std::weak_ptr<RocksFactStoreSharedState>>
    databaseRegistry;

void requireStatus(const rocksdb::Status& status, const char* operation) {
    if (!status.ok()) throw std::runtime_error(std::string(operation) + ": " + status.ToString());
}

void appendU64(std::string& out, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) out.push_back(static_cast<char>((value >> shift) & 0xffU));
}

std::uint64_t readU64(const std::string& source, std::size_t& offset) {
    if (source.size() - offset < 8) throw std::runtime_error("Corrupt Felidae RocksDB integer");
    std::uint64_t value = 0;
    for (int index = 0; index < 8; ++index) {
        value = (value << 8U) | static_cast<unsigned char>(source[offset++]);
    }
    return value;
}

std::shared_ptr<RocksFactStoreSharedState> openSharedDatabase(
    const std::filesystem::path& directory) {
    const std::string identity = directory.generic_string();
    std::lock_guard registryLock(databaseRegistryMutex);
    if (const auto found = databaseRegistry.find(identity);
        found != databaseRegistry.end()) {
        if (auto existing = found->second.lock()) return existing;
        databaseRegistry.erase(found);
    }

    rocksdb::Options options;
    options.create_if_missing = true;
    options.paranoid_checks = true;
    auto state = std::make_shared<RocksFactStoreSharedState>();
    const auto openStatus = rocksdb::DB::Open(
        options, directory.string(), &state->database);
    // RocksDB's LOCK file is the only cross-process ownership guard.
    if (openStatus.IsIOError() &&
        openStatus.ToString().find("lock") != std::string::npos) {
        throw std::runtime_error(
            "RocksDB directory '" + directory.string() +
            "' is in use by another Felidae process (" +
            openStatus.ToString() + ")");
    }
    requireStatus(openStatus, "Open RocksDB");

    std::string version;
    const std::string formatKey = std::string(1, kMetadataPrefix) + "format";
    const auto status = state->database->Get(
        rocksdb::ReadOptions{}, formatKey, &version);
    if (status.IsNotFound()) {
        std::string encoded;
        appendU64(encoded, kStoreFormatVersion);
        rocksdb::WriteOptions write;
        write.sync = true;
        requireStatus(state->database->Put(write, formatKey, encoded),
                      "Initialize Felidae store");
    } else {
        requireStatus(status, "Read Felidae store version");
        std::size_t offset = 0;
        if (readU64(version, offset) != kStoreFormatVersion ||
            offset != version.size()) {
            throw std::runtime_error(
                "Unsupported Felidae RocksDB format version");
        }
    }

    std::unordered_map<std::string, std::string> persistedOptions;
    for (const std::string name : {
             "bytes_per_sync", "delayed_write_rate", "max_background_jobs",
             "max_total_wal_size", "wal_bytes_per_sync"}) {
        std::string encoded;
        const auto optionStatus = state->database->Get(
            rocksdb::ReadOptions{}, std::string(kConfigurationPrefix) + name,
            &encoded);
        if (optionStatus.IsNotFound()) continue;
        requireStatus(optionStatus, "Read RocksDB configuration");
        std::size_t offset = 0;
        const auto value = readU64(encoded, offset);
        if (offset != encoded.size()) {
            throw std::runtime_error(
                "Corrupt Felidae RocksDB configuration");
        }
        persistedOptions.emplace(name, std::to_string(value));
    }
    if (!persistedOptions.empty()) {
        requireStatus(state->database->SetDBOptions(persistedOptions),
                      "Apply RocksDB configuration");
    }
    databaseRegistry.emplace(identity, state);
    return state;
}

void appendOrderedString(std::string& out, const std::string& value) {
    for (const unsigned char byte : value) {
        if (byte == 0) { out.push_back(0); out.push_back(static_cast<char>(0xff)); }
        else out.push_back(static_cast<char>(byte));
    }
    out.push_back(0);
    out.push_back(0);
}

void appendBlob(std::string& out, const std::string& value) {
    appendU64(out, value.size());
    out.append(value);
}

std::string readBlob(const std::string& source, std::size_t& offset) {
    const auto size = readU64(source, offset);
    if (size > source.size() - offset) throw std::runtime_error("Corrupt Felidae RocksDB blob");
    std::string value = source.substr(offset, static_cast<std::size_t>(size));
    offset += static_cast<std::size_t>(size);
    return value;
}

std::string readOrderedString(const std::string& source, std::size_t& offset) {
    std::string output;
    while (offset < source.size()) {
        const unsigned char byte = static_cast<unsigned char>(source[offset++]);
        if (byte != 0) {
            output.push_back(static_cast<char>(byte));
            continue;
        }
        if (offset >= source.size()) {
            throw std::runtime_error("Corrupt Felidae ordered string");
        }
        const unsigned char escaped = static_cast<unsigned char>(source[offset++]);
        if (escaped == 0) return output;
        if (escaped != 0xff) {
            throw std::runtime_error("Corrupt Felidae ordered string escape");
        }
        output.push_back(0);
    }
    throw std::runtime_error("Truncated Felidae ordered string");
}

void encodeExpr(std::string& out, const std::shared_ptr<Expr>& value) {
    if (!value || value->kind() == ExprKind::Nil) { out.push_back('0'); return; }
    switch (value->kind()) {
        case ExprKind::Bool:
            out.push_back('b');
            out.push_back(static_cast<const BoolExpr&>(*value).value ? 1 : 0);
            return;
        case ExprKind::Number: {
            out.push_back('n');
            const double number = static_cast<const NumberExpr&>(*value).value;
            std::uint64_t bits = 0;
            static_assert(sizeof(bits) == sizeof(number));
            std::memcpy(&bits, &number, sizeof(bits));
            appendU64(out, bits);
            return;
        }
        case ExprKind::String:
            out.push_back('s');
            appendBlob(out, static_cast<const StringExpr&>(*value).value);
            return;
        case ExprKind::Array: {
            out.push_back('a');
            const auto& array = static_cast<const ArrayExpr&>(*value);
            appendU64(out, array.items.size());
            for (const auto& item : array.items) encodeExpr(out, item);
            return;
        }
        case ExprKind::Map: {
            out.push_back('m');
            const auto& map = static_cast<const MapExpr&>(*value);
            appendBlob(out, map.factType);
            appendU64(out, map.entries.size());
            for (const auto& field : map.entries) {
                appendBlob(out, field.key);
                encodeExpr(out, field.value);
            }
            return;
        }
        case ExprKind::Term: {
            const auto& term = static_cast<const TermExpr&>(*value);
            if (term.builtinId != BuiltinId::FnPair || term.args.size() != 2) {
                throw std::runtime_error("Only Pair terms can be persisted");
            }
            out.push_back('p');
            encodeExpr(out, term.args[0].value);
            encodeExpr(out, term.args[1].value);
            return;
        }
        default:
            throw std::runtime_error("Only immutable Felidae data values can be persisted");
    }
}

std::shared_ptr<Expr> decodeExpr(const std::string& source, std::size_t& offset) {
    if (offset >= source.size()) throw std::runtime_error("Corrupt Felidae RocksDB value");
    switch (source[offset++]) {
        case '0': return std::make_shared<NilExpr>();
        case 'b': {
            if (offset >= source.size()) throw std::runtime_error("Corrupt Felidae RocksDB boolean");
            return std::make_shared<BoolExpr>(source[offset++] != 0);
        }
        case 'n': {
            const std::uint64_t bits = readU64(source, offset);
            double number = 0;
            std::memcpy(&number, &bits, sizeof(number));
            return std::make_shared<NumberExpr>(number);
        }
        case 's': return std::make_shared<StringExpr>(readBlob(source, offset));
        case 'a': {
            const auto count = readU64(source, offset);
            std::vector<std::shared_ptr<Expr>> items;
            items.reserve(static_cast<std::size_t>(count));
            for (std::uint64_t index = 0; index < count; ++index) items.push_back(decodeExpr(source, offset));
            return std::make_shared<ArrayExpr>(std::move(items));
        }
        case 'm': {
            const std::string type = readBlob(source, offset);
            const auto count = readU64(source, offset);
            std::vector<MapEntry> fields;
            fields.reserve(static_cast<std::size_t>(count));
            for (std::uint64_t index = 0; index < count; ++index) {
                std::string key = readBlob(source, offset);
                auto value = decodeExpr(source, offset);
                fields.emplace_back(std::move(key), std::move(value));
            }
            auto map = std::make_shared<MapExpr>(std::move(fields));
            map->factType = type;
            return map;
        }
        case 'p': {
            auto first = decodeExpr(source, offset);
            auto second = decodeExpr(source, offset);
            return std::make_shared<TermExpr>(
                "fn:pair", std::vector<Arg>{{"first", std::move(first)},
                                             {"last", std::move(second)}},
                BuiltinId::FnPair);
        }
        default: throw std::runtime_error("Unknown Felidae RocksDB value tag");
    }
}

bool dataValuesEqual(const std::shared_ptr<Expr>& left,
                     const std::shared_ptr<Expr>& right) {
    const ExprKind leftKind = left ? left->kind() : ExprKind::Nil;
    const ExprKind rightKind = right ? right->kind() : ExprKind::Nil;
    if (leftKind != rightKind) return false;
    switch (leftKind) {
        case ExprKind::Nil:
            return true;
        case ExprKind::Bool:
            return static_cast<const BoolExpr&>(*left).value ==
                   static_cast<const BoolExpr&>(*right).value;
        case ExprKind::Number:
            return static_cast<const NumberExpr&>(*left).value ==
                   static_cast<const NumberExpr&>(*right).value;
        case ExprKind::String:
            return static_cast<const StringExpr&>(*left).value ==
                   static_cast<const StringExpr&>(*right).value;
        case ExprKind::Array: {
            const auto& lhs = static_cast<const ArrayExpr&>(*left).items;
            const auto& rhs = static_cast<const ArrayExpr&>(*right).items;
            if (lhs.size() != rhs.size()) return false;
            for (std::size_t index = 0; index < lhs.size(); ++index) {
                if (!dataValuesEqual(lhs[index], rhs[index])) return false;
            }
            return true;
        }
        case ExprKind::Map: {
            const auto& lhs = static_cast<const MapExpr&>(*left);
            const auto& rhs = static_cast<const MapExpr&>(*right);
            if (lhs.factType != rhs.factType ||
                lhs.entries.size() != rhs.entries.size()) return false;
            for (const auto& field : lhs.entries) {
                const auto found = std::find_if(
                    rhs.entries.begin(), rhs.entries.end(),
                    [&](const MapEntry& candidate) {
                        return candidate.key == field.key;
                    });
                if (found == rhs.entries.end() ||
                    !dataValuesEqual(field.value, found->value)) return false;
            }
            return true;
        }
        case ExprKind::Term: {
            const auto& lhs = static_cast<const TermExpr&>(*left);
            const auto& rhs = static_cast<const TermExpr&>(*right);
            if (lhs.builtinId != BuiltinId::FnPair || rhs.builtinId != BuiltinId::FnPair ||
                lhs.args.size() != 2 || rhs.args.size() != 2) return false;
            return dataValuesEqual(lhs.args[0].value, rhs.args[0].value) &&
                   dataValuesEqual(lhs.args[1].value, rhs.args[1].value);
        }
        default:
            return false;
    }
}

std::string factPrefix(const std::string& type) {
    std::string key(1, kNodePrefix);
    appendOrderedString(key, type);
    return key;
}

void appendOrderedScalar(std::string& out, const std::shared_ptr<Expr>& value) {
    if (const auto text = std::dynamic_pointer_cast<StringExpr>(value)) {
        out.push_back('s');
        appendOrderedString(out, text->value);
        return;
    }
    if (const auto boolean = std::dynamic_pointer_cast<BoolExpr>(value)) {
        out.push_back('b');
        out.push_back(boolean->value ? 1 : 0);
        return;
    }
    if (const auto number = std::dynamic_pointer_cast<NumberExpr>(value)) {
        out.push_back('n');
        std::uint64_t bits = 0;
        const double canonical = number->value == 0.0 ? 0.0 : number->value;
        std::memcpy(&bits, &canonical, sizeof(bits));
        // IEEE-754 becomes lexicographically ordered by flipping the sign bit
        // for non-negative values and complementing every bit for negatives.
        bits = (bits & (std::uint64_t{1} << 63U)) != 0
            ? ~bits
            : bits ^ (std::uint64_t{1} << 63U);
        appendU64(out, bits);
        return;
    }
    throw std::invalid_argument("Fact key components must be scalar values");
}

std::string schemaKey(const std::string& type) {
    std::string key(1, kSchemaPrefix);
    appendOrderedString(key, type);
    return key;
}

std::string sourceLocatorKey(const std::string& className,
                             const std::string& functionName) {
    std::string key(1, kSourceLocatorPrefix);
    appendOrderedString(key, className);
    appendOrderedString(key, functionName);
    return key;
}

std::string typeParentKey(const std::string& child, const std::string& parent) {
    std::string key(1, kTypeParentPrefix);
    appendOrderedString(key, child);
    appendOrderedString(key, parent);
    return key;
}

std::string linkKey(std::uint64_t id) {
    std::string key(1, kLinkPrefix);
    appendU64(key, id);
    return key;
}

std::string classEdgeKey(std::uint64_t id) {
    std::string key(1, kClassEdgePrefix);
    appendU64(key, id);
    return key;
}

std::string classAdjacencyKey(char prefix, const std::string& type,
                              std::uint64_t edgeId) {
    std::string key(1, prefix);
    appendOrderedString(key, type);
    appendU64(key, edgeId);
    return key;
}

std::string classAdjacencyPrefix(char prefix, const std::string& type) {
    std::string key(1, prefix);
    appendOrderedString(key, type);
    return key;
}

std::string nodeIdKey(std::uint64_t id) {
    std::string key(1, kNodeIdPrefix);
    appendU64(key, id);
    return key;
}

std::string recordMetadataKey(char prefix, std::uint64_t id) {
    std::string key(1, prefix);
    appendU64(key, id);
    return key;
}

std::string encodedTemporalMetadata(const StoredFact& fact) {
    std::string value;
    encodeExpr(value, fact.temporalMetadata);
    return value;
}

std::string factIndexPrefix(const std::string& type,
                            const StoredFactIndex& index) {
    if (index.fields.empty() || index.fields.size() != index.values.size()) {
        throw std::invalid_argument("Fact index requires equally sized fields and values");
    }
    std::string key(1, kIndexPrefix);
    appendOrderedString(key, type);
    for (const auto& field : index.fields) appendOrderedString(key, field);
    key.push_back(0);
    for (const auto& value : index.values) {
        std::string encoded;
        appendOrderedScalar(encoded, value);
        appendOrderedString(key, encoded);
    }
    return key;
}

std::string factIndexKey(const std::string& type,
                         const StoredFactIndex& index,
                         std::uint64_t factId) {
    std::string key = factIndexPrefix(type, index);
    appendU64(key, factId);
    return key;
}

std::string designationPrefix(const std::string& designation) {
    if (designation.empty()) {
        throw std::invalid_argument("Fact designation cannot be empty");
    }
    std::string key(1, kDesignationPrefix);
    appendOrderedString(key, designation);
    return key;
}

std::string designationKey(const std::string& designation,
                           std::uint64_t factId) {
    std::string key = designationPrefix(designation);
    appendU64(key, factId);
    return key;
}

std::string adjacencyKey(char prefix, std::uint64_t node, const std::string& type,
                         std::uint64_t linkId) {
    std::string key(1, prefix);
    appendU64(key, node);
    appendOrderedString(key, type);
    appendU64(key, linkId);
    return key;
}

std::string adjacencyPrefix(char prefix, std::uint64_t node,
                            const std::optional<std::string>& type) {
    std::string key(1, prefix);
    appendU64(key, node);
    if (type) appendOrderedString(key, *type);
    return key;
}

std::string encodeFactValue(const StoredFact& fact) {
    std::string value;
    appendU64(value, fact.id);
    appendU64(value, fact.version);
    appendBlob(value, fact.type);
    appendU64(value, fact.key.size());
    for (const auto& component : fact.key) encodeExpr(value, component);
    encodeExpr(value, fact.value);
    appendBlob(value, fact.parentType);
    appendBlob(value, fact.origin);
    appendU64(value, fact.parentFactIds.size());
    for (const auto id : fact.parentFactIds) appendU64(value, id);
    appendU64(value, fact.designations.size());
    for (const auto& designation : fact.designations) appendBlob(value, designation);
    encodeExpr(value, fact.temporalMetadata);
    return value;
}

StoredFact decodeFactValue(const std::string& value) {
    std::size_t offset = 0;
    StoredFact fact;
    fact.id = readU64(value, offset);
    fact.version = readU64(value, offset);
    fact.type = readBlob(value, offset);
    const auto count = readU64(value, offset);
    fact.key.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0; index < count; ++index) fact.key.push_back(decodeExpr(value, offset));
    fact.value = std::dynamic_pointer_cast<MapExpr>(decodeExpr(value, offset));
    if (!fact.value) throw std::runtime_error("Corrupt Felidae RocksDB fact");
    fact.parentType = readBlob(value, offset);
    fact.origin = readBlob(value, offset);
    const auto parentCount = readU64(value, offset);
    fact.parentFactIds.reserve(static_cast<std::size_t>(parentCount));
    for (std::uint64_t index = 0; index < parentCount; ++index) {
        fact.parentFactIds.push_back(readU64(value, offset));
    }
    const auto designationCount = readU64(value, offset);
    fact.designations.reserve(static_cast<std::size_t>(designationCount));
    for (std::uint64_t index = 0; index < designationCount; ++index) {
        fact.designations.push_back(readBlob(value, offset));
    }
    fact.temporalMetadata = std::dynamic_pointer_cast<MapExpr>(decodeExpr(value, offset));
    if (offset != value.size()) throw std::runtime_error("Corrupt Felidae RocksDB fact");
    fact.value->factIdentity = fact.id;
    fact.value->factType = fact.type;
    return fact;
}

std::string encodeLinkValue(const StoredLink& link) {
    std::string value;
    appendU64(value, link.id);
    appendBlob(value, "Link");
    appendU64(value, link.source);
    appendU64(value, link.target);
    encodeExpr(value, link.properties ? link.properties
                                               : std::make_shared<MapExpr>(std::vector<MapEntry>{}));
    return value;
}

StoredLink decodeLinkValue(const std::string& value) {
    std::size_t offset = 0;
    StoredLink link;
    link.id = readU64(value, offset);
    if (readBlob(value, offset) != "Link") {
        throw std::runtime_error("Corrupt Felidae RocksDB Link type");
    }
    link.source = readU64(value, offset);
    link.target = readU64(value, offset);
    link.properties = std::dynamic_pointer_cast<MapExpr>(decodeExpr(value, offset));
    if (!link.properties || offset != value.size()) {
        throw std::runtime_error("Corrupt Felidae RocksDB Link");
    }
    return link;
}

std::string encodeClassEdgeValue(const StoredClassEdge& edge) {
    std::string value;
    appendU64(value, edge.id);
    appendBlob(value, edge.sourceType);
    appendBlob(value, edge.targetType);
    appendBlob(value, edge.direction);
    encodeExpr(value, edge.properties ? edge.properties
                                      : std::make_shared<MapExpr>(std::vector<MapEntry>{}));
    return value;
}

StoredClassEdge decodeClassEdgeValue(const std::string& value) {
    std::size_t offset = 0;
    StoredClassEdge edge;
    edge.id = readU64(value, offset);
    edge.sourceType = readBlob(value, offset);
    edge.targetType = readBlob(value, offset);
    edge.direction = readBlob(value, offset);
    edge.properties = std::dynamic_pointer_cast<MapExpr>(decodeExpr(value, offset));
    if (!edge.properties || offset != value.size()) {
        throw std::runtime_error("Corrupt Felidae class graph edge");
    }
    return edge;
}

bool startsWith(const rocksdb::Slice& value, const std::string& prefix) {
    return value.size() >= prefix.size() && std::memcmp(value.data(), prefix.data(), prefix.size()) == 0;
}

} // namespace

RocksFactStore::RocksFactStore(const std::filesystem::path& directory)
    : directory_(std::filesystem::absolute(directory).lexically_normal()) {
    sharedState_ = openSharedDatabase(directory_);
    database_ = sharedState_->database.get();
}

RocksFactStore::~RocksFactStore() = default;

RocksFactStoreValues RocksFactStore::databaseStatistics() const {
    RocksFactStoreValues values{
        {"point_reads", stats_.pointReads},
        {"type_scans", stats_.typeScans},
        {"full_scans", stats_.fullScans},
        {"index_scans", stats_.indexScans},
        {"link_scans", stats_.linkScans},
        {"fact_rows_scanned", stats_.factRowsScanned},
        {"index_rows_scanned", stats_.indexRowsScanned},
        {"links_visited", stats_.linksVisited},
        {"fact_writes", stats_.factWrites},
        {"link_writes", stats_.linkWrites},
    };
    const std::pair<const char*, const char*> properties[] = {
        {"estimated_keys", "rocksdb.estimate-num-keys"},
        {"estimated_live_data_bytes", "rocksdb.estimate-live-data-size"},
        {"memtable_bytes", "rocksdb.size-all-mem-tables"},
        {"block_cache_bytes", "rocksdb.block-cache-usage"},
        {"live_sst_bytes", "rocksdb.live-sst-files-size"},
        {"total_sst_bytes", "rocksdb.total-sst-files-size"},
        {"running_compactions", "rocksdb.num-running-compactions"},
        {"running_flushes", "rocksdb.num-running-flushes"},
        {"actual_delayed_write_rate", "rocksdb.actual-delayed-write-rate"},
        {"write_stopped", "rocksdb.is-write-stopped"},
    };
    for (const auto& [name, property] : properties) {
        std::uint64_t value = 0;
        if (database_->GetIntProperty(property, &value)) values.emplace(name, value);
    }
    return values;
}

RocksFactStoreValues RocksFactStore::configuration() const {
    const auto options = database_->GetDBOptions();
    return {
        {"bytes_per_sync", options.bytes_per_sync},
        {"delayed_write_rate", options.delayed_write_rate},
        {"max_background_jobs", static_cast<std::uint64_t>(
            std::max(0, options.max_background_jobs))},
        {"max_total_wal_size", options.max_total_wal_size},
        {"wal_bytes_per_sync", options.wal_bytes_per_sync},
    };
}

RocksFactStoreValues RocksFactStore::configure(
    const RocksFactStoreValues& changes) {
    [[maybe_unused]] auto writeLock = standaloneWriteLock();
    static const std::unordered_set<std::string> allowed{
        "bytes_per_sync", "delayed_write_rate", "max_background_jobs",
        "max_total_wal_size", "wal_bytes_per_sync"};
    std::unordered_map<std::string, std::string> encoded;
    for (const auto& [name, value] : changes) {
        if (!allowed.count(name)) {
            throw std::invalid_argument("RocksDB option '" + name +
                "' is not runtime configurable through Felidae");
        }
        if (name == "max_background_jobs" && (value == 0 || value > 1024)) {
            throw std::invalid_argument(
                "RocksDB max_background_jobs must be from 1 to 1024");
        }
        encoded.emplace(name, std::to_string(value));
    }
    if (encoded.empty()) return configuration();
    requireStatus(database_->SetDBOptions(encoded), "Configure RocksDB");

    rocksdb::WriteBatch batch;
    for (const auto& [name, value] : changes) {
        std::string stored;
        appendU64(stored, value);
        batch.Put(std::string(kConfigurationPrefix) + name, stored);
    }
    commitBatch(batch, "Persist RocksDB configuration");
    return configuration();
}

void RocksFactStore::beginTransaction() {
    if (transaction_) throw std::runtime_error("RocksDB transaction is already active");
    auto batch = std::make_unique<rocksdb::WriteBatch>();
    transactionLock_ = std::unique_lock<std::mutex>(sharedState_->writeMutex);
    transaction_ = std::move(batch);
    stagedValues_.clear();
    stagedSequences_.clear();
}

void RocksFactStore::commitTransaction() {
    if (!transaction_) throw std::runtime_error("No RocksDB transaction is active");
    rocksdb::WriteOptions options;
    options.sync = true;
    const auto status = database_->Write(options, transaction_.get());
    if (!status.ok()) {
        rollbackTransaction();
        requireStatus(status, "Commit Felidae database transaction");
    }
    transaction_.reset();
    stagedValues_.clear();
    stagedSequences_.clear();
    transactionLock_.unlock();
}

void RocksFactStore::rollbackTransaction() noexcept {
    transaction_.reset();
    stagedValues_.clear();
    stagedSequences_.clear();
    if (transactionLock_.owns_lock()) transactionLock_.unlock();
}

std::unique_lock<std::mutex> RocksFactStore::standaloneWriteLock() {
    if (transaction_) return {};
    return std::unique_lock<std::mutex>(sharedState_->writeMutex);
}

std::optional<std::string> RocksFactStore::readValue(const std::string& key) const {
    const auto staged = stagedValues_.find(key);
    if (staged != stagedValues_.end()) return staged->second;
    std::string value;
    const auto status = database_->Get(rocksdb::ReadOptions{}, key, &value);
    if (status.IsNotFound()) return std::nullopt;
    requireStatus(status, "Read RocksDB value");
    return value;
}

void RocksFactStore::putValue(const std::string& key, const std::string& value) {
    if (transaction_) {
        transaction_->Put(key, value);
        stagedValues_[key] = value;
        return;
    }
    rocksdb::WriteOptions options;
    options.sync = true;
    requireStatus(database_->Put(options, key, value), "Write RocksDB value");
}

void RocksFactStore::deleteValue(const std::string& key) {
    if (transaction_) {
        transaction_->Delete(key);
        stagedValues_[key] = std::nullopt;
        return;
    }
    rocksdb::WriteOptions options;
    options.sync = true;
    requireStatus(database_->Delete(options, key), "Delete RocksDB value");
}

void RocksFactStore::commitBatch(rocksdb::WriteBatch& batch, const char* operation) {
    if (transaction_) {
        throw std::logic_error("Internal RocksDB batch must be expanded inside a Felidae transaction");
    }
    rocksdb::WriteOptions options;
    options.sync = true;
    requireStatus(database_->Write(options, &batch), operation);
}

std::string RocksFactStore::encodeFactKey(
    const std::string& type, const std::vector<std::shared_ptr<Expr>>& keyParts) {
    if (type.empty() || keyParts.empty()) throw std::invalid_argument("Fact key requires type and components");
    std::string key = factPrefix(type);
    for (const auto& component : keyParts) {
        std::string encoded;
        appendOrderedScalar(encoded, component);
        appendOrderedString(key, encoded);
    }
    return key;
}

std::optional<StoredFact> RocksFactStore::findFact(
    const std::string& type, const std::vector<std::shared_ptr<Expr>>& key) const {
    ++stats_.pointReads;
    const auto value = readValue(encodeFactKey(type, key));
    return value ? std::optional<StoredFact>(decodeFactValue(*value)) : std::nullopt;
}

std::optional<StoredFact> RocksFactStore::findFactById(std::uint64_t id) const {
    ++stats_.pointReads;
    const auto encodedNodeKey = readValue(nodeIdKey(id));
    if (!encodedNodeKey) return std::nullopt;
    const auto value = readValue(*encodedNodeKey);
    return value ? std::optional<StoredFact>(decodeFactValue(*value)) : std::nullopt;
}

std::uint64_t RocksFactStore::allocateId(const std::string& sequenceKey) {
    const std::string key = std::string(1, kMetadataPrefix) + sequenceKey;
    std::uint64_t next = 1;
    const auto staged = stagedSequences_.find(key);
    if (staged != stagedSequences_.end()) {
        next = staged->second;
    } else if (const auto current = readValue(key)) {
        std::size_t offset = 0;
        next = readU64(*current, offset);
    }
    if (next == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("Felidae id space exhausted");
    std::string encoded;
    appendU64(encoded, next + 1);
    stagedSequences_[key] = next + 1;
    putValue(key, encoded);
    return next;
}

StoredFact RocksFactStore::insertFact(const std::string& type,
                                      const std::vector<std::shared_ptr<Expr>>& key,
                                      const std::shared_ptr<MapExpr>& value,
                                      bool idempotent) {
    StoredFact fact;
    fact.type = type;
    fact.key = key;
    fact.value = value;
    return insertFact(std::move(fact), idempotent);
}

StoredFact RocksFactStore::insertFact(StoredFact fact, bool idempotent) {
    [[maybe_unused]] auto writeLock = standaloneWriteLock();
    if (!fact.value) throw std::invalid_argument("Fact value cannot be null");
    if (fact.type.empty() || fact.key.empty()) {
        throw std::invalid_argument("Fact requires type and key");
    }
    if (const auto existing = findFact(fact.type, fact.key)) {
        // Records are maps, so source field order is not part of their value.
        // Compare typed data recursively rather than debug text.
        if (idempotent && dataValuesEqual(existing->value, fact.value)) {
            StoredFact enriched = *existing;
            bool changed = false;
            for (const auto& designation : fact.designations) {
                if (std::find(enriched.designations.begin(),
                              enriched.designations.end(), designation) ==
                    enriched.designations.end()) {
                    enriched.designations.push_back(designation);
                    changed = true;
                }
            }
            return changed ? updateFact(enriched) : *existing;
        }
        throw std::runtime_error("Duplicate key for fact type '" + fact.type + "'");
    }
    ++stats_.factWrites;
    fact.id = allocateId("node-sequence");
    fact.version = 1;
    fact.value = std::static_pointer_cast<MapExpr>(fact.value->clone());
    if (fact.temporalMetadata) {
        fact.temporalMetadata = std::static_pointer_cast<MapExpr>(
            fact.temporalMetadata->clone());
    }
    const std::string encodedKey = encodeFactKey(fact.type, fact.key);
    const std::string encodedValue = encodeFactValue(fact);
    std::string encodedId;
    appendU64(encodedId, fact.id);
    if (transaction_) {
        putValue(encodedKey, encodedValue);
        putValue(nodeIdKey(fact.id), encodedKey);
        putValue(recordMetadataKey(kProvenancePrefix, fact.id), fact.origin);
        putValue(recordMetadataKey(kTemporalPrefix, fact.id),
                 encodedTemporalMetadata(fact));
        for (const auto& designation : fact.designations) {
            putValue(designationKey(designation, fact.id), encodedId);
        }
    } else {
        rocksdb::WriteBatch batch;
        batch.Put(encodedKey, encodedValue);
        batch.Put(nodeIdKey(fact.id), encodedKey);
        batch.Put(recordMetadataKey(kProvenancePrefix, fact.id), fact.origin);
        batch.Put(recordMetadataKey(kTemporalPrefix, fact.id),
                  encodedTemporalMetadata(fact));
        for (const auto& designation : fact.designations) {
            batch.Put(designationKey(designation, fact.id), encodedId);
        }
        commitBatch(batch, "Insert fact");
    }
    fact.value->factIdentity = fact.id;
    fact.value->factType = fact.type;
    return fact;
}

StoredFact RocksFactStore::updateFact(const StoredFact& replacement) {
    [[maybe_unused]] auto writeLock = standaloneWriteLock();
    const auto existing = findFact(replacement.type, replacement.key);
    if (!existing || existing->id != replacement.id) throw std::runtime_error("Fact update target does not exist");
    ++stats_.factWrites;
    StoredFact next = replacement;
    next.version = existing->version + 1;
    const std::string encodedKey = encodeFactKey(next.type, next.key);
    const std::string encodedValue = encodeFactValue(next);
    std::string encodedId;
    appendU64(encodedId, next.id);
    if (transaction_) {
        for (const auto& designation : existing->designations) {
            deleteValue(designationKey(designation, next.id));
        }
        putValue(encodedKey, encodedValue);
        putValue(recordMetadataKey(kProvenancePrefix, next.id), next.origin);
        putValue(recordMetadataKey(kTemporalPrefix, next.id),
                 encodedTemporalMetadata(next));
        for (const auto& designation : next.designations) {
            putValue(designationKey(designation, next.id), encodedId);
        }
    } else {
        rocksdb::WriteBatch batch;
        for (const auto& designation : existing->designations) {
            batch.Delete(designationKey(designation, next.id));
        }
        batch.Put(encodedKey, encodedValue);
        batch.Put(recordMetadataKey(kProvenancePrefix, next.id), next.origin);
        batch.Put(recordMetadataKey(kTemporalPrefix, next.id),
                  encodedTemporalMetadata(next));
        for (const auto& designation : next.designations) {
            batch.Put(designationKey(designation, next.id), encodedId);
        }
        commitBatch(batch, "Update fact");
    }
    return next;
}

StoredFact RocksFactStore::replaceFact(
    const std::string& oldType,
    const std::vector<std::shared_ptr<Expr>>& oldKey,
    StoredFact replacement) {
    [[maybe_unused]] auto writeLock = standaloneWriteLock();
    const auto existing = findFact(oldType, oldKey);
    if (!existing || existing->id != replacement.id) {
        throw std::runtime_error("Fact replacement target does not exist");
    }
    ++stats_.factWrites;
    const std::string oldEncodedKey = encodeFactKey(oldType, oldKey);
    const std::string newEncodedKey = encodeFactKey(replacement.type, replacement.key);
    if (oldEncodedKey != newEncodedKey) {
        if (hasIncomingLinks(existing->id)) {
            throw std::runtime_error(
                "Cannot change the primary key of a referenced fact '" +
                oldType + "'");
        }
        if (readValue(newEncodedKey)) throw std::runtime_error("Fact replacement creates a duplicate key");
    }
    replacement.version = existing->version + 1;
    std::string encodedId;
    appendU64(encodedId, replacement.id);
    if (transaction_) {
        if (oldEncodedKey != newEncodedKey) deleteValue(oldEncodedKey);
        for (const auto& designation : existing->designations) {
            deleteValue(designationKey(designation, replacement.id));
        }
        putValue(newEncodedKey, encodeFactValue(replacement));
        putValue(nodeIdKey(replacement.id), newEncodedKey);
        putValue(recordMetadataKey(kProvenancePrefix, replacement.id),
                 replacement.origin);
        putValue(recordMetadataKey(kTemporalPrefix, replacement.id),
                 encodedTemporalMetadata(replacement));
        for (const auto& designation : replacement.designations) {
            putValue(designationKey(designation, replacement.id), encodedId);
        }
    } else {
        rocksdb::WriteBatch batch;
        if (oldEncodedKey != newEncodedKey) batch.Delete(oldEncodedKey);
        for (const auto& designation : existing->designations) {
            batch.Delete(designationKey(designation, replacement.id));
        }
        batch.Put(newEncodedKey, encodeFactValue(replacement));
        batch.Put(nodeIdKey(replacement.id), newEncodedKey);
        batch.Put(recordMetadataKey(kProvenancePrefix, replacement.id),
                  replacement.origin);
        batch.Put(recordMetadataKey(kTemporalPrefix, replacement.id),
                  encodedTemporalMetadata(replacement));
        for (const auto& designation : replacement.designations) {
            batch.Put(designationKey(designation, replacement.id), encodedId);
        }
        commitBatch(batch, "Replace fact");
    }
    return replacement;
}

void RocksFactStore::deleteFact(const std::string& type,
                                const std::vector<std::shared_ptr<Expr>>& key) {
    [[maybe_unused]] auto writeLock = standaloneWriteLock();
    const auto fact = findFact(type, key);
    if (!fact) return;
    ++stats_.factWrites;
    if (hasIncomingLinks(fact->id)) {
        throw std::runtime_error("Cannot delete referenced fact '" + type + "'");
    }
    std::vector<StoredLink> outgoing;
    scanLinks(fact->id, true, 0, [&](const StoredLink& edge) {
        outgoing.push_back(edge);
        return true;
    });
    stats_.linkWrites += outgoing.size();
    if (transaction_) {
        deleteValue(encodeFactKey(type, key));
        deleteValue(nodeIdKey(fact->id));
        deleteValue(recordMetadataKey(kProvenancePrefix, fact->id));
        deleteValue(recordMetadataKey(kTemporalPrefix, fact->id));
        for (const auto& designation : fact->designations) {
            deleteValue(designationKey(designation, fact->id));
        }
        for (const auto& edge : outgoing) {
            deleteValue(linkKey(edge.id));
            deleteValue(adjacencyKey(kOutgoingPrefix, edge.source, "Link", edge.id));
            deleteValue(adjacencyKey(kIncomingPrefix, edge.target, "Link", edge.id));
        }
    } else {
        rocksdb::WriteBatch batch;
        batch.Delete(encodeFactKey(type, key));
        batch.Delete(nodeIdKey(fact->id));
        batch.Delete(recordMetadataKey(kProvenancePrefix, fact->id));
        batch.Delete(recordMetadataKey(kTemporalPrefix, fact->id));
        for (const auto& designation : fact->designations) {
            batch.Delete(designationKey(designation, fact->id));
        }
        for (const auto& edge : outgoing) {
            batch.Delete(linkKey(edge.id));
            batch.Delete(adjacencyKey(kOutgoingPrefix, edge.source, "Link", edge.id));
            batch.Delete(adjacencyKey(kIncomingPrefix, edge.target, "Link", edge.id));
        }
        commitBatch(batch, "Delete fact");
    }
}

void RocksFactStore::scanFacts(const std::string& type, std::size_t limit,
                               const FactVisitor& visitor) const {
    ++stats_.typeScans;
    const std::string prefix = factPrefix(type);
    // Snapshot matching transaction entries before invoking visitors. A
    // visitor may stage index writes, which must not invalidate iteration of
    // stagedValues_. Reads inside a module transaction must see prior fact
    // inserts, replacements, and deletes from that same module.
    std::unordered_map<std::string, std::optional<std::string>> staged;
    for (const auto& [key, value] : stagedValues_) {
        if (key.size() >= prefix.size() &&
            std::memcmp(key.data(), prefix.data(), prefix.size()) == 0) {
            staged.emplace(key, value);
        }
    }
    std::unique_ptr<rocksdb::Iterator> iterator(database_->NewIterator(rocksdb::ReadOptions{}));
    std::size_t visited = 0;
    std::unordered_set<std::string> persistedKeys;
    for (iterator->Seek(prefix); iterator->Valid() && startsWith(iterator->key(), prefix); iterator->Next()) {
        if (limit != 0 && visited >= limit) break;
        const std::string key = iterator->key().ToString();
        persistedKeys.insert(key);
        const auto pending = staged.find(key);
        if (pending != staged.end() && !pending->second) continue;
        ++visited;
        ++stats_.factRowsScanned;
        const std::string encoded = pending != staged.end()
            ? *pending->second : iterator->value().ToString();
        if (!visitor(decodeFactValue(encoded))) return;
    }
    requireStatus(iterator->status(), "Scan facts");
    for (const auto& [key, value] : staged) {
        if ((limit != 0 && visited >= limit) || !value || persistedKeys.count(key) != 0) continue;
        ++visited;
        ++stats_.factRowsScanned;
        if (!visitor(decodeFactValue(*value))) return;
    }
}

void RocksFactStore::scanAllFacts(std::size_t limit, const FactVisitor& visitor) const {
    ++stats_.fullScans;
    const std::string prefix(1, kNodePrefix);
    std::unordered_map<std::string, std::optional<std::string>> staged;
    for (const auto& [key, value] : stagedValues_) {
        if (!key.empty() && key.front() == kNodePrefix) staged.emplace(key, value);
    }
    std::unique_ptr<rocksdb::Iterator> iterator(database_->NewIterator(rocksdb::ReadOptions{}));
    std::size_t visited = 0;
    std::unordered_set<std::string> persistedKeys;
    for (iterator->Seek(prefix); iterator->Valid() && startsWith(iterator->key(), prefix); iterator->Next()) {
        if (limit != 0 && visited >= limit) break;
        const std::string key = iterator->key().ToString();
        persistedKeys.insert(key);
        const auto pending = staged.find(key);
        if (pending != staged.end() && !pending->second) continue;
        ++visited;
        ++stats_.factRowsScanned;
        const std::string encoded = pending != staged.end()
            ? *pending->second : iterator->value().ToString();
        if (!visitor(decodeFactValue(encoded))) return;
    }
    requireStatus(iterator->status(), "Scan all facts");
    for (const auto& [key, value] : staged) {
        if ((limit != 0 && visited >= limit) || !value || persistedKeys.count(key) != 0) continue;
        ++visited;
        ++stats_.factRowsScanned;
        if (!visitor(decodeFactValue(*value))) return;
    }
}

void RocksFactStore::addFactIndexes(
    const std::string& type, std::uint64_t factId,
    const std::vector<StoredFactIndex>& indexes) {
    if (!transaction_) {
        throw std::logic_error("Fact indexes must be added inside a record transaction");
    }
    std::string encodedId;
    appendU64(encodedId, factId);
    for (const auto& index : indexes) {
        putValue(factIndexKey(type, index, factId), encodedId);
    }
}

void RocksFactStore::removeFactIndexes(
    const std::string& type, std::uint64_t factId,
    const std::vector<StoredFactIndex>& indexes) {
    if (!transaction_) {
        throw std::logic_error("Fact indexes must be removed inside a record transaction");
    }
    for (const auto& index : indexes) {
        deleteValue(factIndexKey(type, index, factId));
    }
}

void RocksFactStore::scanFactIndex(
    const std::string& type, const StoredFactIndex& index,
    std::size_t limit, const FactVisitor& visitor) const {
    ++stats_.indexScans;
    const std::string prefix = factIndexPrefix(type, index);
    std::unique_ptr<rocksdb::Iterator> iterator(
        database_->NewIterator(rocksdb::ReadOptions{}));
    std::size_t visited = 0;
    std::unordered_set<std::string> visitedKeys;
    const auto visitEncodedId = [&](const std::string& encodedId) {
        std::size_t offset = 0;
        const auto id = readU64(encodedId, offset);
        if (offset != encodedId.size()) {
            throw std::runtime_error("Corrupt Felidae RocksDB index value");
        }
        const auto fact = findFactById(id);
        if (!fact) throw std::runtime_error("Dangling Felidae RocksDB index entry");
        ++visited;
        ++stats_.indexRowsScanned;
        return visitor(*fact);
    };
    for (iterator->Seek(prefix);
         iterator->Valid() && startsWith(iterator->key(), prefix);
         iterator->Next()) {
        if (limit != 0 && visited >= limit) break;
        const std::string key = iterator->key().ToString();
        visitedKeys.insert(key);
        const auto staged = stagedValues_.find(key);
        if (staged != stagedValues_.end() && !staged->second) continue;
        const std::string encodedId = staged != stagedValues_.end()
            ? *staged->second : iterator->value().ToString();
        if (!visitEncodedId(encodedId)) return;
    }
    requireStatus(iterator->status(), "Scan fact index");
    for (const auto& [key, value] : stagedValues_) {
        if ((limit != 0 && visited >= limit) || !value ||
            visitedKeys.count(key) != 0 || key.size() < prefix.size() ||
            std::memcmp(key.data(), prefix.data(), prefix.size()) != 0) {
            continue;
        }
        if (!visitEncodedId(*value)) return;
    }
}

bool RocksFactStore::hasDesignation(const std::string& designation) const {
    bool found = false;
    scanDesignation(designation, 1, [&](const StoredFact&) {
        found = true;
        return false;
    });
    return found;
}

void RocksFactStore::scanDesignation(
    const std::string& designation, std::size_t limit,
    const FactVisitor& visitor) const {
    ++stats_.indexScans;
    const std::string prefix = designationPrefix(designation);
    std::unique_ptr<rocksdb::Iterator> iterator(
        database_->NewIterator(rocksdb::ReadOptions{}));
    std::size_t visited = 0;
    std::unordered_set<std::string> persistedKeys;
    const auto visitEncodedId = [&](const std::string& encodedId) {
        std::size_t offset = 0;
        const auto id = readU64(encodedId, offset);
        if (offset != encodedId.size()) {
            throw std::runtime_error("Corrupt Felidae designation index value");
        }
        const auto fact = findFactById(id);
        if (!fact) {
            throw std::runtime_error("Dangling Felidae designation index entry");
        }
        ++visited;
        ++stats_.indexRowsScanned;
        return visitor(*fact);
    };
    for (iterator->Seek(prefix);
         iterator->Valid() && startsWith(iterator->key(), prefix);
         iterator->Next()) {
        if (limit != 0 && visited >= limit) break;
        const std::string key = iterator->key().ToString();
        persistedKeys.insert(key);
        const auto staged = stagedValues_.find(key);
        if (staged != stagedValues_.end() && !staged->second) continue;
        const std::string encodedId = staged != stagedValues_.end()
            ? *staged->second : iterator->value().ToString();
        if (!visitEncodedId(encodedId)) return;
    }
    requireStatus(iterator->status(), "Scan fact designations");
    for (const auto& [key, value] : stagedValues_) {
        if ((limit != 0 && visited >= limit) || !value ||
            persistedKeys.count(key) != 0 || key.size() < prefix.size() ||
            std::memcmp(key.data(), prefix.data(), prefix.size()) != 0) {
            continue;
        }
        if (!visitEncodedId(*value)) return;
    }
}

void RocksFactStore::registerSchema(const std::string& type, const std::string& fingerprint) {
    [[maybe_unused]] auto writeLock = standaloneWriteLock();
    const auto existing = schemaFingerprint(type);
    if (existing && *existing != fingerprint) {
        throw std::runtime_error("Schema for '" + type + "' differs from the immutable database schema");
    }
    if (existing) return;
    putValue(schemaKey(type), fingerprint);
}

void RocksFactStore::promoteSchemalessSchema(
    const std::string& type,
    const std::string& expectedSchemalessFingerprint,
    const std::string& classFingerprint) {
    [[maybe_unused]] auto writeLock = standaloneWriteLock();
    constexpr std::string_view header = "schemaless-v";
    if (expectedSchemalessFingerprint.rfind(header, 0) != 0) {
        throw std::invalid_argument("Schema promotion requires a schemaless source contract");
    }
    const auto existing = schemaFingerprint(type);
    if (!existing || *existing != expectedSchemalessFingerprint) {
        throw std::runtime_error(
            "Schema for '" + type + "' changed while promoting its declared class");
    }
    if (classFingerprint.empty()) {
        throw std::invalid_argument("Declared class fingerprint cannot be empty");
    }
    putValue(schemaKey(type), classFingerprint);
}

std::optional<std::string> RocksFactStore::schemaFingerprint(const std::string& type) const {
    return readValue(schemaKey(type));
}

void RocksFactStore::registerSourceLocator(const StoredSourceLocator& locator) {
    [[maybe_unused]] auto writeLock = standaloneWriteLock();
    if (locator.className.empty() || locator.functionName.empty() || locator.file.empty() ||
        locator.line == 0 || locator.sourceFingerprint.empty() || locator.schemaFingerprint.empty()) {
        throw std::invalid_argument("Source locator requires class, function, file, line, and fingerprints");
    }
    std::string encoded;
    appendBlob(encoded, locator.file);
    appendU64(encoded, locator.line);
    appendBlob(encoded, locator.sourceFingerprint);
    appendBlob(encoded, locator.schemaFingerprint);
    const auto key = sourceLocatorKey(locator.className, locator.functionName);
    const auto existing = readValue(key);
    if (existing && *existing != encoded) {
        throw std::runtime_error(
            "Source locator for '" + locator.className + "." + locator.functionName +
            "' differs from the immutable database locator; use a new database or explicit reimport");
    }
    if (!existing) putValue(key, encoded);
}

std::optional<StoredSourceLocator> RocksFactStore::sourceLocator(
    const std::string& className, const std::string& functionName) const {
    const auto encoded = readValue(sourceLocatorKey(className, functionName));
    if (!encoded) return std::nullopt;
    std::size_t offset = 0;
    StoredSourceLocator locator;
    locator.className = className;
    locator.functionName = functionName;
    locator.file = readBlob(*encoded, offset);
    locator.line = readU64(*encoded, offset);
    locator.sourceFingerprint = readBlob(*encoded, offset);
    locator.schemaFingerprint = readBlob(*encoded, offset);
    if (offset != encoded->size()) throw std::runtime_error("Corrupt Felidae source locator");
    return locator;
}

void RocksFactStore::scanSchemas(const SchemaVisitor& visitor) const {
    const std::string prefix(1, kSchemaPrefix);
    std::unique_ptr<rocksdb::Iterator> iterator(
        database_->NewIterator(rocksdb::ReadOptions{}));
    for (iterator->Seek(prefix);
         iterator->Valid() && startsWith(iterator->key(), prefix);
         iterator->Next()) {
        const std::string key = iterator->key().ToString();
        std::size_t offset = 1;
        const std::string type = readOrderedString(key, offset);
        if (offset != key.size()) {
            throw std::runtime_error("Corrupt Felidae schema key suffix");
        }
        if (!visitor(type, iterator->value().ToString())) break;
    }
    requireStatus(iterator->status(), "Scan schemas");
}

void RocksFactStore::registerTypeParent(
    const std::string& child, const std::string& parent) {
    [[maybe_unused]] auto writeLock = standaloneWriteLock();
    if (child.empty() || parent.empty() || child == parent) {
        throw std::invalid_argument("Type inheritance requires distinct child and parent names");
    }
    putValue(typeParentKey(child, parent), std::string{});
}

void RocksFactStore::scanTypeParents(const TypeParentVisitor& visitor) const {
    const std::string prefix(1, kTypeParentPrefix);
    std::unique_ptr<rocksdb::Iterator> iterator(
        database_->NewIterator(rocksdb::ReadOptions{}));
    for (iterator->Seek(prefix);
         iterator->Valid() && startsWith(iterator->key(), prefix);
         iterator->Next()) {
        const std::string key = iterator->key().ToString();
        std::size_t offset = 1;
        const std::string child = readOrderedString(key, offset);
        const std::string parent = readOrderedString(key, offset);
        if (offset != key.size()) {
            throw std::runtime_error("Corrupt Felidae type inheritance key suffix");
        }
        if (!visitor(child, parent)) break;
    }
    requireStatus(iterator->status(), "Scan type inheritance");
}

StoredLink RocksFactStore::insertLink(const StoredLink& requested) {
    return insertLink(requested, false);
}

StoredLink RocksFactStore::insertLink(
    const StoredLink& requested, bool idempotent) {
    [[maybe_unused]] auto writeLock = standaloneWriteLock();
    if (requested.source == 0 || requested.target == 0) {
        throw std::invalid_argument("Link requires source and target nodes");
    }
    std::optional<StoredLink> duplicate;
    scanLinks(requested.source, true, 0,
        [&](const StoredLink& existing) {
            if (existing.target == requested.target &&
                dataValuesEqual(existing.properties, requested.properties)) {
                duplicate = existing;
                return false;
            }
            return true;
        });
    if (duplicate) {
        if (idempotent) return *duplicate;
        throw std::runtime_error("Duplicate Link");
    }
    StoredLink link = requested;
    ++stats_.linkWrites;
    if (link.id == 0) link.id = allocateId("relationship-sequence");
    const std::string encoded = encodeLinkValue(link);
    if (transaction_) {
        putValue(linkKey(link.id), encoded);
        putValue(adjacencyKey(kOutgoingPrefix, link.source, "Link", link.id), encoded);
        putValue(adjacencyKey(kIncomingPrefix, link.target, "Link", link.id), encoded);
    } else {
        rocksdb::WriteBatch batch;
        batch.Put(linkKey(link.id), encoded);
        batch.Put(adjacencyKey(kOutgoingPrefix, link.source, "Link", link.id), encoded);
        batch.Put(adjacencyKey(kIncomingPrefix, link.target, "Link", link.id), encoded);
        commitBatch(batch, "Insert Link");
    }
    return link;
}

void RocksFactStore::scanLinks(std::uint64_t nodeId, bool outgoing,
                              std::size_t limit,
                              const LinkVisitor& visitor) const {
    ++stats_.linkScans;
    const std::string prefix = adjacencyPrefix(
        outgoing ? kOutgoingPrefix : kIncomingPrefix, nodeId, std::string("Link"));
    std::unique_ptr<rocksdb::Iterator> iterator(database_->NewIterator(rocksdb::ReadOptions{}));
    std::size_t visited = 0;
    std::unordered_set<std::string> visitedKeys;
    for (iterator->Seek(prefix); iterator->Valid() && startsWith(iterator->key(), prefix); iterator->Next()) {
        if (limit != 0 && visited >= limit) break;
        const std::string key = iterator->key().ToString();
        visitedKeys.insert(key);
        const auto staged = stagedValues_.find(key);
        if (staged != stagedValues_.end() && !staged->second) continue;
        ++visited;
        ++stats_.linksVisited;
        const std::string encoded = staged != stagedValues_.end()
            ? *staged->second : iterator->value().ToString();
        if (!visitor(decodeLinkValue(encoded))) return;
    }
    requireStatus(iterator->status(), "Scan Links");
    for (const auto& [key, value] : stagedValues_) {
        if ((limit != 0 && visited >= limit) || !value || visitedKeys.count(key) != 0 ||
            key.size() < prefix.size() || std::memcmp(key.data(), prefix.data(), prefix.size()) != 0) {
            continue;
        }
        ++visited;
        ++stats_.linksVisited;
        if (!visitor(decodeLinkValue(*value))) return;
    }
}

bool RocksFactStore::hasIncomingLinks(std::uint64_t nodeId) const {
    bool found = false;
    scanLinks(nodeId, false, 1, [&](const StoredLink&) {
        found = true;
        return false;
    });
    return found;
}

void RocksFactStore::deleteOutgoingLinks(std::uint64_t nodeId) {
    [[maybe_unused]] auto writeLock = standaloneWriteLock();
    std::vector<StoredLink> outgoing;
    scanLinks(nodeId, true, 0, [&](const StoredLink& edge) {
        outgoing.push_back(edge);
        return true;
    });
    stats_.linkWrites += outgoing.size();
    if (transaction_) {
        for (const auto& edge : outgoing) {
            deleteValue(linkKey(edge.id));
            deleteValue(adjacencyKey(kOutgoingPrefix, edge.source, "Link", edge.id));
            deleteValue(adjacencyKey(kIncomingPrefix, edge.target, "Link", edge.id));
        }
        return;
    }
    rocksdb::WriteBatch batch;
    for (const auto& edge : outgoing) {
        batch.Delete(linkKey(edge.id));
        batch.Delete(adjacencyKey(kOutgoingPrefix, edge.source, "Link", edge.id));
        batch.Delete(adjacencyKey(kIncomingPrefix, edge.target, "Link", edge.id));
    }
    if (outgoing.empty()) return;
    commitBatch(batch, "Delete outgoing Links");
}

StoredClassEdge RocksFactStore::insertClassEdge(
    const StoredClassEdge& requested, bool idempotent) {
    [[maybe_unused]] auto writeLock = standaloneWriteLock();
    if (requested.sourceType.empty() || requested.targetType.empty()) {
        throw std::invalid_argument("Graph.add requires source and target classes");
    }
    if (requested.direction != "forward" && requested.direction != "backward" &&
        requested.direction != "both") {
        throw std::invalid_argument(
            "Class graph direction must be forward.class, backward.class, or both.class");
    }
    std::optional<StoredClassEdge> duplicate;
    scanClassEdges(requested.sourceType, true, 0,
        [&](const StoredClassEdge& existing) {
            if (existing.targetType == requested.targetType &&
                existing.direction == requested.direction &&
                dataValuesEqual(existing.properties, requested.properties)) {
                duplicate = existing;
                return false;
            }
            return true;
        });
    if (duplicate) {
        if (idempotent) return *duplicate;
        throw std::runtime_error("Duplicate class graph edge");
    }
    StoredClassEdge edge = requested;
    if (edge.id == 0) edge.id = allocateId("class-graph-sequence");
    const std::string encoded = encodeClassEdgeValue(edge);
    if (transaction_) {
        putValue(classEdgeKey(edge.id), encoded);
        putValue(classAdjacencyKey(kClassOutgoingPrefix, edge.sourceType, edge.id), encoded);
        putValue(classAdjacencyKey(kClassIncomingPrefix, edge.targetType, edge.id), encoded);
    } else {
        rocksdb::WriteBatch batch;
        batch.Put(classEdgeKey(edge.id), encoded);
        batch.Put(classAdjacencyKey(kClassOutgoingPrefix, edge.sourceType, edge.id), encoded);
        batch.Put(classAdjacencyKey(kClassIncomingPrefix, edge.targetType, edge.id), encoded);
        commitBatch(batch, "Insert class graph edge");
    }
    return edge;
}

void RocksFactStore::scanClassEdges(
    const std::string& type, bool outgoing, std::size_t limit,
    const ClassEdgeVisitor& visitor) const {
    const std::string prefix = classAdjacencyPrefix(
        outgoing ? kClassOutgoingPrefix : kClassIncomingPrefix, type);
    std::unique_ptr<rocksdb::Iterator> iterator(
        database_->NewIterator(rocksdb::ReadOptions{}));
    std::size_t visited = 0;
    std::unordered_set<std::string> visitedKeys;
    for (iterator->Seek(prefix);
         iterator->Valid() && startsWith(iterator->key(), prefix);
         iterator->Next()) {
        if (limit != 0 && visited >= limit) break;
        const std::string key = iterator->key().ToString();
        visitedKeys.insert(key);
        const auto staged = stagedValues_.find(key);
        if (staged != stagedValues_.end() && !staged->second) continue;
        ++visited;
        const std::string encoded = staged != stagedValues_.end()
            ? *staged->second : iterator->value().ToString();
        if (!visitor(decodeClassEdgeValue(encoded))) return;
    }
    requireStatus(iterator->status(), "Scan class graph adjacency");
    for (const auto& [key, value] : stagedValues_) {
        if ((limit != 0 && visited >= limit) || !value ||
            visitedKeys.count(key) != 0 || key.size() < prefix.size() ||
            std::memcmp(key.data(), prefix.data(), prefix.size()) != 0) continue;
        ++visited;
        if (!visitor(decodeClassEdgeValue(*value))) return;
    }
}

void RocksFactStore::scanAllClassEdges(
    std::size_t limit, const ClassEdgeVisitor& visitor) const {
    const std::string prefix(1, kClassEdgePrefix);
    std::unique_ptr<rocksdb::Iterator> iterator(
        database_->NewIterator(rocksdb::ReadOptions{}));
    std::size_t visited = 0;
    std::unordered_set<std::string> visitedKeys;
    for (iterator->Seek(prefix);
         iterator->Valid() && startsWith(iterator->key(), prefix);
         iterator->Next()) {
        if (limit != 0 && visited >= limit) break;
        const std::string key = iterator->key().ToString();
        visitedKeys.insert(key);
        const auto staged = stagedValues_.find(key);
        if (staged != stagedValues_.end() && !staged->second) continue;
        ++visited;
        const std::string encoded = staged != stagedValues_.end()
            ? *staged->second : iterator->value().ToString();
        if (!visitor(decodeClassEdgeValue(encoded))) return;
    }
    requireStatus(iterator->status(), "Scan class graph edges");
    for (const auto& [key, value] : stagedValues_) {
        if ((limit != 0 && visited >= limit) || !value ||
            visitedKeys.count(key) != 0 || key.size() < prefix.size() ||
            std::memcmp(key.data(), prefix.data(), prefix.size()) != 0) continue;
        ++visited;
        if (!visitor(decodeClassEdgeValue(*value))) return;
    }
}

} // namespace Felidae
