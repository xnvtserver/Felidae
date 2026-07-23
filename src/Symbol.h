#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace Felidae {

using SymbolId = std::uint64_t;

inline SymbolId hashSymbol(std::string_view text) {
    auto hash = static_cast<SymbolId>(std::hash<std::string_view>{}(text));
    if (hash > 0 && hash < 1024) hash += 1024;
    return hash == 0 ? 1ULL : hash;
}

inline SymbolId hashSymbol(const std::string& text) {
    return hashSymbol(std::string_view(text));
}

inline SymbolId hashSymbol(const char* text) {
    return hashSymbol(std::string_view(text ? text : ""));
}

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

inline SymbolId symbolIdForName(std::string_view name) {
    if (name == internalSymbolName(InternalSymbolKind::Type)) return InternalSymbol::TypeId;
    if (name == internalSymbolName(InternalSymbolKind::Parent)) return InternalSymbol::ParentId;
    if (name == internalSymbolName(InternalSymbolKind::Return)) return InternalSymbol::ReturnId;
    if (name == internalSymbolName(InternalSymbolKind::System)) return InternalSymbol::SystemId;
    if (name == internalSymbolName(InternalSymbolKind::Result)) return InternalSymbol::ResultId;
    if (name == internalSymbolName(InternalSymbolKind::SystemResult)) return InternalSymbol::SystemResultId;
    return hashSymbol(name);
}

inline SymbolId symbolIdForName(const std::string& name) {
    return symbolIdForName(std::string_view(name));
}

inline SymbolId symbolIdForName(const char* name) {
    return symbolIdForName(std::string_view(name ? name : ""));
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
