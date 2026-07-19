#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Felidae {

using NativeCallFn = char* (*)(const char*, const char*);
using NativeFreeFn = void (*)(char*);

struct NativeLibrary {
    std::filesystem::path path;
    std::string moduleName;
    void* handle = nullptr;
    NativeCallFn call = nullptr;
    NativeFreeFn free = nullptr;
};

std::vector<std::string> nativeLibraryFileNames(const std::string& moduleName);
bool hasNativeLibraryExtension(const std::filesystem::path& path);
void* openSharedLibrary(const std::filesystem::path& path);
std::string sharedLibraryError();
void closeSharedLibrary(void* handle);
void* findSharedLibrarySymbol(void* handle, const char* name);
std::string nativeModuleNameFromPath(const std::filesystem::path& path);

} // namespace Felidae
