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

// Source symbols use deterministic 63-bit FNV-1a IDs. The compiler retains
// spellings only while compiling to detect the astronomically unlikely hash
// collision; binary IR and the VM carry IDs only.
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
        const SymbolId id = deterministicId(name);
        const auto collision = names_.find(id);
        if (collision != names_.end() && collision->second != key) {
            throw std::runtime_error("Felidae SymbolId hash collision");
        }
        ids_.emplace(key, id);
        names_.emplace(id, key);
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
        const auto found = names_.find(id);
        return found == names_.end() ? std::string{} : found->second;
    }

private:
    void reserve(SymbolId id, std::string_view name) {
        ids_.emplace(std::string(name), id);
        reservedNames_.emplace(id, std::string(name));
    }
    static SymbolId deterministicId(std::string_view name) noexcept {
        SymbolId value = 1469598103934665603ull;
        for (const unsigned char byte : name) { value ^= byte; value *= 1099511628211ull; }
        value &= ~(SymbolId{1} << 63U);
        value |= SymbolId{1} << 62U;
        return value;
    }

    mutable std::mutex mutex_;
    std::unordered_map<std::string, SymbolId> ids_;
    std::unordered_map<SymbolId, std::string> reservedNames_;
    std::unordered_map<SymbolId, std::string> generatedNames_;
    std::unordered_map<SymbolId, std::string> names_;
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
