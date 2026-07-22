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
inline constexpr std::string_view Type = "__type";
inline constexpr std::string_view Parent = "__parent";
inline constexpr std::string_view Return = "__return";
inline constexpr std::string_view AnonymousPrefix = "__anon";
inline constexpr std::string_view RenamePrefix = "__r";
inline constexpr std::string_view System = "system";
inline constexpr std::string_view Result = "result";
inline constexpr std::string_view SystemResult = "system:result";

inline constexpr SymbolId TypeId = 1;
inline constexpr SymbolId ParentId = 2;
inline constexpr SymbolId ReturnId = 3;
inline constexpr SymbolId SystemId = 4;
inline constexpr SymbolId ResultId = 5;
inline constexpr SymbolId SystemResultId = 6;
}

inline SymbolId symbolIdForName(std::string_view name) {
    if (name == InternalSymbol::Type) return InternalSymbol::TypeId;
    if (name == InternalSymbol::Parent) return InternalSymbol::ParentId;
    if (name == InternalSymbol::Return) return InternalSymbol::ReturnId;
    if (name == InternalSymbol::System) return InternalSymbol::SystemId;
    if (name == InternalSymbol::Result) return InternalSymbol::ResultId;
    if (name == InternalSymbol::SystemResult) return InternalSymbol::SystemResultId;
    return hashSymbol(name);
}

inline SymbolId symbolIdForName(const std::string& name) {
    return symbolIdForName(std::string_view(name));
}

inline std::string makeAnonymousSymbolName(std::size_t id) {
    return std::string(InternalSymbol::AnonymousPrefix) + std::to_string(id);
}

inline std::string makeRenameSymbolPrefix(std::size_t id) {
    return std::string(InternalSymbol::RenamePrefix) + std::to_string(id) + "_";
}

inline bool hasInternalPrefix(const std::string& name, std::string_view prefix) {
    return name.size() >= prefix.size() &&
           name.compare(0, prefix.size(), prefix.data(), prefix.size()) == 0;
}

inline bool isAnonymousSymbolName(const std::string& name) {
    return hasInternalPrefix(name, InternalSymbol::AnonymousPrefix);
}

inline bool isRenamedSymbolName(const std::string& name) {
    return hasInternalPrefix(name, InternalSymbol::RenamePrefix);
}

inline bool isInternalGeneratedSymbolName(const std::string& name) {
    return isAnonymousSymbolName(name) || isRenamedSymbolName(name);
}

} // namespace Felidae
