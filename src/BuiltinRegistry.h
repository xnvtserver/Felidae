#pragma once

#include <string>

namespace Felidae {

enum class BuiltinEffect {
    Pure,
    ReadsExternalState,
    WritesExternalState,
    Volatile
};

struct BuiltinInfo {
    const char* name;
    BuiltinEffect effect;
};

bool isBuiltinFunctionName(const std::string& name);
BuiltinEffect builtinEffect(const std::string& name);
bool isBuiltinPure(const std::string& name);

} // namespace Felidae
