#pragma once

#include <cstdint>
#include <array>
#include <functional>
#include <mutex>
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

    std::string name(SymbolId id) const {
        if (id == 0) return {};
        if (id < 1024) {
            switch (id) {
                case InternalSymbol::TypeId: return "__type";
                case InternalSymbol::ParentId: return "__parent";
                case InternalSymbol::ReturnId: return "__return";
                case InternalSymbol::SystemId: return "system";
                case InternalSymbol::ResultId: return "result";
                case InternalSymbol::SystemResultId: return "system:result";
                default: return {};
            }
        }
        std::lock_guard<std::mutex> lock(mutex_);
        const std::size_t index = static_cast<std::size_t>(id - 1024);
        return index < names_.size() ? names_[index] : std::string{};
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, SymbolId> ids_;
    std::vector<std::string> names_;
    SymbolId nextId_ = 1024;
};

inline SymbolInterner& symbolInterner() {
    static SymbolInterner interner;
    return interner;
}

inline SymbolId symbolIdForName(std::string_view name) {
    if (name == internalSymbolName(InternalSymbolKind::Type)) return InternalSymbol::TypeId;
    if (name == internalSymbolName(InternalSymbolKind::Parent)) return InternalSymbol::ParentId;
    if (name == internalSymbolName(InternalSymbolKind::Return)) return InternalSymbol::ReturnId;
    if (name == internalSymbolName(InternalSymbolKind::System)) return InternalSymbol::SystemId;
    if (name == internalSymbolName(InternalSymbolKind::Result)) return InternalSymbol::ResultId;
    if (name == internalSymbolName(InternalSymbolKind::SystemResult)) return InternalSymbol::SystemResultId;
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

inline std::string makeAnonymousSymbolName(std::size_t id) {
    return "__anon" + std::to_string(id);
}

inline std::string makeRenameSymbolPrefix(std::size_t id) {
    return "__r" + std::to_string(id) + "_";
}

inline bool hasInternalPrefix(const std::string& name, std::string_view prefix) {
    return name.size() >= prefix.size() &&
           name.compare(0, prefix.size(), prefix.data(), prefix.size()) == 0;
}

inline bool isAnonymousSymbolName(const std::string& name) {
    return hasInternalPrefix(name, "__anon");
}

inline bool isRenamedSymbolName(const std::string& name) {
    return hasInternalPrefix(name, "__r");
}

inline bool isInternalGeneratedSymbolName(const std::string& name) {
    return isAnonymousSymbolName(name) || isRenamedSymbolName(name);
}

} // namespace Felidae
