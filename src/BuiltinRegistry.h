#pragma once

#include "FelidaeGrammar.h"
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
// Parser-facing native resolution.  The input is a complete qualified source
// name from the SentencePiece stream (including atomic separator IDs), not a
// decoded spelling.  This keeps grammar assembly independent of string
// comparisons while retaining BuiltinId as the interpreter's semantic ID.
BuiltinId builtinIdForPieceIds(const std::vector<TokenId::Id>& pieceIds);
bool isBuiltinFunctionName(const std::string& name);
const char* builtinName(BuiltinId id);
BuiltinEffect builtinEffect(BuiltinId id);
BuiltinEffect builtinEffect(const std::string& name);
bool isBuiltinPure(BuiltinId id);
bool isBuiltinPure(const std::string& name);
// Compiler-known namespaces do not identify source files and need no module
// linking. Keep this decision beside the builtin registry so frontend import
// resolution and IR lowering cannot maintain divergent allowlists.
bool isBuiltinModuleName(std::string_view name);

// Every registered builtin except the Unknown sentinel, in table order.
const std::vector<BuiltinInfo>& allBuiltins();

} // namespace Felidae
