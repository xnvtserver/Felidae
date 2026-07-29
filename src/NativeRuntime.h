#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Felidae {

using NativeCallFn = char* (*)(const char*, const char*);
using NativeFreeFn = void (*)(char*);
using NativeManifestFn = const char* (*)();

struct NativeCapabilities {
    bool needsFactSnapshot = false;
    bool needsFactHierarchy = false;
    bool pure = false;
    bool threadSafe = false;
    bool supportsBatch = false;
    // Whether this contract can consume a FactSelection cursor.  The JSON
    // ABI still receives a bounded materialized projection; keeping this on
    // the contract prevents the interpreter from guessing from module names.
    bool acceptsFactSelections = false;
    // A selection cardinality request can be answered from its cursor without
    // materializing the selected facts for the native boundary.
    bool selectionCardinality = false;
    std::vector<std::string> requestedFactTypes;
};

enum class NativeArgumentConstraintKind {
    StringOption,
    UnitInterval,
    PositiveFinite,
    StringArray,
    BooleanText,
};

struct NativeArgumentConstraint {
    std::string name;
    std::size_t positionalIndex = 0;
    NativeArgumentConstraintKind kind = NativeArgumentConstraintKind::StringOption;
    bool required = false;
    std::vector<std::string> allowedValues;
};

struct NativeContract {
    NativeCapabilities capabilities;
    std::vector<NativeArgumentConstraint> argumentConstraints;
};

struct NativeModuleManifest {
    std::string moduleName;
    std::uint32_t abiVersion = 0;
    NativeContract defaultContract;
    std::unordered_map<std::string, NativeContract> functions;

    const NativeContract& contractFor(const std::string& functionName) const;
};

struct NativeLibrary {
    std::filesystem::path path;
    std::string moduleName;
    void* handle = nullptr;
    NativeCallFn call = nullptr;
    NativeFreeFn free = nullptr;
    NativeModuleManifest manifest;
};

bool parseNativeModuleManifest(const std::string& json,
                               NativeModuleManifest& manifest,
                               std::string& error);
std::vector<std::string> nativeLibraryFileNames(const std::string& moduleName);
bool hasNativeLibraryExtension(const std::filesystem::path& path);
void* openSharedLibrary(const std::filesystem::path& path);
std::string sharedLibraryError();
void closeSharedLibrary(void* handle);
void* findSharedLibrarySymbol(void* handle, const char* name);
std::string nativeModuleNameFromPath(const std::filesystem::path& path);

} // namespace Felidae
