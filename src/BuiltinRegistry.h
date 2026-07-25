#pragma once

#include "Token.h"
#include <string>
#include <string_view>
#include <vector>

namespace Felidae {

enum class BuiltinEffect {
    Pure,
    ReadsExternalState,
    WritesExternalState,
    Volatile
};

struct BuiltinInfo {
    BuiltinId id;
    const char* name;
    BuiltinEffect effect;
};

BuiltinId builtinIdForName(const std::string& name);
BuiltinId builtinIdForName(std::string_view name);
BuiltinId builtinIdForName(const char* name);
bool isBuiltinFunctionName(const std::string& name);
const char* builtinName(BuiltinId id);
BuiltinEffect builtinEffect(BuiltinId id);
BuiltinEffect builtinEffect(const std::string& name);
bool isBuiltinPure(BuiltinId id);
bool isBuiltinPure(const std::string& name);

// Every registered builtin except the Unknown sentinel, in table order.
// Editor/tooling integrations should call this (via `celidae --list-builtins`)
// instead of hand-maintaining a copy of the builtin name list.
const std::vector<BuiltinInfo>& allBuiltins();

} // namespace Felidae
