#pragma once

#include "Token.h"
#include <string>

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
BuiltinId builtinIdForName(const char* name);
bool isBuiltinFunctionName(const std::string& name);
const char* builtinName(BuiltinId id);
BuiltinEffect builtinEffect(BuiltinId id);
BuiltinEffect builtinEffect(const std::string& name);
bool isBuiltinPure(BuiltinId id);
bool isBuiltinPure(const std::string& name);

} // namespace Felidae
