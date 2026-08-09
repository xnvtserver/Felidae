#pragma once

#include <cstdint>
#include <array>
#include <functional>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Felidae {

using SymbolId = std::uint64_t;

namespace InternalSymbol {
inline constexpr SymbolId TypeId = 1;
inline constexpr SymbolId ParentId = 2;
inline constexpr SymbolId ReturnId = 3;
inline constexpr SymbolId SystemId = 4;
inline constexpr SymbolId ResultId = 5;
inline constexpr SymbolId SystemResultId = 6;
}

enum class InternalSymbolKind : SymbolId {
    Type = InternalSymbol::TypeId,
    Parent = InternalSymbol::ParentId,
    Return = InternalSymbol::ReturnId,
    System = InternalSymbol::SystemId,
    Result = InternalSymbol::ResultId,
    SystemResult = InternalSymbol::SystemResultId
};

inline std::string_view internalSymbolName(InternalSymbolKind kind) {
    switch (kind) {
        case InternalSymbolKind::Type: return "__type";
        case InternalSymbolKind::Parent: return "__parent";
        case InternalSymbolKind::Return: return "__return";
        case InternalSymbolKind::System: return "system";
        case InternalSymbolKind::Result: return "result";
        case InternalSymbolKind::SystemResult: return "system:result";
    }
    return "";
}

inline std::string internalSymbolString(InternalSymbolKind kind) {
    return std::string(internalSymbolName(kind));
}

// Identifier hashes are not identities: two different names are allowed to
// have the same hash.  The old implementation used std::hash directly as a
// SymbolId and therefore had to retain and compare strings throughout every
// hot lookup to defend against collisions.  This process-wide interner gives
// every spelling one canonical, collision-free id while keeping source text
// on AST nodes for diagnostics and serialization.
class SymbolInterner {
public:
    static constexpr SymbolId GeneratedIdBase = SymbolId{1} << 63U;
    SymbolInterner() {
        reserve(InternalSymbol::TypeId, internalSymbolName(InternalSymbolKind::Type));
        reserve(InternalSymbol::ParentId, internalSymbolName(InternalSymbolKind::Parent));
        reserve(InternalSymbol::ReturnId, internalSymbolName(InternalSymbolKind::Return));
        reserve(InternalSymbol::SystemId, internalSymbolName(InternalSymbolKind::System));
        reserve(InternalSymbol::ResultId, internalSymbolName(InternalSymbolKind::Result));
        reserve(InternalSymbol::SystemResultId, internalSymbolName(InternalSymbolKind::SystemResult));
    }

    SymbolId intern(std::string_view name) {
        if (name.empty()) return 0;
        const std::string key(name);
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = ids_.find(key);
        if (found != ids_.end()) return found->second;
        const SymbolId id = nextId_++;
        ids_.emplace(key, id);
        names_.push_back(key);
        return id;
    }

    SymbolId makeGenerated() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (nextGeneratedId_ == std::numeric_limits<SymbolId>::max()) {
            throw std::overflow_error("Felidae generated SymbolId space exhausted");
        }
        const SymbolId id = nextGeneratedId_++;
        generatedNames_.emplace(id, "$g" + std::to_string(id - GeneratedIdBase));
        return id;
    }

    std::string name(SymbolId id) const {
        if (id == 0) return {};
        std::lock_guard<std::mutex> lock(mutex_);
        const auto reserved = reservedNames_.find(id);
        if (reserved != reservedNames_.end()) return reserved->second;
        const auto generated = generatedNames_.find(id);
        if (generated != generatedNames_.end()) return generated->second;
        if (id < 1024) return {};
        const std::size_t index = static_cast<std::size_t>(id - 1024);
        return index < names_.size() ? names_[index] : std::string{};
    }

private:
    void reserve(SymbolId id, std::string_view name) {
        ids_.emplace(std::string(name), id);
        reservedNames_.emplace(id, std::string(name));
    }

    mutable std::mutex mutex_;
    std::unordered_map<std::string, SymbolId> ids_;
    std::unordered_map<SymbolId, std::string> reservedNames_;
    std::unordered_map<SymbolId, std::string> generatedNames_;
    std::vector<std::string> names_;
    SymbolId nextId_ = 1024;
    SymbolId nextGeneratedId_ = GeneratedIdBase;
};

inline SymbolInterner& symbolInterner() {
    static SymbolInterner interner;
    return interner;
}

inline SymbolId symbolIdForName(std::string_view name) {
    // Parsing fact-heavy sources repeatedly sees the same small set of type
    // and field identifiers. Avoid taking the process-wide interner mutex for
    // every occurrence while still confirming the spelling on hash collision.
    struct LocalEntry {
        std::size_t hash = 0;
        std::string spelling;
        SymbolId id = 0;
    };
    thread_local std::array<LocalEntry, 64> local{};
    const std::size_t hash = std::hash<std::string_view>{}(name);
    auto& entry = local[hash % local.size()];
    if (entry.id != 0 && entry.hash == hash && entry.spelling == name) {
        return entry.id;
    }
    const SymbolId id = symbolInterner().intern(name);
    entry.hash = hash;
    entry.spelling.assign(name.data(), name.size());
    entry.id = id;
    return id;
}

inline SymbolId symbolIdForName(const std::string& name) {
    return symbolIdForName(std::string_view(name));
}

inline SymbolId symbolIdForName(const char* name) {
    return symbolIdForName(std::string_view(name ? name : ""));
}

inline std::string symbolNameForId(SymbolId id) {
    return symbolInterner().name(id);
}

inline bool isInternalGeneratedSymbolId(SymbolId id) {
    return id != 0 && id >= SymbolInterner::GeneratedIdBase;
}

} // namespace Felidae
