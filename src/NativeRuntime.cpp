#include "NativeRuntime.h"

#include <algorithm>
#include <cctype>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace Felidae {

namespace {

std::string lowerText(std::string text) {
    for (char& ch : text) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return text;
}

} // namespace

std::vector<std::string> nativeLibraryFileNames(const std::string& moduleName) {
#if defined(_WIN32)
    return {moduleName + ".dll", "felidae_" + moduleName + ".dll"};
#elif defined(__APPLE__)
    return {"lib" + moduleName + ".dylib", "libfelidae_" + moduleName + ".dylib", moduleName + ".dylib"};
#else
    return {"lib" + moduleName + ".so", "libfelidae_" + moduleName + ".so", moduleName + ".so"};
#endif
}

bool hasNativeLibraryExtension(const std::filesystem::path& path) {
    std::string extension = lowerText(path.extension().string());
#if defined(_WIN32)
    return extension == ".dll";
#elif defined(__APPLE__)
    return extension == ".dylib" || extension == ".so";
#else
    return extension == ".so";
#endif
}

void* openSharedLibrary(const std::filesystem::path& path) {
#if defined(__EMSCRIPTEN__)
    (void)path;
    return nullptr;
#elif defined(_WIN32)
    HMODULE handle = LoadLibraryW(path.wstring().c_str());
    return reinterpret_cast<void*>(handle);
#else
    return dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

std::string sharedLibraryError() {
#if defined(__EMSCRIPTEN__)
    return "native packages are unsupported in the Felidae WASM runtime";
#elif defined(_WIN32)
    DWORD code = GetLastError();
    if (code == 0) return "unknown loader error";
    LPSTR buffer = nullptr;
    DWORD size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&buffer),
        0,
        nullptr);
    std::string message = size && buffer ? std::string(buffer, size) : ("Windows loader error " + std::to_string(code));
    if (buffer) LocalFree(buffer);
    return message;
#else
    const char* error = dlerror();
    return error ? std::string(error) : "unknown loader error";
#endif
}

void closeSharedLibrary(void* handle) {
    if (!handle) return;
#if defined(_WIN32)
    FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
}

void* findSharedLibrarySymbol(void* handle, const char* name) {
    if (!handle) return nullptr;
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(handle), name));
#else
    return dlsym(handle, name);
#endif
}

std::string nativeModuleNameFromPath(const std::filesystem::path& path) {
    std::string stem = path.stem().string();
    const std::string felidaePrefix = "felidae_";
    const std::string libFelidaePrefix = "libfelidae_";
    const std::string libPrefix = "lib";
    if (stem.rfind(libFelidaePrefix, 0) == 0) return stem.substr(libFelidaePrefix.size());
    if (stem.rfind(felidaePrefix, 0) == 0) return stem.substr(felidaePrefix.size());
    if (stem.rfind(libPrefix, 0) == 0) return stem.substr(libPrefix.size());
    return stem;
}

} // namespace Felidae
