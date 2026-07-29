#include "NativeRuntime.h"

#include "../native_modules/common/NativeJson.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

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

const NativeContract& NativeModuleManifest::contractFor(const std::string& functionName) const {
    const auto exact = functions.find(functionName);
    if (exact != functions.end()) return exact->second;
    const size_t separator = functionName.rfind(':');
    if (separator != std::string::npos) {
        const auto shortName = functions.find(functionName.substr(separator + 1));
        if (shortName != functions.end()) return shortName->second;
    }
    return defaultContract;
}

bool parseNativeModuleManifest(const std::string& json,
                               NativeModuleManifest& manifest,
                               std::string& error) {
    using Felidae::NativeJson::Value;
    size_t position = 0;
    Value root;
    if (!Felidae::NativeJson::parseValue(json, position, root)) {
        error = "invalid JSON";
        return false;
    }
    Felidae::NativeJson::skipWhitespace(json, position);
    if (position != json.size() || root.kind != Value::Kind::Object) {
        error = "manifest must be one JSON object";
        return false;
    }
    const auto field = [](const Value& object, const char* name) -> const Value* {
        const auto it = object.fields.find(name);
        return it == object.fields.end() ? nullptr : &it->second;
    };
    const auto text = [&](const Value& object, const char* name, std::string& out) -> bool {
        const Value* value = field(object, name);
        if (!value || value->kind != Value::Kind::String || value->text.empty()) return false;
        out = value->text;
        return true;
    };
    const Value* schema = field(root, "schema_version");
    const Value* abi = field(root, "abi_version");
    std::string module;
    if (!schema || schema->kind != Value::Kind::Number || schema->number != 1.0 ||
        !abi || abi->kind != Value::Kind::Number || abi->number != 1.0 ||
        !text(root, "module", module)) {
        error = "manifest requires schema_version 1, abi_version 1, and a module name";
        return false;
    }

    const auto parseContract = [&](const Value* object, NativeContract& contract) -> bool {
        if (!object) return true;
        if (object->kind != Value::Kind::Object) return false;
        const auto boolean = [&](const char* name, bool& target) -> bool {
            const Value* value = field(*object, name);
            if (!value) return true;
            if (value->kind != Value::Kind::Bool) return false;
            target = value->boolean;
            return true;
        };
        auto& capabilities = contract.capabilities;
        if (!boolean("pure", capabilities.pure) ||
            !boolean("thread_safe", capabilities.threadSafe) ||
            !boolean("supports_batch", capabilities.supportsBatch) ||
            !boolean("accepts_fact_selections", capabilities.acceptsFactSelections) ||
            !boolean("selection_cardinality", capabilities.selectionCardinality) ||
            !boolean("needs_fact_snapshot", capabilities.needsFactSnapshot) ||
            !boolean("needs_fact_hierarchy", capabilities.needsFactHierarchy)) {
            return false;
        }
        const Value* types = field(*object, "requested_fact_types");
        if (types) {
            if (types->kind != Value::Kind::Array) return false;
            capabilities.requestedFactTypes.clear();
            capabilities.requestedFactTypes.reserve(types->items.size());
            for (const Value& item : types->items) {
                if (item.kind != Value::Kind::String || item.text.empty()) return false;
                capabilities.requestedFactTypes.push_back(item.text);
            }
        }
        const Value* constraints = field(*object, "argument_constraints");
        if (constraints) {
            if (constraints->kind != Value::Kind::Array) return false;
            contract.argumentConstraints.clear();
            contract.argumentConstraints.reserve(constraints->items.size());
            for (const Value& item : constraints->items) {
                if (item.kind != Value::Kind::Object) return false;
                std::string name;
                std::string kind;
                const Value* index = field(item, "index");
                if (!text(item, "name", name) || !text(item, "kind", kind) ||
                    !index || index->kind != Value::Kind::Number ||
                    index->number < 0.0 || index->number > static_cast<double>(std::numeric_limits<size_t>::max()) ||
                    std::floor(index->number) != index->number) {
                    return false;
                }
                NativeArgumentConstraint constraint;
                constraint.name = std::move(name);
                constraint.positionalIndex = static_cast<size_t>(index->number);
                if (kind == "string_option") constraint.kind = NativeArgumentConstraintKind::StringOption;
                else if (kind == "unit_interval") constraint.kind = NativeArgumentConstraintKind::UnitInterval;
                else if (kind == "positive_finite") constraint.kind = NativeArgumentConstraintKind::PositiveFinite;
                else if (kind == "string_array") constraint.kind = NativeArgumentConstraintKind::StringArray;
                else if (kind == "boolean_text") constraint.kind = NativeArgumentConstraintKind::BooleanText;
                else return false;
                const Value* required = field(item, "required");
                if (required) {
                    if (required->kind != Value::Kind::Bool) return false;
                    constraint.required = required->boolean;
                }
                const Value* values = field(item, "values");
                if (values) {
                    if (values->kind != Value::Kind::Array) return false;
                    constraint.allowedValues.reserve(values->items.size());
                    for (const Value& value : values->items) {
                        if (value.kind != Value::Kind::String || value.text.empty()) return false;
                        constraint.allowedValues.push_back(value.text);
                    }
                }
                if (constraint.kind == NativeArgumentConstraintKind::StringOption &&
                    constraint.allowedValues.empty()) return false;
                contract.argumentConstraints.push_back(std::move(constraint));
            }
        }
        return true;
    };

    NativeModuleManifest parsed;
    parsed.moduleName = std::move(module);
    parsed.abiVersion = 1;
    if (!parseContract(field(root, "capabilities"), parsed.defaultContract)) {
        error = "invalid capabilities";
        return false;
    }
    const Value* functions = field(root, "functions");
    if (functions) {
        if (functions->kind != Value::Kind::Object) {
            error = "functions must be an object";
            return false;
        }
        for (const auto& item : functions->fields) {
            if (item.first.empty()) {
                error = "function name cannot be empty";
                return false;
            }
            NativeContract contract = parsed.defaultContract;
            if (!parseContract(&item.second, contract)) {
                error = "invalid function contract for '" + item.first + "'";
                return false;
            }
            parsed.functions.emplace(item.first, std::move(contract));
        }
    }
    manifest = std::move(parsed);
    return true;
}

#if 0 // Obsolete hard-coded native option contract; manifests own this metadata.
        option("lexical_algorithm", 3, {"path", "wup", "wu_palmer", "Wu-Palmer", "Wu Palmer", "resnik", "jiang_conrath", "Jiang-Conrath", "Jiang Conrath", "lin", "edit", "Leacock-Chodorow", "Leacockâ€“Chodorow", "Leacock Chodorow", "leacock_chodorow", "lch"});
#endif
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
