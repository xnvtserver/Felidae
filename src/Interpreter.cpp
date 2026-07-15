#include "Interpreter.h"
#include "Lexer.h"
#include "Visualization.h"
#include "../native_modules/csv/NativeCsv.h"
#include "../native_modules/http/NativeHttp.h"
#include "../native_modules/process/NativeProcess.h"
#include "FelidaeRuntime.h"
#include "Parser.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <cstring>
#include <random>
#include <set>
#include <sstream>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#ifdef FELIDAE_HAS_EIGEN
#include <Eigen/Dense>
#endif

namespace Felidae {
namespace fs = std::filesystem;

static std::string jsonEscape(const std::string& value) {
    std::ostringstream out;
    for (char c : value) {
        switch (c) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out << "\\u";
                    out << std::hex << std::uppercase << std::setw(4) << std::setfill('0')
                        << static_cast<int>(static_cast<unsigned char>(c));
                    out << std::dec << std::nouppercase;
                } else {
                    out << c;
                }
        }
    }
    return out.str();
}

static bool argAsNumber(const std::shared_ptr<Expr>& expr, double& out) {
    if (auto n = std::dynamic_pointer_cast<NumberExpr>(expr)) {
        out = n->value;
        return true;
    }
    return false;
}

static bool argAsString(const std::shared_ptr<Expr>& expr, std::string& out) {
    if (auto s = std::dynamic_pointer_cast<StringExpr>(expr)) {
        out = s->value;
        return true;
    }
    return false;
}

static std::vector<std::shared_ptr<Expr>> termArgs(const std::shared_ptr<Expr>& expr,
                                                   const std::string& name) {
    if (auto t = std::dynamic_pointer_cast<TermExpr>(expr)) {
        if (t->name == name) {
            std::vector<std::shared_ptr<Expr>> out;
            for (const auto& arg : t->args) out.push_back(arg.value);
            return out;
        }
    }
    if (name == "fn:array") {
        if (auto a = std::dynamic_pointer_cast<ArrayExpr>(expr)) return a->items;
    }
    return {};
}

static std::shared_ptr<Expr> findMapValue(const std::shared_ptr<Expr>& expr,
                                          const std::string& key) {
    if (auto m = std::dynamic_pointer_cast<MapExpr>(expr)) {
        for (const auto& entry : m->entries) {
            if (entry.key == key) return entry.value;
        }
    }
    if (auto t = std::dynamic_pointer_cast<TermExpr>(expr)) {
        if (t->name == "json:object") {
            for (const auto& field : t->args) {
                auto pair = termArgs(field.value, "fn:pair");
                std::string fieldName;
                if (pair.size() == 2 && argAsString(pair[0], fieldName) && fieldName == key) {
                    return pair[1];
                }
            }
        }
    }
    return {};
}

static std::vector<MapEntry> cloneEntries(const std::vector<MapEntry>& entries) {
    std::vector<MapEntry> copied;
    copied.reserve(entries.size());
    for (const auto& entry : entries) copied.push_back(MapEntry{entry.key, entry.value->clone()});
    return copied;
}

static std::shared_ptr<Expr> cloneExprOrNil(const std::shared_ptr<Expr>& value) {
    return value ? value->clone() : std::static_pointer_cast<Expr>(std::make_shared<NilExpr>());
}

static bool exprAsMapEntries(const std::shared_ptr<Expr>& expr, std::vector<MapEntry>& out) {
    if (auto map = std::dynamic_pointer_cast<MapExpr>(expr)) {
        out = cloneEntries(map->entries);
        return true;
    }
    if (auto term = std::dynamic_pointer_cast<TermExpr>(expr)) {
        if (term->name == "json:object") {
            out.clear();
            for (const auto& field : term->args) {
                auto pair = termArgs(field.value, "fn:pair");
                std::string key;
                if (pair.size() != 2 || !argAsString(pair[0], key)) return false;
                out.push_back(MapEntry{key, pair[1]->clone()});
            }
            return true;
        }
    }
    return false;
}

static bool exprAsArrayItems(const std::shared_ptr<Expr>& expr, std::vector<std::shared_ptr<Expr>>& out) {
    if (auto array = std::dynamic_pointer_cast<ArrayExpr>(expr)) {
        out.clear();
        out.reserve(array->items.size());
        for (const auto& item : array->items) out.push_back(cloneExprOrNil(item));
        return true;
    }
    if (auto term = std::dynamic_pointer_cast<TermExpr>(expr)) {
        if (term->name == "fn:array") {
            out.clear();
            for (const auto& arg : term->args) {
                if (arg.name == "data") {
                    auto data = std::dynamic_pointer_cast<ArrayExpr>(arg.value);
                    if (!data) return false;
                    for (const auto& item : data->items) out.push_back(cloneExprOrNil(item));
                    return true;
                }
                out.push_back(cloneExprOrNil(arg.value));
            }
            return true;
        }
    }
    return false;
}

static double requireNumber(const std::shared_ptr<Expr>& expr, const std::string& fn, const std::string& arg);

static std::vector<double> requireNumberArray(const std::shared_ptr<Expr>& expr, const std::string& fn, const std::string& arg) {
    std::vector<std::shared_ptr<Expr>> items;
    if (!exprAsArrayItems(expr, items)) throw InterpreterError(fn + " expects numeric array argument '" + arg + "'");
    std::vector<double> numbers;
    numbers.reserve(items.size());
    for (const auto& item : items) numbers.push_back(requireNumber(item, fn, arg));
    return numbers;
}

static std::shared_ptr<ArrayExpr> numbersToArray(const std::vector<double>& values) {
    std::vector<std::shared_ptr<Expr>> items;
    items.reserve(values.size());
    for (double value : values) items.push_back(std::make_shared<NumberExpr>(value));
    return std::make_shared<ArrayExpr>(std::move(items));
}

static double factorialTerm(double n, const std::string& fn, const std::string& arg) {
    if (n < 0 || std::floor(n) != n) throw InterpreterError(fn + " expects non-negative integer argument '" + arg + "'");
    return n;
}

static void upsertEntry(std::vector<MapEntry>& entries, const std::string& key, std::shared_ptr<Expr> value) {
    for (auto& entry : entries) {
        if (entry.key == key) {
            entry.value = std::move(value);
            return;
        }
    }
    entries.push_back(MapEntry{key, std::move(value)});
}

static bool removeEntry(std::vector<MapEntry>& entries, const std::string& key) {
    auto oldSize = entries.size();
    entries.erase(std::remove_if(entries.begin(), entries.end(),
        [&](const MapEntry& entry) { return entry.key == key; }), entries.end());
    return entries.size() != oldSize;
}

static std::string exprTextValue(const std::shared_ptr<Expr>& expr) {
    if (auto str = std::dynamic_pointer_cast<StringExpr>(expr)) return str->value;
    return expr ? expr->debug() : "nil";
}

static bool mapFieldMatches(const std::shared_ptr<Expr>& row, const std::string& key, const std::string& expected) {
    auto value = findMapValue(row, key);
    return value && exprTextValue(value) == expected;
}

static std::shared_ptr<Expr> patchMapValue(const std::shared_ptr<Expr>& row, const std::shared_ptr<Expr>& patch) {
    std::vector<MapEntry> entries;
    if (!exprAsMapEntries(row, entries)) return nullptr;
    std::vector<MapEntry> patchEntries;
    if (!exprAsMapEntries(patch, patchEntries)) return nullptr;
    for (const auto& entry : patchEntries) upsertEntry(entries, entry.key, cloneExprOrNil(entry.value));
    return std::make_shared<MapExpr>(std::move(entries));
}

static bool exprEqualsLiteral(const std::shared_ptr<Expr>& a, const std::shared_ptr<Expr>& b) {
    if (auto sa = std::dynamic_pointer_cast<StringExpr>(a)) {
        auto sb = std::dynamic_pointer_cast<StringExpr>(b);
        return sb && sa->value == sb->value;
    }
    if (auto na = std::dynamic_pointer_cast<NumberExpr>(a)) {
        auto nb = std::dynamic_pointer_cast<NumberExpr>(b);
        return nb && std::fabs(na->value - nb->value) < 1e-12;
    }
    if (std::dynamic_pointer_cast<NilExpr>(a) || std::dynamic_pointer_cast<NilExpr>(b)) {
        return static_cast<bool>(std::dynamic_pointer_cast<NilExpr>(a)) &&
               static_cast<bool>(std::dynamic_pointer_cast<NilExpr>(b));
    }
    return a->debug() == b->debug();
}

static bool exprContainsLiteral(const std::shared_ptr<Expr>& haystack, const std::shared_ptr<Expr>& needle) {
    if (exprEqualsLiteral(haystack, needle)) return true;
    std::vector<std::shared_ptr<Expr>> items;
    if (!exprAsArrayItems(haystack, items)) return false;
    for (const auto& item : items) {
        if (exprEqualsLiteral(item, needle)) return true;
    }
    return false;
}

static void appendFactEntry(std::vector<MapEntry>& entries, const std::string& key, std::shared_ptr<Expr> value) {
    for (auto& entry : entries) {
        if (entry.key != key) continue;
        if (auto array = std::dynamic_pointer_cast<ArrayExpr>(entry.value)) {
            array->items.push_back(std::move(value));
        } else {
            std::vector<std::shared_ptr<Expr>> items;
            items.push_back(entry.value);
            items.push_back(std::move(value));
            entry.value = std::make_shared<ArrayExpr>(std::move(items));
        }
        return;
    }
    entries.push_back(MapEntry{key, std::move(value)});
}

static bool isMethodTruthTupleWithFalse(const std::shared_ptr<Expr>& expr) {
    auto tuple = std::dynamic_pointer_cast<TermExpr>(expr);
    if (!tuple || tuple->name != "fn:tuple" || tuple->args.empty()) return false;
    bool sawBool = false;
    for (const auto& arg : tuple->args) {
        auto text = std::dynamic_pointer_cast<StringExpr>(arg.value);
        if (!text) return false;
        if (text->value != "true" && text->value != "false") return false;
        sawBool = true;
        if (text->value == "false") return true;
    }
    return sawBool && false;
}

static std::string lowerText(std::string text) {
    for (char& ch : text) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return text;
}

static std::string upperText(std::string text) {
    for (char& ch : text) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return text;
}

static const Arg* findTermArgByNameOrIndex(const TermExpr& term, const std::string& name, size_t index) {
    for (const auto& arg : term.args) {
        if (arg.name == name) return &arg;
    }
    if (index < term.args.size()) return &term.args[index];
    return nullptr;
}

static bool exprAsArray(const std::shared_ptr<Expr>& expr, std::vector<std::shared_ptr<Expr>>& out) {
    if (auto array = std::dynamic_pointer_cast<ArrayExpr>(expr)) {
        out = array->items;
        return true;
    }
    if (auto term = std::dynamic_pointer_cast<TermExpr>(expr)) {
        if (term->name == "fn:array") {
            out.clear();
            for (const auto& arg : term->args) {
                if (arg.name == "data") {
                    auto data = std::dynamic_pointer_cast<ArrayExpr>(arg.value);
                    if (!data) return false;
                    out = data->items;
                    return true;
                }
            }
        }
    }
    return false;
}

static bool isBuiltinTypeName(const std::string& name) {
    static const std::set<std::string> names = {
        "any", "array", "bool", "boolean", "decimal", "double", "float", "int", "number", "string"
    };
    return names.count(name) > 0;
}

static bool isTypeAnnotationName(const std::string& name) {
    return !name.empty() &&
           (std::isupper(static_cast<unsigned char>(name.front())) || isBuiltinTypeName(name));
}

static bool valueMatchesBuiltinType(const std::shared_ptr<Expr>& value, const std::string& type) {
    const std::string lowered = lowerText(type);
    if (lowered == "any") return true;
    if (lowered == "number" || lowered == "decimal" || lowered == "double" || lowered == "float") {
        return static_cast<bool>(std::dynamic_pointer_cast<NumberExpr>(value));
    }
    if (lowered == "int") {
        auto number = std::dynamic_pointer_cast<NumberExpr>(value);
        return number && std::fabs(number->value - std::round(number->value)) < 1e-12;
    }
    if (lowered == "string") {
        return static_cast<bool>(std::dynamic_pointer_cast<StringExpr>(value));
    }
    if (lowered == "array") {
        std::vector<std::shared_ptr<Expr>> items;
        return exprAsArray(value, items);
    }
    if (lowered == "bool" || lowered == "boolean") {
        auto text = std::dynamic_pointer_cast<StringExpr>(value);
        return text && (text->value == "true" || text->value == "false");
    }
    return false;
}

static double requireNumber(const std::shared_ptr<Expr>& expr, const std::string& fn, const std::string& arg) {
    double number = 0.0;
    if (!argAsNumber(expr, number)) throw InterpreterError(fn + " expects numeric argument '" + arg + "'");
    return number;
}

static std::string requireString(const std::shared_ptr<Expr>& expr, const std::string& fn, const std::string& arg) {
    std::string text;
    if (!argAsString(expr, text)) throw InterpreterError(fn + " expects string argument '" + arg + "'");
    return text;
}

static fs::path sourceRootFromBase(const fs::path& baseDir) {
    fs::path current = fs::absolute(baseDir).lexically_normal();
    while (!current.empty()) {
        if (fs::exists(current / "core") && fs::is_directory(current / "core")) return current;
        fs::path parent = current.parent_path();
        if (parent == current) break;
        current = parent;
    }
    return fs::current_path();
}

static bool isBareModuleImport(const std::string& pattern) {
    if (pattern.empty()) return false;
    fs::path raw(pattern);
    if (raw.is_absolute() || raw.has_parent_path() || raw.has_extension()) return false;
    return pattern.find('*') == std::string::npos;
}

static fs::path resolveCoreImport(const fs::path& baseDir, const std::string& pattern) {
    fs::path root = sourceRootFromBase(baseDir);
    return fs::absolute(root / "core" / (pattern + ".fx")).lexically_normal();
}

static std::vector<std::string> nativeLibraryFileNames(const std::string& moduleName) {
#if defined(_WIN32)
    return {
        moduleName + ".dll",
        "felidae_" + moduleName + ".dll"
    };
#elif defined(__APPLE__)
    return {
        "lib" + moduleName + ".dylib",
        "libfelidae_" + moduleName + ".dylib",
        moduleName + ".dylib"
    };
#else
    return {
        "lib" + moduleName + ".so",
        "libfelidae_" + moduleName + ".so",
        moduleName + ".so"
    };
#endif
}

static bool hasNativeLibraryExtension(const fs::path& path) {
    std::string extension = lowerText(path.extension().string());
#if defined(_WIN32)
    return extension == ".dll";
#elif defined(__APPLE__)
    return extension == ".dylib" || extension == ".so";
#else
    return extension == ".so";
#endif
}

static void* openSharedLibrary(const fs::path& path) {
#if defined(_WIN32)
    HMODULE handle = LoadLibraryW(path.wstring().c_str());
    return reinterpret_cast<void*>(handle);
#else
    return dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

static std::string sharedLibraryError() {
#if defined(_WIN32)
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

static void closeSharedLibrary(void* handle) {
    if (!handle) return;
#if defined(_WIN32)
    FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
}

static void* findSharedLibrarySymbol(void* handle, const char* name) {
    if (!handle) return nullptr;
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(handle), name));
#else
    return dlsym(handle, name);
#endif
}

static std::string nativeModuleNameFromPath(const fs::path& path) {
    std::string stem = path.stem().string();
    const std::string felidaePrefix = "felidae_";
    const std::string libFelidaePrefix = "libfelidae_";
    const std::string libPrefix = "lib";
    if (stem.rfind(libFelidaePrefix, 0) == 0) return stem.substr(libFelidaePrefix.size());
    if (stem.rfind(felidaePrefix, 0) == 0) return stem.substr(felidaePrefix.size());
    if (stem.rfind(libPrefix, 0) == 0) return stem.substr(libPrefix.size());
    return stem;
}

static std::string exprToJson(const std::shared_ptr<Expr>& expr) {
    if (auto s = std::dynamic_pointer_cast<StringExpr>(expr)) return "\"" + jsonEscape(s->value) + "\"";
    if (auto n = std::dynamic_pointer_cast<NumberExpr>(expr)) {
        std::ostringstream out;
        out << std::setprecision(15) << n->value;
        return out.str();
    }
    if (std::dynamic_pointer_cast<NilExpr>(expr)) return "null";
    if (auto a = std::dynamic_pointer_cast<ArrayExpr>(expr)) {
        std::ostringstream out;
        out << "[";
        for (size_t i = 0; i < a->items.size(); ++i) {
            if (i) out << ",";
            out << exprToJson(a->items[i]);
        }
        out << "]";
        return out.str();
    }
    if (auto m = std::dynamic_pointer_cast<MapExpr>(expr)) {
        std::ostringstream out;
        out << "{";
        for (size_t i = 0; i < m->entries.size(); ++i) {
            if (i) out << ",";
            out << "\"" << jsonEscape(m->entries[i].key) << "\":" << exprToJson(m->entries[i].value);
        }
        out << "}";
        return out.str();
    }
    if (auto t = std::dynamic_pointer_cast<TermExpr>(expr)) {
        std::vector<MapEntry> entries;
        entries.push_back(MapEntry{"__term", std::make_shared<StringExpr>(t->name)});
        for (size_t i = 0; i < t->args.size(); ++i) {
            const auto& arg = t->args[i];
            entries.push_back(MapEntry{arg.name.empty() ? ("arg" + std::to_string(i)) : arg.name, arg.value});
        }
        return exprToJson(std::make_shared<MapExpr>(std::move(entries)));
    }
    return "\"" + jsonEscape(expr ? expr->debug() : "") + "\"";
}

static void skipJsonWs(const std::string& text, size_t& pos) {
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) pos++;
}

static bool parseJsonString(const std::string& text, size_t& pos, std::string& out) {
    skipJsonWs(text, pos);
    if (pos >= text.size() || text[pos] != '"') return false;
    pos++;
    out.clear();
    while (pos < text.size() && text[pos] != '"') {
        char c = text[pos++];
        if (c == '\\' && pos < text.size()) {
            char esc = text[pos++];
            switch (esc) {
                case 'n': out.push_back('\n'); break;
                case 't': out.push_back('\t'); break;
                case 'r': out.push_back('\r'); break;
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                default: out.push_back(esc); break;
            }
        } else {
            out.push_back(c);
        }
    }
    if (pos >= text.size() || text[pos] != '"') return false;
    pos++;
    return true;
}

static bool parseJsonValue(const std::string& text, size_t& pos, std::shared_ptr<Expr>& out);

static bool parseJsonObjectValue(const std::string& text, size_t& pos, std::shared_ptr<Expr>& out) {
    skipJsonWs(text, pos);
    if (pos >= text.size() || text[pos] != '{') return false;
    pos++;
    std::vector<MapEntry> entries;
    skipJsonWs(text, pos);
    if (pos < text.size() && text[pos] == '}') {
        pos++;
        out = std::make_shared<MapExpr>(std::move(entries));
        return true;
    }
    while (pos < text.size()) {
        std::string key;
        std::shared_ptr<Expr> value;
        if (!parseJsonString(text, pos, key)) return false;
        skipJsonWs(text, pos);
        if (pos >= text.size() || text[pos] != ':') return false;
        pos++;
        if (!parseJsonValue(text, pos, value)) return false;
        entries.push_back(MapEntry{key, value});
        skipJsonWs(text, pos);
        if (pos < text.size() && text[pos] == ',') {
            pos++;
            continue;
        }
        if (pos < text.size() && text[pos] == '}') {
            pos++;
            out = std::make_shared<MapExpr>(std::move(entries));
            return true;
        }
        return false;
    }
    return false;
}

static bool parseJsonArrayValue(const std::string& text, size_t& pos, std::shared_ptr<Expr>& out) {
    skipJsonWs(text, pos);
    if (pos >= text.size() || text[pos] != '[') return false;
    pos++;
    std::vector<std::shared_ptr<Expr>> items;
    skipJsonWs(text, pos);
    if (pos < text.size() && text[pos] == ']') {
        pos++;
        out = std::make_shared<ArrayExpr>(std::move(items));
        return true;
    }
    while (pos < text.size()) {
        std::shared_ptr<Expr> value;
        if (!parseJsonValue(text, pos, value)) return false;
        items.push_back(value);
        skipJsonWs(text, pos);
        if (pos < text.size() && text[pos] == ',') {
            pos++;
            continue;
        }
        if (pos < text.size() && text[pos] == ']') {
            pos++;
            out = std::make_shared<ArrayExpr>(std::move(items));
            return true;
        }
        return false;
    }
    return false;
}

static bool parseJsonValue(const std::string& text, size_t& pos, std::shared_ptr<Expr>& out) {
    skipJsonWs(text, pos);
    if (pos >= text.size()) return false;
    if (text.compare(pos, 4, "null") == 0) {
        pos += 4;
        out = std::make_shared<NilExpr>();
        return true;
    }
    if (text.compare(pos, 4, "true") == 0) {
        pos += 4;
        out = std::make_shared<StringExpr>("true");
        return true;
    }
    if (text.compare(pos, 5, "false") == 0) {
        pos += 5;
        out = std::make_shared<StringExpr>("false");
        return true;
    }
    if (text[pos] == '{') return parseJsonObjectValue(text, pos, out);
    if (text[pos] == '[') return parseJsonArrayValue(text, pos, out);
    if (text[pos] == '"') {
        std::string value;
        if (!parseJsonString(text, pos, value)) return false;
        out = std::make_shared<StringExpr>(value);
        return true;
    }
    size_t start = pos;
    if (text[pos] == '-') pos++;
    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) pos++;
    if (pos < text.size() && text[pos] == '.') {
        pos++;
        while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) pos++;
    }
    if (start == pos || (start + 1 == pos && text[start] == '-')) return false;
    out = std::make_shared<NumberExpr>(std::stod(text.substr(start, pos - start)));
    return true;
}

static bool parseFlatJsonObject(const std::string& text, std::shared_ptr<Expr>& out) {
    size_t pos = 0;
    if (!parseJsonValue(text, pos, out)) return false;
    skipJsonWs(text, pos);
    return pos == text.size();
}

Interpreter::~Interpreter() {
    joinThreads();
    closeNativeLibraries();
}

void Interpreter::closeNativeLibraries() {
    for (auto& library : nativeLibraries_) closeSharedLibrary(library.handle);
    nativeLibraries_.clear();
    nativeLibraryPaths_.clear();
}

void Interpreter::joinThreads() {
    std::vector<std::shared_ptr<ThreadTask>> tasks;
    {
        std::lock_guard<std::mutex> lock(threadMutex_);
        tasks.reserve(threadTasks_.size());
        for (const auto& entry : threadTasks_) tasks.push_back(entry.second);
    }
    for (const auto& task : tasks) {
        if (task && task->worker.joinable()) task->worker.join();
    }
}

std::shared_ptr<Expr> Interpreter::makeThreadHandle(const std::string& id) const {
    return std::make_shared<MapExpr>(std::vector<MapEntry>{
        {"__type", std::make_shared<StringExpr>("Thread")},
        {"id", std::make_shared<StringExpr>(id)}
    });
}

std::shared_ptr<Interpreter::ThreadTask> Interpreter::threadTaskFromHandle(const std::shared_ptr<Expr>& handle) {
    auto typeValue = findMapValue(handle, "__type");
    auto idValue = findMapValue(handle, "id");
    auto type = std::dynamic_pointer_cast<StringExpr>(typeValue);
    auto id = std::dynamic_pointer_cast<StringExpr>(idValue);
    if (!type || type->value != "Thread" || !id || id->value.empty()) {
        throw InterpreterError("thread API expects a valid thread handle");
    }
    std::lock_guard<std::mutex> lock(threadMutex_);
    auto it = threadTasks_.find(id->value);
    if (it == threadTasks_.end()) throw InterpreterError("Unknown thread handle: " + id->value);
    return it->second;
}

std::string Interpreter::createThreadTask(const std::string& functionName) {
    if (functionName.empty()) throw InterpreterError("thread.createThread expects non-empty function name");
    if (!hasMethod(functionName)) {
        throw InterpreterError("Thread function '" + functionName + "' not found");
    }
    std::lock_guard<std::mutex> lock(threadMutex_);
    std::string id = "thread-" + std::to_string(++threadCounter_);
    threadTasks_[id] = std::make_shared<ThreadTask>(functionName);
    return id;
}

std::string Interpreter::startThreadTask(const std::shared_ptr<Expr>& handle) {
    auto task = threadTaskFromHandle(handle);
    {
        std::lock_guard<std::mutex> lock(threadMutex_);
        if (task->started) return task->status;
        task->started = true;
        task->status = "running";
    }

    auto clausesSnapshot = clauses_;
    auto memorySnapshot = memory_;
    auto globalsSnapshot = globals_;
    auto lazySnapshot = lazyModules_;
    auto loadedFilesSnapshot = loadedFiles_;
    auto currentLoadingFileSnapshot = currentLoadingFile_;
    auto functionName = task->functionName;

    task->worker = std::thread([this,
                                task,
                                functionName,
                                clausesSnapshot = std::move(clausesSnapshot),
                                memorySnapshot = std::move(memorySnapshot),
                                globalsSnapshot = std::move(globalsSnapshot),
                                lazySnapshot = std::move(lazySnapshot),
                                loadedFilesSnapshot = std::move(loadedFilesSnapshot),
                                currentLoadingFileSnapshot = std::move(currentLoadingFileSnapshot)]() mutable {
        try {
            Interpreter child;
            child.clauses_ = std::move(clausesSnapshot);
            child.memory_ = std::move(memorySnapshot);
            child.globals_ = std::move(globalsSnapshot);
            child.lazyModules_ = std::move(lazySnapshot);
            child.loadedFiles_ = std::move(loadedFilesSnapshot);
            child.currentLoadingFile_ = std::move(currentLoadingFileSnapshot);

            if (!child.hasMethod(functionName)) {
                throw InterpreterError("Thread function '" + functionName + "' not found");
            }

            Call call(functionName, {});
            auto it = child.clauses_.find(functionName);
            std::vector<Solution> solutions;
            if (it != child.clauses_.end()) {
                for (const auto& clause : it->second) {
                    if (!child.isMethodClause(*clause)) continue;
                    child.solveMethodCall(call, clause, Env{}, solutions, 1, 0);
                    if (!solutions.empty()) break;
                }
            }

            std::string result = "false";
            if (!solutions.empty()) {
                auto returned = solutions.front().env.find("__return");
                result = returned == solutions.front().env.end()
                    ? "true"
                    : child.valueToString(returned->second);
            }

            std::lock_guard<std::mutex> lock(threadMutex_);
            task->result = result;
            task->status = "finished";
        } catch (const std::exception& ex) {
            std::lock_guard<std::mutex> lock(threadMutex_);
            task->error = ex.what();
            task->status = "error";
        }
    });

    return "started";
}

std::string Interpreter::threadTaskStatus(const std::shared_ptr<Expr>& handle) {
    auto task = threadTaskFromHandle(handle);
    std::lock_guard<std::mutex> lock(threadMutex_);
    return task->status;
}

std::shared_ptr<Expr> Interpreter::threadTaskResult(const std::shared_ptr<Expr>& handle) {
    auto task = threadTaskFromHandle(handle);
    if (task->worker.joinable()) task->worker.join();
    std::lock_guard<std::mutex> lock(threadMutex_);
    if (!task->error.empty()) throw InterpreterError("Thread failed: " + task->error);
    if (!task->started) throw InterpreterError("Thread has not been started");
    return std::make_shared<StringExpr>(task->result);
}

void Interpreter::loadNativeLibrary(const std::filesystem::path& file) {
    fs::path normalized = fs::absolute(file).lexically_normal();
    if (nativeLibraryPaths_.count(normalized)) return;
    void* handle = openSharedLibrary(normalized);
    if (!handle) {
        throw InterpreterError("Cannot load native module library '" + normalized.string() + "': " + sharedLibraryError());
    }
    auto call = reinterpret_cast<NativeCallFn>(findSharedSymbol(handle, "felidae_native_call"));
    auto free = reinterpret_cast<NativeFreeFn>(findSharedSymbol(handle, "felidae_native_free"));
    if (!call || !free) {
        closeSharedLibrary(handle);
        throw InterpreterError("Native module library '" + normalized.string() +
                               "' must export felidae_native_call and felidae_native_free");
    }
    nativeLibraries_.push_back(NativeLibrary{normalized, nativeModuleNameFromPath(normalized), handle, call, free});
    nativeLibraryPaths_.insert(normalized);
}

void Interpreter::addProgram(const Program& program) {
    for (const auto& stmt : program.statements) {
        if (auto clause = std::dynamic_pointer_cast<ClauseStmt>(stmt)) {
            addClause(clause);
        } else if (auto binding = std::dynamic_pointer_cast<GlobalBindingStmt>(stmt)) {
            if (globals_.count(binding->name) || clauses_.count(binding->name)) {
                throw InterpreterError("Global '" + binding->name + "' is already defined and immutable");
            }
            Env env;
            std::shared_ptr<Expr> value;
            if (!evalExprValue(binding->expr, env, value)) {
                throw InterpreterError("Cannot evaluate global binding '" + binding->name + "'");
            }
            globals_[binding->name] = value->clone();
            Call head(binding->name, std::vector<Arg>{{"value", value->clone()}});
            addClause(std::make_shared<ClauseStmt>(std::move(head), std::vector<std::shared_ptr<Goal>>{}));
        }
    }
}

void Interpreter::addClause(std::shared_ptr<ClauseStmt> clause) {
    invalidateCaches();
    if (clause->isFact()) {
        auto factMap = factToMap(*clause, clause->parentName);
        Call mergedHead(clause->head.name, {});
        for (const auto& entry : factMap->entries) {
            if (entry.key != "__type" && entry.key != "__parent") {
                std::vector<std::shared_ptr<Expr>> items;
                if (exprAsArrayItems(entry.value, items)) {
                    for (const auto& item : items) mergedHead.args.push_back(Arg{entry.key, item->clone()});
                } else {
                    mergedHead.args.push_back(Arg{entry.key, entry.value->clone()});
                }
            }
        }
        clause = std::make_shared<ClauseStmt>(std::move(mergedHead), clause->parentName, std::vector<std::shared_ptr<Goal>>{});
        memory_.addFact(clause->head.name, clause->parentName, factMap);
        if (!clause->parentName.empty() && !memory_.parents().count(clause->head.name)) {
            memory_.setParent(clause->head.name, clause->parentName);
        }
    }
    if (!currentLoadingFile_.empty()) {
        clauseOrigins_[clause.get()] = currentLoadingFile_;
    }
    clauses_[clause->head.name].push_back(std::move(clause));
}

void Interpreter::addLazyImport(const std::filesystem::path& baseDir, const std::string& pattern) {
    invalidateCaches();
    LazyModule module;
    module.baseDir = baseDir;
    module.pattern = pattern;

    auto files = expandImportPattern(baseDir, pattern);
    module.files = std::move(files);
    module.nativeLibrary = resolveNativeImport(baseDir, pattern);
    lazyModules_.push_back(std::move(module));
}

std::vector<Solution> Interpreter::solve(const std::vector<std::shared_ptr<Goal>>& queryGoals,
                                         size_t maxSolutions) {
    ++solveEpoch_;
    const std::string cacheKey = solveCacheKey(queryGoals, maxSolutions);
    auto cached = solveCache_.find(cacheKey);
    if (cached != solveCache_.end()) return cached->second;

    std::vector<Solution> out;
    Env env;
    solveRecursive(queryGoals, std::move(env), out, maxSolutions, 0);
    evictColdModules();
    solveCache_[cacheKey] = out;
    return out;
}

bool Interpreter::hasMethod(const std::string& name) {
    auto it = clauses_.find(name);
    if (it == clauses_.end() && ensurePredicateLoaded(name)) {
        it = clauses_.find(name);
    }
    if (it == clauses_.end()) return false;
    for (const auto& clause : it->second) {
        if (isMethodClause(*clause)) return true;
    }
    return false;
}

bool Interpreter::hasGlobal(const std::string& name) const {
    return globals_.count(name) > 0;
}

std::shared_ptr<Expr> Interpreter::evaluateGlobal(const std::string& name) const {
    auto it = globals_.find(name);
    if (it == globals_.end()) throw InterpreterError("Unknown global '" + name + "'");
    return it->second->clone();
}

std::shared_ptr<Expr> Interpreter::evaluateExpressionText(const std::string& text) {
    Lexer lexer(text);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpressionText();
    std::shared_ptr<Expr> value;
    Env env;
    if (!evalExprValue(expr, env, value)) {
        throw InterpreterError("Expression did not evaluate to a value");
    }
    return value;
}

std::shared_ptr<Expr> Interpreter::callMain(const std::shared_ptr<Expr>& systemInput) {
    if (!hasMethod("main")) throw InterpreterError("No main() method found");
    std::vector<Solution> out;
    std::shared_ptr<ClauseStmt> mainClause;
    for (const auto& clause : clauses_.at("main")) {
        if (isMethodClause(*clause)) {
            mainClause = clause;
            break;
        }
    }
    if (!mainClause) throw InterpreterError("No main() method found");
    Call call("main", {});
    if (!mainClause->head.args.empty()) {
        call.args.push_back(Arg{"arguments", systemInput->clone()});
    }
    const bool previousStrictValueFailures = strictValueFailures_;
    strictValueFailures_ = true;
    try {
        solveMethodCall(call, mainClause, Env{}, out, 1, 0);
    } catch (...) {
        strictValueFailures_ = previousStrictValueFailures;
        throw;
    }
    strictValueFailures_ = previousStrictValueFailures;
    if (out.empty()) {
        throw InterpreterError("main() produced no result. A goal in main failed; add an explicit return or check method calls used as values.");
    }
    auto it = out.front().env.find("__return");
    if (it == out.front().env.end()) {
        throw InterpreterError("main() completed without a return value");
    }
    return it->second->clone();
}

std::string Interpreter::valueToString(const std::shared_ptr<Expr>& value) const {
    Env env;
    return exprToString(value, env);
}

std::string Interpreter::visualizeDataJson(bool loadImports) {
    if (loadImports) loadAllImports();
    return runtimeGraphJson();
}

std::string Interpreter::visualizeDataHtml(bool loadImports) {
    if (loadImports) loadAllImports();
    return standaloneDataVisualizationHtml(runtimeGraphJson());
}

void Interpreter::solveRecursive(const std::vector<std::shared_ptr<Goal>>& goals,
                                 Env env,
                                 std::vector<Solution>& out,
                                 size_t maxSolutions,
                                 size_t depth) {
    if (out.size() >= maxSolutions) return;
    if (depth > 10000) throw InterpreterError("Maximum recursion depth reached");

    if (goals.empty()) {
        out.push_back(Solution{std::move(env)});
        return;
    }

    auto first = goals.front();
    std::vector<std::shared_ptr<Goal>> rest(goals.begin() + 1, goals.end());

    if (auto groupGoal = std::dynamic_pointer_cast<GroupGoal>(first)) {
        std::vector<std::shared_ptr<Goal>> combined;
        combined.reserve(groupGoal->goals.size() + rest.size());
        for (const auto& g : groupGoal->goals) combined.push_back(g);
        for (const auto& g : rest) combined.push_back(g);
        solveRecursive(combined, std::move(env), out, maxSolutions, depth + 1);
        return;
    }

    if (auto orGoal = std::dynamic_pointer_cast<OrGoal>(first)) {
        for (const auto& branch : orGoal->branches) {
            std::vector<std::shared_ptr<Goal>> combined;
            combined.reserve(branch.size() + rest.size());
            for (const auto& g : branch) combined.push_back(g);
            for (const auto& g : rest) combined.push_back(g);
            solveRecursive(combined, env, out, maxSolutions, depth + 1);
            if (out.size() >= maxSolutions) return;
        }
        return;
    }

    if (auto assign = std::dynamic_pointer_cast<AssignGoal>(first)) {
        Env nextEnv = env;
        if (solveAssignGoal(*assign, nextEnv)) {
            solveRecursive(rest, std::move(nextEnv), out, maxSolutions, depth + 1);
        }
        return;
    }

    if (auto multiAssign = std::dynamic_pointer_cast<MultiAssignGoal>(first)) {
        Env nextEnv = env;
        if (solveMultiAssignGoal(*multiAssign, nextEnv)) {
            solveRecursive(rest, std::move(nextEnv), out, maxSolutions, depth + 1);
        }
        return;
    }

    if (auto bin = std::dynamic_pointer_cast<BinaryGoal>(first)) {
        Env nextEnv = env;
        if (solveBinaryGoal(*bin, nextEnv)) {
            solveRecursive(rest, std::move(nextEnv), out, maxSolutions, depth + 1);
        }
        return;
    }

    if (auto where = std::dynamic_pointer_cast<WhereGoal>(first)) {
        Env nextEnv = env;
        if (solveWhereGoal(*where, nextEnv)) {
            solveRecursive(rest, std::move(nextEnv), out, maxSolutions, depth + 1);
        }
        return;
    }

    if (auto ret = std::dynamic_pointer_cast<ReturnGoal>(first)) {
        Env nextEnv = env;
        if (solveReturnGoal(*ret, nextEnv)) {
            solveRecursive(rest, std::move(nextEnv), out, maxSolutions, depth + 1);
        }
        return;
    }

    auto callGoal = std::dynamic_pointer_cast<CallGoal>(first);
    if (!callGoal) throw InterpreterError("Unknown goal node");

    auto it = clauses_.find(callGoal->call.name);
    if (it == clauses_.end() && ensurePredicateLoaded(callGoal->call.name)) {
        it = clauses_.find(callGoal->call.name);
    }
    const bool preferLocalClause = it != clauses_.end() && callGoal->call.name.find(':') == std::string::npos;
    if (!preferLocalClause) {
        Env builtinEnv = env;
        if (solveBuiltin(callGoal->call, builtinEnv)) {
            solveRecursive(rest, std::move(builtinEnv), out, maxSolutions, depth + 1);
            if (out.size() >= maxSolutions) return;
        }
    }
    if (it == clauses_.end()) return;
    touchClauses(it->second);

    for (const auto& originalClause : it->second) {
        if (isMethodClause(*originalClause)) {
            std::vector<Solution> methodSolutions;
            solveMethodCall(callGoal->call, originalClause, env, methodSolutions, maxSolutions, depth + 1);
            for (auto& solution : methodSolutions) {
                solveRecursive(rest, std::move(solution.env), out, maxSolutions, depth + 1);
                if (out.size() >= maxSolutions) return;
            }
            if (out.size() >= maxSolutions) return;
            continue;
        }
        auto clause = standardizeApart(*originalClause);
        for (auto& nextEnv : unifyCallAlternatives(callGoal->call, clause->head, env)) {
            std::vector<std::shared_ptr<Goal>> combined;
            combined.reserve(clause->body.size() + rest.size());
            for (const auto& g : clause->body) combined.push_back(g);
            for (const auto& g : rest) combined.push_back(g);

            solveRecursive(combined, std::move(nextEnv), out, maxSolutions, depth + 1);
            if (out.size() >= maxSolutions) return;
        }
    }

    for (size_t factIndex : memory_.compatibleFactIndexes(callGoal->call.name)) {
        const auto& fact = memory_.fact(factIndex);
        if (fact.type == callGoal->call.name) continue;
        Call factHead(fact.type, {});
        for (const auto& entry : fact.value->entries) {
            if (entry.key != "__type" && entry.key != "__parent") {
                std::vector<std::shared_ptr<Expr>> items;
                if (exprAsArrayItems(entry.value, items)) {
                    for (const auto& item : items) factHead.args.push_back(Arg{entry.key, item->clone()});
                } else {
                    factHead.args.push_back(Arg{entry.key, entry.value->clone()});
                }
            }
        }
        factHead.name = callGoal->call.name;
        for (auto& nextEnv : unifyCallAlternatives(callGoal->call, factHead, env)) {
            solveRecursive(rest, std::move(nextEnv), out, maxSolutions, depth + 1);
            if (out.size() >= maxSolutions) return;
        }
    }
}

bool Interpreter::solveAssignGoal(const AssignGoal& goal, Env& env) {
    auto var = std::make_shared<VarExpr>(goal.name);
    if (globals_.count(goal.name)) {
        throw InterpreterError("Variable '" + goal.name + "' is already assigned and immutable");
    }
    auto existing = env.find(goal.name);
    if (existing != env.end() && !std::dynamic_pointer_cast<NilExpr>(resolveExpr(existing->second, env))) {
        throw InterpreterError("Variable '" + goal.name + "' is already assigned and immutable");
    }

    if (goal.expr) {
        std::shared_ptr<Expr> value;
        if (!evalExprValue(goal.expr, env, value)) {
            if (strictValueFailures_) {
                throw InterpreterError("Assignment '" + goal.name + " := ...' did not produce a value");
            }
            return false;
        }
        return unifyExpr(var, value, env);
    }

    if (auto callGoal = std::dynamic_pointer_cast<CallGoal>(goal.goal)) {
        Call builtinCall = callGoal->call;
        builtinCall.args.push_back(Arg{"out", var});

        Env nextEnv = env;
        if (solveBuiltin(builtinCall, nextEnv)) {
            env = std::move(nextEnv);
            return true;
        }

        const Call& call = builtinCall;
        auto it = clauses_.find(call.name);
        if (it == clauses_.end() && ensurePredicateLoaded(call.name)) {
            it = clauses_.find(call.name);
        }
        if (it != clauses_.end()) {
            touchClauses(it->second);
            for (const auto& originalClause : it->second) {
                std::vector<Solution> methodSolutions;
                const bool previousValueCallMode = valueCallMode_;
                valueCallMode_ = true;
                try {
                    solveMethodCall(call, originalClause, env, methodSolutions, 1, 0);
                } catch (...) {
                    valueCallMode_ = previousValueCallMode;
                    throw;
                }
                valueCallMode_ = previousValueCallMode;
                if (!methodSolutions.empty()) {
                    Env attempt = std::move(methodSolutions.front().env);
                    auto returned = attempt.find("__return");
                    auto assigned = attempt.find(goal.name);
                    std::shared_ptr<Expr> value = assigned != attempt.end()
                        ? assigned->second
                        : (returned == attempt.end()
                            ? evaluateGoalTruthTuple(originalClause->body, env)
                            : returned->second);
                    if (unifyExpr(var, value, attempt)) {
                        env = std::move(attempt);
                        return true;
                    }
                }
            }
        }

        if (strictValueFailures_) {
            throw InterpreterError("Assignment '" + goal.name + " := ...' did not produce a value");
        }
        return false;
    }

    if (auto binary = std::dynamic_pointer_cast<BinaryGoal>(goal.goal)) {
        Env nextEnv = env;
        if (solveBinaryGoal(*binary, nextEnv) &&
            unifyExpr(var, std::make_shared<StringExpr>("ok"), nextEnv)) {
            env = std::move(nextEnv);
            return true;
        }
    }
    return false;
}

bool Interpreter::solveMultiAssignGoal(const MultiAssignGoal& goal, Env& env) {
    std::shared_ptr<Expr> value;
    if (!evalExprValue(goal.expr, env, value)) {
        if (strictValueFailures_) {
            throw InterpreterError("Assignment '" + goal.debug() + "' did not produce a value");
        }
        return false;
    }

    std::vector<std::shared_ptr<Expr>> items;
    if (auto tuple = std::dynamic_pointer_cast<TermExpr>(value)) {
        if (tuple->name == "fn:tuple") {
            for (const auto& arg : tuple->args) items.push_back(cloneExprOrNil(arg.value));
        }
    }
    if (items.empty()) exprAsArrayItems(value, items);
    if (items.size() != goal.targets.size()) {
        throw InterpreterError(
            "ProgrammingError: tuple assignment expected " +
            std::to_string(goal.targets.size()) + " value(s), got " + std::to_string(items.size()));
    }

    Env attempt = env;
    for (size_t i = 0; i < goal.targets.size(); ++i) {
        const auto& target = goal.targets[i];
        if (globals_.count(target.name)) {
            throw InterpreterError("Variable '" + target.name + "' is already assigned and immutable");
        }
        auto existing = attempt.find(target.name);
        if (existing != attempt.end() && !std::dynamic_pointer_cast<NilExpr>(resolveExpr(existing->second, attempt))) {
            throw InterpreterError("Variable '" + target.name + "' is already assigned and immutable");
        }
        if (!target.type.empty() && !valueMatchesBuiltinType(items[i], target.type)) {
            throw InterpreterError(
                "ProgrammingError: tuple assignment target '" + target.name +
                "' expects " + target.type + ", got " + exprToString(items[i], attempt));
        }
        if (!unifyExpr(std::make_shared<VarExpr>(target.name), items[i], attempt)) return false;
    }
    env = std::move(attempt);
    return true;
}

bool Interpreter::solveBinaryGoal(const BinaryGoal& goal, Env& env) {
    if (goal.op == "==") {
        std::shared_ptr<Expr> leftValue;
        std::shared_ptr<Expr> rightValue;
        if (evalExprValue(goal.left, env, leftValue) && evalExprValue(goal.right, env, rightValue)) {
            return unifyExpr(leftValue, rightValue, env);
        }
        return unifyExpr(goal.left, goal.right, env);
    }

    if (goal.op == "!=") {
        Env copy = env;
        std::shared_ptr<Expr> leftValue;
        std::shared_ptr<Expr> rightValue;
        if (evalExprValue(goal.left, copy, leftValue) && evalExprValue(goal.right, copy, rightValue)) {
            return !unifyExpr(leftValue, rightValue, copy);
        }
        return !unifyExpr(goal.left, goal.right, copy);
    }

    auto left = resolveExpr(goal.left, env);
    auto right = resolveExpr(goal.right, env);
    if (!isGroundLiteral(left) || !isGroundLiteral(right)) return false;
    return compareResolved(left, goal.op, right);
}

bool Interpreter::solveWhereGoal(const WhereGoal& goal, Env& env) {
    if (auto binary = std::dynamic_pointer_cast<BinaryGoal>(goal.condition)) {
        return solveBinaryGoal(*binary, env);
    }
    return false;
}

bool Interpreter::solveReturnGoal(const ReturnGoal& goal, Env& env) {
    if (goal.fields.size() == 1 && goal.fields.front().name.empty()) {
        std::shared_ptr<Expr> value;
        if (!evalExprValue(goal.fields.front().value, env, value)) {
            if (strictValueFailures_) throw InterpreterError("return value did not evaluate");
            return false;
        }
        env["__return"] = value;
        return true;
    }

    std::vector<MapEntry> entries;
    entries.reserve(goal.fields.size());
    for (const auto& field : goal.fields) {
        std::shared_ptr<Expr> value;
        if (!evalExprValue(field.value, env, value)) {
            if (strictValueFailures_) {
                throw InterpreterError("return field '" + field.name + "' did not evaluate");
            }
            return false;
        }
        entries.push_back(MapEntry{field.name, value});
    }
    env["__return"] = std::make_shared<MapExpr>(std::move(entries));
    return true;
}

bool Interpreter::bodyHasReturnGoal(const std::vector<std::shared_ptr<Goal>>& goals) const {
    for (const auto& goal : goals) {
        if (std::dynamic_pointer_cast<ReturnGoal>(goal)) return true;
        if (auto group = std::dynamic_pointer_cast<GroupGoal>(goal)) {
            if (bodyHasReturnGoal(group->goals)) return true;
        }
        if (auto orGoal = std::dynamic_pointer_cast<OrGoal>(goal)) {
            for (const auto& branch : orGoal->branches) {
                if (bodyHasReturnGoal(branch)) return true;
            }
        }
    }
    return false;
}

bool Interpreter::evaluateGoalTruth(const std::shared_ptr<Goal>& goal, Env& env) {
    std::vector<Solution> solutions;
    const bool previousValueCallMode = valueCallMode_;
    valueCallMode_ = false;
    try {
        solveRecursive({goal}, env, solutions, 1, 0);
    } catch (...) {
        valueCallMode_ = previousValueCallMode;
        throw;
    }
    valueCallMode_ = previousValueCallMode;
    if (solutions.empty()) return false;
    env = std::move(solutions.front().env);
    return true;
}

std::shared_ptr<Expr> Interpreter::evaluateGoalTruthTuple(const std::vector<std::shared_ptr<Goal>>& goals, Env env) {
    std::vector<Arg> values;
    values.reserve(goals.size());
    for (const auto& goal : goals) {
        if (std::dynamic_pointer_cast<ReturnGoal>(goal)) continue;
        const bool ok = evaluateGoalTruth(goal, env);
        values.push_back(Arg{"value", std::make_shared<StringExpr>(ok ? "true" : "false")});
    }
    if (values.empty()) {
        values.push_back(Arg{"value", std::make_shared<StringExpr>("true")});
    }
    return std::make_shared<TermExpr>("fn:tuple", std::move(values));
}

std::shared_ptr<Expr> Interpreter::executeGoalTruthTuple(const std::vector<std::shared_ptr<Goal>>& goals, Env env, Env& outEnv) {
    std::vector<Arg> values;
    values.reserve(goals.size());
    for (const auto& goal : goals) {
        if (std::dynamic_pointer_cast<ReturnGoal>(goal)) continue;
        const bool ok = evaluateGoalTruth(goal, env);
        values.push_back(Arg{"value", std::make_shared<StringExpr>(ok ? "true" : "false")});
    }
    if (values.empty()) {
        values.push_back(Arg{"value", std::make_shared<StringExpr>("true")});
    }
    outEnv = std::move(env);
    return std::make_shared<TermExpr>("fn:tuple", std::move(values));
}

bool Interpreter::solveMethodCall(const Call& call,
                                  const std::shared_ptr<ClauseStmt>& originalClause,
                                  Env env,
                                  std::vector<Solution>& out,
                                  size_t maxSolutions,
                                  size_t depth) {
    if (originalClause->emptyDeclaration) return false;
    Env callerEnv = std::move(env);
    std::vector<Env> candidates{Env{}};

    for (size_t paramIndex = 0; paramIndex < originalClause->head.args.size(); ++paramIndex) {
        const auto& param = originalClause->head.args[paramIndex];
        auto typeExpr = std::dynamic_pointer_cast<VarExpr>(param.value);
        bool typedParam = typeExpr && isTypeAnnotationName(typeExpr->name);
        std::string localName = param.name;
        if (typeExpr && !typedParam && !typeExpr->name.empty() && typeExpr->name.rfind("__anon", 0) != 0) {
            localName = typeExpr->name;
        }
        std::vector<Env> nextCandidates;
        const Arg* callArg = findArg(call, param, paramIndex);
        for (const auto& candidate : candidates) {
            if (!callArg) {
                nextCandidates.push_back(candidate);
                continue;
            }
            Env attempt = candidate;
            auto unresolvedCallVar = std::dynamic_pointer_cast<VarExpr>(resolveExpr(callArg->value, callerEnv));
            if (unresolvedCallVar) {
                if (unresolvedCallVar->name == localName ||
                    unifyExpr(std::make_shared<VarExpr>(localName), callArg->value, attempt)) {
                    nextCandidates.push_back(std::move(attempt));
                }
                continue;
            }
            std::shared_ptr<Expr> value;
            if (!evalExprValue(callArg->value, callerEnv, value)) continue;
            if (!typedParam) {
                if (unifyExpr(std::make_shared<VarExpr>(localName), value, attempt)) {
                    nextCandidates.push_back(std::move(attempt));
                }
                continue;
            }
            if (isBuiltinTypeName(typeExpr->name)) {
                if (!valueMatchesBuiltinType(value, typeExpr->name)) continue;
                if (unifyExpr(std::make_shared<VarExpr>(localName), value, attempt)) {
                    nextCandidates.push_back(std::move(attempt));
                }
                continue;
            }
            auto map = std::dynamic_pointer_cast<MapExpr>(value);
            std::string actualType;
            if (map) {
                auto typeValue = findMapValue(value, "__type");
                auto typeString = std::dynamic_pointer_cast<StringExpr>(typeValue);
                if (typeString) actualType = typeString->value;
            }
            if (actualType.empty() || !memory_.isCompatibleType(actualType, typeExpr->name)) continue;
            if (unifyExpr(std::make_shared<VarExpr>(localName), value, attempt)) {
                nextCandidates.push_back(std::move(attempt));
            }
        }
        candidates = std::move(nextCandidates);
        if (candidates.empty()) return false;
    }

    auto appendReturnedSolution = [&](const Env& solutionEnv, const std::shared_ptr<Expr>& returned) -> bool {
        Env attempt = callerEnv;
        attempt["__return"] = returned;
        auto localNameForParam = [](const Arg& param) {
            auto typeExpr = std::dynamic_pointer_cast<VarExpr>(param.value);
            bool typedParam = typeExpr && isTypeAnnotationName(typeExpr->name);
            std::string localName = param.name;
            if (typeExpr && !typedParam && !typeExpr->name.empty() && typeExpr->name.rfind("__anon", 0) != 0) {
                localName = typeExpr->name;
            }
            return localName;
        };
        auto resolvedBinding = [&](const std::string& name) -> std::shared_ptr<Expr> {
            auto bound = solutionEnv.find(name);
            if (bound == solutionEnv.end()) return nullptr;
            return resolveExpr(bound->second, solutionEnv);
        };
        bool ok = true;
        size_t positionalOutputIndex = 0;
        for (size_t argIndex = 0; argIndex < call.args.size(); ++argIndex) {
            const auto& arg = call.args[argIndex];
            bool isInputArg = false;
            for (size_t paramIndex = 0; paramIndex < originalClause->head.args.size(); ++paramIndex) {
                const auto& param = originalClause->head.args[paramIndex];
                if ((!arg.name.empty() && param.name == arg.name) ||
                    (arg.name.empty() && argIndex == paramIndex)) {
                    auto unresolved = std::dynamic_pointer_cast<VarExpr>(resolveExpr(arg.value, callerEnv));
                    isInputArg = !unresolved;
                    break;
                }
            }
            if (isInputArg) continue;
            std::shared_ptr<Expr> returnedField;
            if (!arg.name.empty()) {
                returnedField = findMapValue(returned, arg.name);
                if (!returnedField) returnedField = findMapValue(returned, "value");
                if (!returnedField) returnedField = findMapValue(returned, "output");
                if (!returnedField) returnedField = findMapValue(returned, "");
                if (!returnedField) {
                    for (const auto& param : originalClause->head.args) {
                        if (param.name != arg.name) continue;
                        returnedField = resolvedBinding(localNameForParam(param));
                        break;
                    }
                }
            } else if (auto returnedMap = std::dynamic_pointer_cast<MapExpr>(returned)) {
                if (positionalOutputIndex < returnedMap->entries.size()) {
                    returnedField = returnedMap->entries[positionalOutputIndex].value;
                }
                positionalOutputIndex++;
                if (!returnedField && argIndex < originalClause->head.args.size()) {
                    returnedField = resolvedBinding(localNameForParam(originalClause->head.args[argIndex]));
                }
            } else {
                if (argIndex < originalClause->head.args.size()) {
                    returnedField = resolvedBinding(localNameForParam(originalClause->head.args[argIndex]));
                }
            }
            if (!returnedField || !unifyExpr(arg.value, returnedField, attempt)) {
                ok = false;
                break;
            }
        }
        if (!ok) return false;
        out.push_back(Solution{std::move(attempt)});
        return out.size() >= maxSolutions;
    };

    if (!originalClause->fallbackBranches.empty()) {
        for (auto& candidate : candidates) {
            std::vector<Solution> preludeSolutions;
            if (originalClause->body.empty()) {
                preludeSolutions.push_back(Solution{std::move(candidate)});
            } else {
                solveRecursive(originalClause->body, std::move(candidate), preludeSolutions, 1, depth + 1);
            }
            for (auto& prelude : preludeSolutions) {
                for (const auto& branch : originalClause->fallbackBranches) {
                    std::vector<Solution> branchSolutions;
                    solveRecursive(branch, prelude.env, branchSolutions, 1, depth + 1);
                    bool branchReturned = false;
                    for (auto& solution : branchSolutions) {
                        auto returnedIt = solution.env.find("__return");
                        if (returnedIt == solution.env.end()) continue;
                        branchReturned = true;
                        auto returnedValue = returnedIt->second;
                        if (appendReturnedSolution(solution.env, returnedValue)) return true;
                        break;
                    }
                    if (branchReturned) break;
                }
            }
        }
        return true;
    }

    for (auto& candidate : candidates) {
        if (valueCallMode_ && originalClause->head.name != "main" && !bodyHasReturnGoal(originalClause->body)) {
            Env truthEnv;
            auto truthTuple = executeGoalTruthTuple(originalClause->body, candidate, truthEnv);
            truthEnv["__return"] = truthTuple;
            if (appendReturnedSolution(truthEnv, truthTuple)) return true;
            continue;
        }

        std::vector<Solution> nested;
        Env startingCandidate = candidate;
        solveRecursive(originalClause->body, std::move(candidate), nested, 1, depth + 1);
        if (nested.empty() && valueCallMode_ && originalClause->head.name != "main") {
            Env failedValueEnv = startingCandidate;
            failedValueEnv["__return"] = evaluateGoalTruthTuple(originalClause->body, startingCandidate);
            nested.push_back(Solution{std::move(failedValueEnv)});
        }
        for (auto& solution : nested) {
            auto returnedIt = solution.env.find("__return");
            if (returnedIt == solution.env.end()) {
                if (originalClause->head.name == "main") {
                    solution.env["__return"] = std::make_shared<MapExpr>(std::vector<MapEntry>{});
                } else {
                    solution.env["__return"] = evaluateGoalTruthTuple(originalClause->body, startingCandidate);
                }
                returnedIt = solution.env.find("__return");
            }
            auto returnedValue = returnedIt->second;
            if (appendReturnedSolution(solution.env, returnedValue)) return true;
        }
    }
    return true;
}

bool Interpreter::solveBuiltin(const Call& call, Env& env) {
    if (!nativeDeclarationFor(call.name)) {
        ensurePredicateLoaded(call.name);
    }
    Env nativeEnv = env;
    if (solveNativeCall(call, nativeEnv)) {
        env = std::move(nativeEnv);
        return true;
    }

    auto outArg = [&](const std::string& name, size_t index) -> const Arg* {
        return findArgByNameOrIndex(call, name, index);
    };
    auto namedArg = [&](const std::string& name) -> const Arg* {
        for (const auto& arg : call.args) {
            if (arg.name == name) return &arg;
        }
        return nullptr;
    };
    auto valueArg = [&](std::initializer_list<const char*> names, size_t index, std::shared_ptr<Expr>& out) -> bool {
        const Arg* arg = nullptr;
        for (const char* name : names) {
            arg = namedArg(name);
            if (arg) break;
        }
        if (!arg && index < call.args.size() && call.args[index].name.empty()) arg = &call.args[index];
        return arg && evalExprValue(arg->value, env, out);
    };

    std::shared_ptr<Expr> a;
    std::shared_ptr<Expr> b;
    std::shared_ptr<Expr> c;
    std::shared_ptr<Expr> result;

    auto callNativeValueBuiltin = [&]() -> bool {
        TermExpr term(call.name, {});
        for (const auto& arg : call.args) {
            if (arg.name == "result" || arg.name == "out" || arg.name == "access") {
                continue;
            }
            term.args.push_back(Arg{arg.name, arg.value});
        }
        std::shared_ptr<Expr> value;
        if (!evalCallAsValue(term, env, value)) return false;
        const Arg* out = namedArg("result");
        if (!out) out = namedArg("out");
        if (!out) out = namedArg("access");
        if (!out) out = namedArg("equals");
        if (!out) return true;
        return unifyExpr(out->value, value, env);
    };

    if (isBuiltinFunctionName(call.name) &&
        call.name != "throw" &&
        !(call.name == "math:add" || call.name == "math:sub" ||
          call.name == "math:mul" || call.name == "math:div" ||
          call.name == "math:mod") &&
        call.name.rfind("str:", 0) != 0 &&
        call.name.rfind("array:", 0) != 0 &&
        call.name.rfind("pair:", 0) != 0 &&
        call.name.rfind("json:", 0) != 0) {
        return callNativeValueBuiltin();
    }

    if (call.name == "throw") {
        if (!valueArg({"msg", "reason"}, 0, a)) return false;
        env["error_reason"] = a->clone();

        const Arg* target = namedArg("target");
        if (target) {
            std::string targetName;
            if (auto targetVar = std::dynamic_pointer_cast<VarExpr>(target->value)) {
                targetName = targetVar->name;
            } else {
                std::shared_ptr<Expr> targetValue;
                if (!evalExprValue(target->value, env, targetValue)) return false;
                if (auto targetString = std::dynamic_pointer_cast<StringExpr>(targetValue)) {
                    targetName = targetString->value;
                } else {
                    return false;
                }
            }

            Call handler;
            handler.name = targetName;
            handler.args.push_back(Arg{"msg", a});

            auto it = clauses_.find(handler.name);
            if (it == clauses_.end() && ensurePredicateLoaded(handler.name)) {
                it = clauses_.find(handler.name);
            }
            if (it == clauses_.end()) return false;
            touchClauses(it->second);

            bool handled = false;
            for (const auto& originalClause : it->second) {
                Env attempt = env;
                auto clause = standardizeApart(*originalClause);
                if (!unifyCall(handler, clause->head, attempt)) continue;

                std::vector<Solution> nested;
                solveRecursive(clause->body, std::move(attempt), nested, 1, 0);
                if (!nested.empty()) {
                    env = std::move(nested.front().env);
                    handled = true;
                    break;
                }
                if (clause->body.empty()) {
                    env = std::move(attempt);
                    handled = true;
                    break;
                }
            }
            if (!handled) return false;
        }

        const Arg* out = namedArg("out");
        if (!out && call.args.size() > 1 && call.args[1].name.empty()) out = &call.args[1];
        if (out) return unifyExpr(out->value, a, env);
        return true;
    }

    if (call.name == "type") {
        if (!valueArg({"value", "data", "input"}, 0, a)) return false;
        auto typeValue = findMapValue(a, "__type");
        auto typeString = std::dynamic_pointer_cast<StringExpr>(typeValue);
        if (!typeString) return false;
        const Arg* out = namedArg("name");
        if (!out) out = namedArg("type");
        if (!out) out = namedArg("out");
        if (!out) out = outArg("out", 1);
        return out && unifyExpr(out->value, std::make_shared<StringExpr>(typeString->value), env);
    }

    if (call.name == "instanceof") {
        if (!valueArg({"value", "data", "input"}, 0, a)) return false;
        const Arg* typeArg = namedArg("type");
        if (!typeArg) typeArg = namedArg("parent");
        if (!typeArg) typeArg = namedArg("of");
        if (!typeArg && call.args.size() > 1) typeArg = &call.args[1];
        if (!typeArg) return false;
        auto typeValue = findMapValue(a, "__type");
        auto typeString = std::dynamic_pointer_cast<StringExpr>(typeValue);
        if (!typeString) return false;
        std::string expected;
        if (auto expectedString = std::dynamic_pointer_cast<StringExpr>(typeArg->value)) {
            expected = expectedString->value;
        } else if (auto expectedVar = std::dynamic_pointer_cast<VarExpr>(typeArg->value)) {
            expected = expectedVar->name;
        } else {
            return false;
        }
        return memory_.isCompatibleType(typeString->value, expected);
    }

    if (call.name == "math:add" || call.name == "math:sub" ||
        call.name == "math:mul" || call.name == "math:div" ||
        call.name == "math:mod") {
        if (!valueArg({"lhs", "left"}, 0, a) || !valueArg({"rhs", "right"}, 1, b)) return false;
        double left = 0.0;
        double right = 0.0;
        if (!argAsNumber(a, left) || !argAsNumber(b, right)) return false;
        if (call.name == "math:add") result = std::make_shared<NumberExpr>(left + right);
        if (call.name == "math:sub") result = std::make_shared<NumberExpr>(left - right);
        if (call.name == "math:mul") result = std::make_shared<NumberExpr>(left * right);
        if (call.name == "math:div") {
            if (std::fabs(right) < 1e-12) throw InterpreterError("DivisionByZero");
            result = std::make_shared<NumberExpr>(left / right);
        }
        if (call.name == "math:mod") {
            if (std::fabs(right) < 1e-12) throw InterpreterError("DivisionByZero");
            result = std::make_shared<NumberExpr>(std::fmod(left, right));
        }
        const Arg* out = namedArg("result");
        if (!out) out = outArg("out", 2);
        return out && unifyExpr(out->value, result, env);
    }

    if (call.name == "str:len" || call.name == "str:contains" ||
        call.name == "str:concat" || call.name == "str:lower" ||
        call.name == "str:upper" || call.name == "str:trim" ||
        call.name == "str:split" || call.name == "str:replace" ||
        call.name == "str:startsWith" || call.name == "str:endsWith") {
        if (!valueArg({"left", "data", "value"}, 0, a)) return false;
        std::string text;
        if (!argAsString(a, text)) return false;
        if (call.name == "str:len") {
            result = std::make_shared<NumberExpr>(static_cast<double>(text.size()));
            const Arg* out = namedArg("equals");
            if (!out) out = outArg("out", 1);
            return out && unifyExpr(out->value, result, env);
        }
        if (call.name == "str:contains") {
            if (!valueArg({"needle"}, 1, b)) return false;
            std::string needle;
            if (!argAsString(b, needle)) return false;
            result = std::make_shared<StringExpr>(text.find(needle) != std::string::npos ? "true" : "false");
            const Arg* out = namedArg("access");
            if (!out) out = outArg("out", 2);
            return out ? unifyExpr(out->value, result, env) : text.find(needle) != std::string::npos;
        }
        if (call.name == "str:concat") {
            if (!valueArg({"right", "rhs"}, 1, b)) return false;
            std::string right;
            if (!argAsString(b, right)) return false;
            result = std::make_shared<StringExpr>(text + right);
            const Arg* out = namedArg("result");
            if (!out) out = outArg("out", 2);
            return out && unifyExpr(out->value, result, env);
        }
        if (call.name == "str:trim") {
            result = std::make_shared<StringExpr>(Felidae::trim(text));
            const Arg* out = namedArg("access");
            if (!out) out = outArg("out", 1);
            return out && unifyExpr(out->value, result, env);
        }
        if (call.name == "str:split") {
            if (!valueArg({"delimiter", "separator"}, 1, b)) return false;
            std::string delimiter;
            if (!argAsString(b, delimiter) || delimiter.empty()) return false;
            std::vector<std::shared_ptr<Expr>> parts;
            size_t start = 0;
            while (true) {
                size_t pos = text.find(delimiter, start);
                parts.push_back(std::make_shared<StringExpr>(text.substr(start, pos == std::string::npos ? pos : pos - start)));
                if (pos == std::string::npos) break;
                start = pos + delimiter.size();
            }
            const Arg* out = namedArg("access");
            if (!out) out = outArg("out", 2);
            return out && unifyExpr(out->value, std::make_shared<ArrayExpr>(std::move(parts)), env);
        }
        if (call.name == "str:replace") {
            if (!valueArg({"search", "needle"}, 1, b)) return false;
            std::shared_ptr<Expr> replacementValue;
            const Arg* replacementArg = namedArg("replacement");
            if (!replacementArg) replacementArg = namedArg("with");
            if (!replacementArg || !evalExprValue(replacementArg->value, env, replacementValue)) return false;
            std::string search;
            std::string replacement;
            if (!argAsString(b, search) || !argAsString(replacementValue, replacement) || search.empty()) return false;
            size_t pos = 0;
            while ((pos = text.find(search, pos)) != std::string::npos) {
                text.replace(pos, search.size(), replacement);
                pos += replacement.size();
            }
            const Arg* out = namedArg("access");
            if (!out) out = outArg("out", 3);
            return out && unifyExpr(out->value, std::make_shared<StringExpr>(text), env);
        }
        if (call.name == "str:startsWith" || call.name == "str:endsWith") {
            if (!valueArg({call.name == "str:startsWith" ? "prefix" : "suffix"}, 1, b)) return false;
            std::string needle;
            if (!argAsString(b, needle)) return false;
            bool ok = call.name == "str:startsWith"
                ? text.rfind(needle, 0) == 0
                : (needle.size() <= text.size() && text.compare(text.size() - needle.size(), needle.size(), needle) == 0);
            const Arg* out = namedArg("access");
            if (!out) out = outArg("out", 2);
            return out && unifyExpr(out->value, std::make_shared<StringExpr>(ok ? "true" : "false"), env);
        }
        for (char& ch : text) {
            ch = static_cast<char>(call.name == "str:lower"
                ? std::tolower(static_cast<unsigned char>(ch))
                : std::toupper(static_cast<unsigned char>(ch)));
        }
        result = std::make_shared<StringExpr>(text);
        const Arg* out = namedArg("equals");
        if (!out) out = outArg("out", 1);
        return out && unifyExpr(out->value, result, env);
    }

    if (call.name == "array:get" || call.name == "array:len" || call.name == "array:push") {
        if (!valueArg({"data", "array"}, 0, a)) return false;
        auto args = termArgs(a, "fn:array");
        if (args.empty() && !(std::dynamic_pointer_cast<TermExpr>(a) &&
                              std::dynamic_pointer_cast<TermExpr>(a)->name == "fn:array") &&
            !std::dynamic_pointer_cast<ArrayExpr>(a)) {
            return false;
        }
        if (call.name == "array:len") {
            const Arg* out = namedArg("access");
            if (!out) out = outArg("out", 1);
            return out && unifyExpr(out->value, std::make_shared<NumberExpr>(static_cast<double>(args.size())), env);
        }
        if (call.name == "array:get") {
            if (!valueArg({"position", "index"}, 1, b)) return false;
            double index = 0.0;
            if (!argAsNumber(b, index) || index < 0 || std::floor(index) != index) return false;
            size_t i = static_cast<size_t>(index);
            if (i >= args.size()) return false;
            const Arg* out = namedArg("access");
            if (!out) out = outArg("out", 2);
            return out && unifyExpr(out->value, args[i], env);
        }
        if (call.name == "array:push") {
            if (!valueArg({"value"}, 1, b)) return false;
            args.push_back(b);
            const Arg* out = namedArg("result");
            if (!out) out = outArg("out", 2);
            return out && unifyExpr(out->value, std::make_shared<ArrayExpr>(std::move(args)), env);
        }
    }

    if (call.name == "pair:first" || call.name == "pair:second") {
        if (!valueArg({"data", "pair"}, 0, a)) return false;
        auto args = termArgs(a, "fn:pair");
        if (args.size() != 2) return false;
        const Arg* out = namedArg("access");
        if (!out) out = outArg("out", 1);
        return out && unifyExpr(out->value, call.name == "pair:first" ? args[0] : args[1], env);
    }

    if (call.name == "json:parse") {
        if (!valueArg({"data", "value"}, 0, a)) return false;
        std::string jsonText;
        if (!argAsString(a, jsonText)) return false;
        std::shared_ptr<Expr> parsed;
        if (!parseFlatJsonObject(jsonText, parsed)) return false;
        const Arg* out = namedArg("access");
        if (!out) out = outArg("out", 1);
        return out && unifyExpr(out->value, parsed, env);
    }

    if (call.name == "json:get") {
        if (!valueArg({"data", "object"}, 0, a) || !valueArg({"key"}, 1, b)) return false;
        std::string key;
        if (!argAsString(b, key)) return false;
        auto value = findMapValue(a, key);
        const Arg* out = namedArg("access");
        if (!out) out = outArg("out", 2);
        if (value && out) return unifyExpr(out->value, value, env);
        return false;
    }

    if (call.name == "json:has" || call.name == "json:keys" ||
        call.name == "json:set" || call.name == "json:remove") {
        if (!valueArg({"data", "object"}, 0, a)) return false;
        std::vector<MapEntry> entries;
        if (!exprAsMapEntries(a, entries)) return false;
        std::shared_ptr<Expr> resultValue;
        if (call.name == "json:keys") {
            std::vector<std::shared_ptr<Expr>> keys;
            for (const auto& entry : entries) keys.push_back(std::make_shared<StringExpr>(entry.key));
            resultValue = std::make_shared<ArrayExpr>(std::move(keys));
            const Arg* out = namedArg("access");
            if (!out) out = outArg("out", 1);
            return out && unifyExpr(out->value, resultValue, env);
        }
        if (!valueArg({"key"}, 1, b)) return false;
        std::string key;
        if (!argAsString(b, key)) return false;
        if (call.name == "json:has") {
            resultValue = std::make_shared<StringExpr>(findMapValue(a, key) ? "true" : "false");
            const Arg* out = namedArg("access");
            if (!out) out = outArg("out", 2);
            return out && unifyExpr(out->value, resultValue, env);
        }
        if (call.name == "json:set") {
            std::shared_ptr<Expr> value;
            const Arg* valueArgPtr = namedArg("value");
            if (!valueArgPtr || !evalExprValue(valueArgPtr->value, env, value)) return false;
            upsertEntry(entries, key, cloneExprOrNil(value));
        } else {
            removeEntry(entries, key);
        }
        const Arg* out = namedArg("access");
        if (!out) out = outArg("out", call.name == "json:set" ? 3 : 2);
        return out && unifyExpr(out->value, std::make_shared<MapExpr>(std::move(entries)), env);
    }

    if (call.name == "json:toText") {
        if (!valueArg({"data", "value"}, 0, a)) return false;
        const Arg* out = namedArg("access");
        if (!out) out = outArg("out", 1);
        return out && unifyExpr(out->value, std::make_shared<StringExpr>(exprToJson(a)), env);
    }

    if (call.name == "csv:parse" || call.name == "csv:toFacts") {
        if (!valueArg({"data", "text", "csv"}, 0, a)) return false;
        std::string csvText;
        if (!argAsString(a, csvText)) return false;
        std::string typeName;
        if (call.name == "csv:toFacts") {
            if (!valueArg({"type", "fact"}, 1, b) || !argAsString(b, typeName) || typeName.empty()) {
                throw InterpreterError("csv.toFacts expects non-empty string argument 'type'");
            }
        }
        const Arg* out = namedArg("access");
        if (!out) out = outArg("out", call.name == "csv:toFacts" ? 2 : 1);
        try {
            return out && unifyExpr(out->value, NativeCsv::parse(csvText, typeName, call.name), env);
        } catch (const NativeCsv::Error& ex) {
            throw InterpreterError(ex.what());
        }
    }

    if (call.name == "csv:toText" || call.name == "csv:toFelidaeFacts") {
        if (!valueArg({"data", "rows"}, 0, a)) return false;
        std::shared_ptr<Expr> result;
        try {
            if (call.name == "csv:toText") {
                result = std::make_shared<StringExpr>(NativeCsv::toText(a, call.name));
            } else {
                if (!valueArg({"type", "fact"}, 1, b)) return false;
                std::string typeName;
                if (!argAsString(b, typeName)) return false;
                result = std::make_shared<StringExpr>(NativeCsv::toFelidaeFacts(a, typeName, call.name));
            }
        } catch (const NativeCsv::Error& ex) {
            throw InterpreterError(ex.what());
        }
        const Arg* out = namedArg("access");
        if (!out) out = outArg("out", call.name == "csv:toFelidaeFacts" ? 2 : 1);
        return out && unifyExpr(out->value, result, env);
    }

    if (call.name == "csv:addRow" || call.name == "csv:findRows" ||
        call.name == "csv:updateRows" || call.name == "csv:deleteRows") {
        if (!valueArg({"data", "rows"}, 0, a)) return false;
        std::vector<std::shared_ptr<Expr>> rows;
        if (!exprAsArrayItems(a, rows)) return false;
        std::vector<std::shared_ptr<Expr>> resultRows;
        if (call.name == "csv:addRow") {
            if (!valueArg({"row", "value"}, 1, b)) return false;
            std::vector<MapEntry> rowEntries;
            if (!exprAsMapEntries(b, rowEntries)) return false;
            resultRows = std::move(rows);
            resultRows.push_back(std::make_shared<MapExpr>(std::move(rowEntries)));
        } else {
            if (!valueArg({"key", "field"}, 1, b)) return false;
            std::string key;
            if (!argAsString(b, key)) return false;
            std::shared_ptr<Expr> valueExpr;
            const Arg* valueArgPtr = namedArg("value");
            if (!valueArgPtr || !evalExprValue(valueArgPtr->value, env, valueExpr)) return false;
            std::string expected = exprTextValue(valueExpr);
            std::shared_ptr<Expr> patch;
            if (call.name == "csv:updateRows") {
                const Arg* patchArg = namedArg("patch");
                if (!patchArg || !evalExprValue(patchArg->value, env, patch)) return false;
                std::vector<MapEntry> patchEntries;
                if (!exprAsMapEntries(patch, patchEntries)) return false;
            }
            for (const auto& row : rows) {
                bool match = mapFieldMatches(row, key, expected);
                if (call.name == "csv:findRows") {
                    if (match) resultRows.push_back(cloneExprOrNil(row));
                } else if (call.name == "csv:deleteRows") {
                    if (!match) resultRows.push_back(cloneExprOrNil(row));
                } else {
                    auto patched = match ? patchMapValue(row, patch) : cloneExprOrNil(row);
                    if (!patched) throw InterpreterError("csv.updateRows expects map rows in 'data'");
                    resultRows.push_back(patched);
                }
            }
        }
        const Arg* out = namedArg("access");
        if (!out) out = outArg("out", call.name == "csv:addRow" ? 2 : (call.name == "csv:updateRows" ? 4 : 3));
        return out && unifyExpr(out->value, std::make_shared<ArrayExpr>(std::move(resultRows)), env);
    }

    return false;
}

void* Interpreter::findSharedSymbol(void* handle, const char* name) const {
    return findSharedLibrarySymbol(handle, name);
}

const ClauseStmt* Interpreter::nativeDeclarationFor(const std::string& name) const {
    auto it = clauses_.find(name);
    if (it == clauses_.end()) return nullptr;
    for (const auto& clause : it->second) {
        if (clause && clause->emptyDeclaration) return clause.get();
    }
    return nullptr;
}

void Interpreter::validateNativeCallTypes(const Call& call, const ClauseStmt& declaration, const Env& env) {
    for (size_t i = 0; i < declaration.head.args.size(); ++i) {
        const Arg& declared = declaration.head.args[i];
        const Arg* provided = findArg(call, declared, i);
        if (!provided) continue;
        auto typeName = std::dynamic_pointer_cast<VarExpr>(declared.value);
        if (!typeName || !isBuiltinTypeName(typeName->name)) continue;

        std::shared_ptr<Expr> value;
        if (!evalExprValue(provided->value, env, value)) {
            if (std::dynamic_pointer_cast<VarExpr>(provided->value)) continue;
            throw InterpreterError(call.name + " cannot evaluate argument '" +
                                   (declared.name.empty() ? std::to_string(i) : declared.name) +
                                   "' before native type checking");
        }
        if (!valueMatchesBuiltinType(value, typeName->name)) {
            throw InterpreterError(call.name + " expects argument '" +
                                   (declared.name.empty() ? std::to_string(i) : declared.name) +
                                   "' to be " + typeName->name + " before calling native library");
        }
    }
}

bool Interpreter::solveNativeCall(const Call& call, Env& env) {
    if (nativeLibraries_.empty()) return false;
    const ClauseStmt* declaration = nativeDeclarationFor(call.name);
    if (!declaration) return false;

    std::string moduleName = call.name;
    size_t sep = moduleName.find(':');
    if (sep != std::string::npos) moduleName = moduleName.substr(0, sep);

    const NativeLibrary* library = nullptr;
    for (const auto& candidate : nativeLibraries_) {
        if (candidate.moduleName == moduleName) {
            library = &candidate;
            break;
        }
    }
    if (!library && nativeLibraries_.size() == 1) library = &nativeLibraries_.front();
    if (!library) return false;

    validateNativeCallTypes(call, *declaration, env);

    std::ostringstream json;
    json << "{";
    bool first = true;
    std::vector<const Arg*> outputArgs;
    for (size_t i = 0; i < call.args.size(); ++i) {
        const Arg& arg = call.args[i];
        std::shared_ptr<Expr> value;
        if (!evalExprValue(arg.value, env, value)) {
            if (std::dynamic_pointer_cast<VarExpr>(arg.value)) {
                outputArgs.push_back(&arg);
                continue;
            }
            return false;
        }
        if (!first) json << ",";
        first = false;
        std::string key = arg.name.empty() ? ("arg" + std::to_string(i)) : arg.name;
        json << "\"" << jsonEscape(key) << "\":" << exprToJson(value);
    }
    json << "}";

    char* response = library->call(call.name.c_str(), json.str().c_str());
    if (!response) throw InterpreterError("Native function '" + call.name + "' returned a null response");
    std::string responseText(response);
    library->free(response);

    std::shared_ptr<Expr> parsed;
    size_t pos = 0;
    if (!parseJsonValue(responseText, pos, parsed)) {
        throw InterpreterError("Native function '" + call.name + "' returned invalid JSON");
    }
    skipJsonWs(responseText, pos);
    if (pos != responseText.size()) {
        throw InterpreterError("Native function '" + call.name + "' returned trailing data after JSON");
    }

    if (auto errorValue = findMapValue(parsed, "error")) {
        std::string message;
        if (argAsString(errorValue, message) && !message.empty()) {
            throw InterpreterError("Native function '" + call.name + "' failed: " + message);
        }
    }

    env["__return"] = parsed->clone();
    if (outputArgs.empty()) return true;

    auto returnedMap = std::dynamic_pointer_cast<MapExpr>(parsed);
    for (const Arg* outArg : outputArgs) {
        std::shared_ptr<Expr> value;
        if (returnedMap) {
            if (!outArg->name.empty()) value = findMapValue(parsed, outArg->name);
            if (!value) value = findMapValue(parsed, "access");
            if (!value) value = findMapValue(parsed, "out");
            if (!value) value = findMapValue(parsed, "result");
            if (!value) value = findMapValue(parsed, "value");
            if (!value && returnedMap->entries.size() == 1) value = returnedMap->entries.front().value;
        } else {
            value = parsed;
        }
        if (!value || !unifyExpr(outArg->value, value, env)) return false;
    }
    return true;
}

bool Interpreter::evalBuiltinTerm(const TermExpr& term, const Env& env, std::shared_ptr<Expr>& out) {
    std::vector<std::shared_ptr<Expr>> args;
    args.reserve(term.args.size());
    for (const auto& arg : term.args) {
        std::shared_ptr<Expr> value;
        if (!evalExprValue(arg.value, env, value)) {
            if (auto var = std::dynamic_pointer_cast<VarExpr>(arg.value)) {
                if (!var->name.empty() && std::isupper(static_cast<unsigned char>(var->name.front()))) {
                    value = arg.value->clone();
                } else {
                    return false;
                }
            } else {
                return false;
            }
        }
        args.push_back(value);
    }

    if (term.name == "fn:pair") {
        if (args.size() != 2) return false;
        out = std::make_shared<TermExpr>("fn:pair", std::vector<Arg>{{"first", args[0]}, {"last", args[1]}});
        return true;
    }
    if (term.name == "fn:tuple") {
        std::vector<Arg> termArgs;
        for (auto& arg : args) termArgs.push_back(Arg{"value", arg});
        out = std::make_shared<TermExpr>("fn:tuple", std::move(termArgs));
        return true;
    }
    if (term.name == "fn:array") {
        if (args.size() == 1) {
            if (auto array = std::dynamic_pointer_cast<ArrayExpr>(args[0])) {
                out = array->clone();
                return true;
            }
        }
        std::vector<Arg> termArgs;
        for (auto& arg : args) termArgs.push_back(Arg{"data", arg});
        out = std::make_shared<TermExpr>("fn:array", std::move(termArgs));
        return true;
    }
    if (term.name == "json:object") {
        std::vector<Arg> termArgs;
        for (auto& arg : args) termArgs.push_back(Arg{"field", arg});
        out = std::make_shared<TermExpr>("json:object", std::move(termArgs));
        return true;
    }
    auto hasCallableBody = [&]() {
        auto it = clauses_.find(term.name);
        if (it == clauses_.end() && ensurePredicateLoaded(term.name)) it = clauses_.find(term.name);
        if (it == clauses_.end()) return false;
        for (const auto& clause : it->second) {
            if (!clause->body.empty() || isMethodClause(*clause)) return true;
        }
        return false;
    };
    if (hasMethod(term.name) || hasCallableBody()) return evalCallAsValue(term, env, out);
    if (isBuiltinFunctionName(term.name)) return evalCallAsValue(term, env, out);

    if (term.name.find(':') == std::string::npos && !term.args.empty()) {
        std::vector<MapEntry> entries;
        entries.push_back(MapEntry{"__type", std::make_shared<StringExpr>(term.name)});
        for (size_t i = 0; i < term.args.size(); ++i) {
            const auto& arg = term.args[i];
            if (arg.name.empty()) {
                entries.push_back(MapEntry{"value", args[i]});
            } else {
                entries.push_back(MapEntry{arg.name, args[i]});
            }
        }
        out = std::make_shared<MapExpr>(std::move(entries));
        return true;
    }

    std::vector<Arg> termArgs;
    for (auto& arg : args) termArgs.push_back(Arg{"value", arg});
    out = std::make_shared<TermExpr>(term.name, std::move(termArgs));
    return true;
}

bool Interpreter::evalCallAsValue(const TermExpr& term, const Env& env, std::shared_ptr<Expr>& out) {
    if (!nativeDeclarationFor(term.name)) {
        ensurePredicateLoaded(term.name);
    }
    if (!nativeLibraries_.empty() && nativeDeclarationFor(term.name)) {
        Call nativeCall(term.name, {});
        for (const auto& arg : term.args) nativeCall.args.push_back(Arg{arg.name, arg.value->clone()});
        Env nativeEnv = env;
        if (solveNativeCall(nativeCall, nativeEnv)) {
            auto returned = nativeEnv.find("__return");
            if (returned != nativeEnv.end()) {
                if (auto returnedMap = std::dynamic_pointer_cast<MapExpr>(returned->second)) {
                    if (returnedMap->entries.size() == 1) {
                        out = returnedMap->entries.front().value->clone();
                        return true;
                    }
                }
                out = returned->second->clone();
                return true;
            }
        }
    }

    std::vector<std::shared_ptr<Expr>> args;
    args.reserve(term.args.size());
    for (const auto& arg : term.args) {
        std::shared_ptr<Expr> value;
        if (!evalExprValue(arg.value, env, value)) {
            if (auto var = std::dynamic_pointer_cast<VarExpr>(arg.value)) {
                if (!var->name.empty() && std::isupper(static_cast<unsigned char>(var->name.front()))) {
                    value = arg.value->clone();
                } else {
                    return false;
                }
            } else {
                return false;
            }
        }
        args.push_back(value);
    }

    auto callableIt = clauses_.find(term.name);
    bool callableBody = false;
    if (callableIt != clauses_.end()) {
        for (const auto& clause : callableIt->second) {
            if (!clause->body.empty() || isMethodClause(*clause)) {
                callableBody = true;
                break;
            }
        }
    }
    if (term.name.find(':') == std::string::npos && callableBody) {
        Call call(term.name, {});
        for (size_t i = 0; i < args.size(); ++i) {
            call.args.push_back(Arg{term.args[i].name, args[i]});
        }
        auto it = callableIt;
        if (it == clauses_.end()) return false;
        for (const auto& clause : it->second) {
            if (clause->body.empty() && !isMethodClause(*clause)) continue;
            std::vector<Solution> solutions;
            const bool previousValueCallMode = valueCallMode_;
            valueCallMode_ = true;
            try {
                solveMethodCall(call, clause, env, solutions, 1, 0);
            } catch (...) {
                valueCallMode_ = previousValueCallMode;
                throw;
            }
            valueCallMode_ = previousValueCallMode;
            if (!solutions.empty()) {
                auto returned = solutions.front().env.find("__return");
                if (returned != solutions.front().env.end()) {
                    if (auto returnedMap = std::dynamic_pointer_cast<MapExpr>(returned->second)) {
                        if (returnedMap->entries.size() == 1 && returnedMap->entries.front().key.empty()) {
                            out = returnedMap->entries.front().value->clone();
                            return true;
                        }
                    }
                    out = returned->second->clone();
                    return true;
                }
            }
        }
        std::vector<Solution> goalSolutions;
        solveRecursive({std::make_shared<CallGoal>(call)}, env, goalSolutions, 1, 0);
        if (!goalSolutions.empty()) {
            out = std::make_shared<StringExpr>("true");
            return true;
        }
        return false;
    }

    auto evalNamed = [&](const std::string& name, size_t index, std::shared_ptr<Expr>& value) -> bool {
        const Arg* arg = findTermArgByNameOrIndex(term, name, index);
        if (!arg) return false;
        return evalExprValue(arg->value, env, value);
    };

    auto requireNamedString = [&](std::initializer_list<const char*> names, size_t index, const std::string& label) {
        std::shared_ptr<Expr> value;
        const Arg* arg = nullptr;
        for (const char* name : names) {
            arg = findTermArgByNameOrIndex(term, name, index);
            if (arg) break;
        }
        if (!arg || !evalExprValue(arg->value, env, value)) {
            throw InterpreterError(term.name + " expects string argument '" + label + "'");
        }
        return requireString(value, term.name, label);
    };

    auto requireNamedNumber = [&](std::initializer_list<const char*> names, size_t index, const std::string& label) {
        std::shared_ptr<Expr> value;
        const Arg* arg = nullptr;
        for (const char* name : names) {
            arg = findTermArgByNameOrIndex(term, name, index);
            if (arg) break;
        }
        if (!arg || !evalExprValue(arg->value, env, value)) {
            throw InterpreterError(term.name + " expects numeric argument '" + label + "'");
        }
        return requireNumber(value, term.name, label);
    };

    auto optionalNamedBool = [&](std::initializer_list<const char*> names, size_t index, bool defaultValue) {
        std::shared_ptr<Expr> value;
        const Arg* arg = nullptr;
        for (const char* name : names) {
            arg = findTermArgByNameOrIndex(term, name, index);
            if (arg) break;
        }
        if (!arg) return defaultValue;
        if (!evalExprValue(arg->value, env, value)) {
            throw InterpreterError(term.name + " expects boolean-like string argument 'loadImports'");
        }
        std::string text = requireString(value, term.name, "loadImports");
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (text == "true" || text == "yes" || text == "1") return true;
        if (text == "false" || text == "no" || text == "0") return false;
        throw InterpreterError(term.name + " expects loadImports to be true or false");
    };

    auto arrayItems = [&](const std::shared_ptr<Expr>& value) -> std::vector<std::shared_ptr<Expr>> {
        if (auto array = std::dynamic_pointer_cast<ArrayExpr>(value)) return array->items;
        if (auto var = std::dynamic_pointer_cast<VarExpr>(term.args.empty() ? nullptr : term.args[0].value)) {
            if (!var->name.empty() && std::isupper(static_cast<unsigned char>(var->name.front()))) {
                return valuesForLambdaSource(term.args[0].value, env);
            }
        }
        return {};
    };

    if (term.name == "console:readLine") {
        std::string line;
        if (!std::getline(std::cin, line)) line.clear();
        out = std::make_shared<StringExpr>(line);
        return true;
    }

    if (term.name == "console:writeLine" || term.name == "console:write" || term.name == "system:print") {
        if (args.size() != 1) throw InterpreterError(term.name + " expects one value");
        std::string text = args[0]->debug();
        if (auto str = std::dynamic_pointer_cast<StringExpr>(args[0])) text = str->value;
        std::cout << text;
        if (term.name == "console:writeLine" || term.name == "system:print") std::cout << "\n";
        out = std::make_shared<StringExpr>("ok");
        return true;
    }

    if (term.name == "str:concat") {
        std::string left = requireNamedString({"left", "data", "value"}, 0, "left");
        std::string right = requireNamedString({"right", "rhs"}, 1, "right");
        out = std::make_shared<StringExpr>(left + right);
        return true;
    }

    if (term.name == "str:lower" || term.name == "str:upper") {
        std::string text = requireNamedString({"data", "value"}, 0, "data");
        for (char& ch : text) {
            ch = static_cast<char>(term.name == "str:lower"
                ? std::tolower(static_cast<unsigned char>(ch))
                : std::toupper(static_cast<unsigned char>(ch)));
        }
        out = std::make_shared<StringExpr>(text);
        return true;
    }

    if (term.name == "str:trim") {
        out = std::make_shared<StringExpr>(Felidae::trim(requireNamedString({"data", "value"}, 0, "data")));
        return true;
    }

    if (term.name == "str:split") {
        std::string text = requireNamedString({"data", "value"}, 0, "data");
        std::string delimiter = requireNamedString({"delimiter", "separator"}, 1, "delimiter");
        if (delimiter.empty()) throw InterpreterError("str.split expects non-empty delimiter");
        std::vector<std::shared_ptr<Expr>> parts;
        size_t start = 0;
        while (true) {
            size_t pos = text.find(delimiter, start);
            parts.push_back(std::make_shared<StringExpr>(text.substr(start, pos == std::string::npos ? pos : pos - start)));
            if (pos == std::string::npos) break;
            start = pos + delimiter.size();
        }
        out = std::make_shared<ArrayExpr>(std::move(parts));
        return true;
    }

    if (term.name == "str:replace") {
        std::string text = requireNamedString({"data", "value"}, 0, "data");
        std::string search = requireNamedString({"search", "needle"}, 1, "search");
        std::string replacement = requireNamedString({"replacement", "with"}, 2, "replacement");
        if (search.empty()) throw InterpreterError("str.replace expects non-empty search text");
        size_t pos = 0;
        while ((pos = text.find(search, pos)) != std::string::npos) {
            text.replace(pos, search.size(), replacement);
            pos += replacement.size();
        }
        out = std::make_shared<StringExpr>(text);
        return true;
    }

    if (term.name == "str:contains" || term.name == "str:startsWith" || term.name == "str:endsWith") {
        std::string text = requireNamedString({"data", "value"}, 0, "data");
        std::string needle = term.name == "str:contains"
            ? requireNamedString({"needle", "search"}, 1, "needle")
            : requireNamedString({term.name == "str:startsWith" ? "prefix" : "suffix"}, 1, "needle");
        bool ok = term.name == "str:contains"
            ? text.find(needle) != std::string::npos
            : (term.name == "str:startsWith"
                ? text.rfind(needle, 0) == 0
                : (needle.size() <= text.size() && text.compare(text.size() - needle.size(), needle.size(), needle) == 0));
        out = std::make_shared<StringExpr>(ok ? "true" : "false");
        return true;
    }

    if (term.name == "str:len") {
        std::string text = requireNamedString({"data", "value"}, 0, "data");
        out = std::make_shared<NumberExpr>(static_cast<double>(text.size()));
        return true;
    }

    if (term.name == "file:readFile") {
        std::string pathText = requireNamedString({"path", "file"}, 0, "path");
        fs::path target(pathText);
        if (fs::is_directory(target)) throw InterpreterError("file.readFile expected a file, got directory: " + pathText);
        try {
            out = std::make_shared<StringExpr>(readSourceFile(target));
        } catch (const std::exception& e) {
            throw InterpreterError("file.readFile failed: " + std::string(e.what()));
        }
        return true;
    }

    if (term.name == "file:readLines") {
        std::string pathText = requireNamedString({"path", "file"}, 0, "path");
        fs::path target(pathText);
        if (fs::is_directory(target)) throw InterpreterError("file.readLines expected a file, got directory: " + pathText);
        std::vector<std::shared_ptr<Expr>> lines;
        try {
            readSourceLines(target, [&](const std::string& line) {
                lines.push_back(std::make_shared<StringExpr>(line));
            });
        } catch (const std::exception& e) {
            throw InterpreterError("file.readLines failed: " + std::string(e.what()));
        }
        out = std::make_shared<ArrayExpr>(std::move(lines));
        return true;
    }

    if (term.name == "file:readLine") {
        std::string pathText = requireNamedString({"path", "file"}, 0, "path");
        double lineNumber = requireNamedNumber({"line", "index"}, 1, "line");
        if (lineNumber < 0 || std::floor(lineNumber) != lineNumber) {
            throw InterpreterError("file.readLine expects non-negative integer argument 'line'");
        }
        fs::path target(pathText);
        if (fs::is_directory(target)) throw InterpreterError("file.readLine expected a file, got directory: " + pathText);
        std::ifstream in(target, std::ios::binary);
        if (!in) throw InterpreterError("file.readLine cannot open: " + pathText);
        std::string line;
        size_t wanted = static_cast<size_t>(lineNumber);
        for (size_t index = 0; std::getline(in, line); ++index) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (index == wanted) {
                out = std::make_shared<StringExpr>(line);
                return true;
            }
        }
        return false;
    }

    if (term.name == "file:writeFile" || term.name == "file:appendFile" || term.name == "file:writeLines") {
        std::string pathText = requireNamedString({"path", "file"}, 0, "path");
        std::string data;
        if (term.name == "file:writeLines") {
            std::shared_ptr<Expr> linesValue;
            if (!evalNamed("data", 1, linesValue) && !evalNamed("lines", 1, linesValue)) {
                throw InterpreterError("file.writeLines expects array argument 'data'");
            }
            std::vector<std::shared_ptr<Expr>> lines;
            if (!exprAsArrayItems(linesValue, lines)) throw InterpreterError("file.writeLines expects array argument 'data'");
            std::ostringstream joined;
            for (const auto& line : lines) joined << exprTextValue(line) << "\n";
            data = joined.str();
        } else {
            data = requireNamedString({"data", "text", "content"}, 1, "data");
        }
        std::string modeText = term.name == "file:appendFile" ? "append" : "write";
        if (const Arg* modeArg = findTermArgByNameOrIndex(term, "mode", 2)) {
            std::shared_ptr<Expr> modeValue;
            if (!evalExprValue(modeArg->value, env, modeValue) || !argAsString(modeValue, modeText)) {
                throw InterpreterError(term.name + " expects string argument 'mode'");
            }
        }
        if (modeText != "write" && modeText != "append") {
            throw InterpreterError(term.name + " mode must be 'write' or 'append'");
        }
        std::ios::openmode mode = std::ios::binary;
        mode |= modeText == "append" ? std::ios::app : std::ios::trunc;
        std::ofstream outFile(fs::path(pathText), mode);
        if (!outFile) throw InterpreterError(term.name + " cannot open: " + pathText);
        outFile << data;
        out = std::make_shared<StringExpr>("ok");
        return true;
    }

    if (term.name == "file:exists") {
        std::string pathText = requireNamedString({"path", "file"}, 0, "path");
        out = std::make_shared<StringExpr>(fs::exists(fs::path(pathText)) ? "true" : "false");
        return true;
    }

    if (term.name == "file:deleteFile") {
        std::string pathText = requireNamedString({"path", "file"}, 0, "path");
        std::error_code ec;
        fs::path target(pathText);
        if (fs::is_directory(target, ec)) {
            if (ec) throw InterpreterError("file.deleteFile failed: " + ec.message());
            throw InterpreterError("file.deleteFile expected a file, got directory: " + pathText);
        }
        bool removed = fs::remove(target, ec);
        if (ec) throw InterpreterError("file.deleteFile failed: " + ec.message());
        out = std::make_shared<StringExpr>(removed ? "true" : "false");
        return true;
    }

    if (term.name == "csv:parse" || term.name == "csv:toFacts") {
        std::string csvText = requireNamedString({"data", "text", "csv"}, 0, "data");
        std::string typeName;
        if (term.name == "csv:toFacts") {
            typeName = requireNamedString({"type", "fact"}, 1, "type");
            if (typeName.empty()) throw InterpreterError("csv.toFacts expects non-empty string argument 'type'");
        }
        try {
            out = NativeCsv::parse(csvText, typeName, term.name);
        } catch (const NativeCsv::Error& ex) {
            throw InterpreterError(ex.what());
        }
        return true;
    }

    if (term.name == "csv:toText" || term.name == "csv:toFelidaeFacts") {
        std::shared_ptr<Expr> dataValue;
        if (!evalNamed("data", 0, dataValue) && !evalNamed("rows", 0, dataValue)) {
            throw InterpreterError(term.name + " expects array argument 'data'");
        }
        std::string text;
        try {
            if (term.name == "csv:toText") {
                text = NativeCsv::toText(dataValue, term.name);
            } else {
                std::string typeName = requireNamedString({"type", "fact"}, 1, "type");
                text = NativeCsv::toFelidaeFacts(dataValue, typeName, term.name);
            }
        } catch (const NativeCsv::Error& ex) {
            throw InterpreterError(ex.what());
        }
        out = std::make_shared<StringExpr>(text);
        return true;
    }

    if (term.name == "json:parse") {
        std::string jsonText = requireNamedString({"data", "value"}, 0, "data");
        std::shared_ptr<Expr> parsed;
        if (!parseFlatJsonObject(jsonText, parsed)) {
            throw InterpreterError("json.parse failed: invalid JSON");
        }
        out = parsed;
        return true;
    }

    if (term.name == "json:get") {
        std::shared_ptr<Expr> dataValue;
        if (!evalNamed("data", 0, dataValue) && !evalNamed("object", 0, dataValue)) {
            throw InterpreterError("json.get expects argument 'data'");
        }
        std::string key = requireNamedString({"key"}, 1, "key");
        auto value = findMapValue(dataValue, key);
        if (!value) return false;
        out = value->clone();
        return true;
    }

    if (term.name == "json:has" || term.name == "json:keys" ||
        term.name == "json:set" || term.name == "json:remove") {
        std::shared_ptr<Expr> dataValue;
        if (!evalNamed("data", 0, dataValue) && !evalNamed("object", 0, dataValue)) {
            throw InterpreterError(term.name + " expects argument 'data'");
        }
        std::vector<MapEntry> entries;
        if (!exprAsMapEntries(dataValue, entries)) throw InterpreterError(term.name + " expects map/object argument 'data'");
        if (term.name == "json:keys") {
            std::vector<std::shared_ptr<Expr>> keys;
            for (const auto& entry : entries) keys.push_back(std::make_shared<StringExpr>(entry.key));
            out = std::make_shared<ArrayExpr>(std::move(keys));
            return true;
        }
        std::string key = requireNamedString({"key"}, 1, "key");
        if (term.name == "json:has") {
            out = std::make_shared<StringExpr>(findMapValue(dataValue, key) ? "true" : "false");
            return true;
        }
        if (term.name == "json:set") {
            std::shared_ptr<Expr> value;
            if (!evalNamed("value", 2, value)) throw InterpreterError("json.set expects argument 'value'");
            upsertEntry(entries, key, cloneExprOrNil(value));
        } else {
            removeEntry(entries, key);
        }
        out = std::make_shared<MapExpr>(std::move(entries));
        return true;
    }

    if (term.name == "json:toText") {
        std::shared_ptr<Expr> dataValue;
        if (!evalNamed("data", 0, dataValue) && !evalNamed("value", 0, dataValue)) {
            throw InterpreterError("json.toText expects argument 'data'");
        }
        out = std::make_shared<StringExpr>(exprToJson(dataValue));
        return true;
    }

    if (term.name == "csv:addRow" || term.name == "csv:findRows" ||
        term.name == "csv:updateRows" || term.name == "csv:deleteRows") {
        std::shared_ptr<Expr> dataValue;
        if (!evalNamed("data", 0, dataValue) && !evalNamed("rows", 0, dataValue)) {
            throw InterpreterError(term.name + " expects array argument 'data'");
        }
        std::vector<std::shared_ptr<Expr>> rows;
        if (!exprAsArrayItems(dataValue, rows)) throw InterpreterError(term.name + " expects array argument 'data'");
        std::vector<std::shared_ptr<Expr>> resultRows;
        if (term.name == "csv:addRow") {
            std::shared_ptr<Expr> rowValue;
            if (!evalNamed("row", 1, rowValue) && !evalNamed("value", 1, rowValue)) {
                throw InterpreterError("csv.addRow expects map argument 'row'");
            }
            std::vector<MapEntry> rowEntries;
            if (!exprAsMapEntries(rowValue, rowEntries)) throw InterpreterError("csv.addRow expects map argument 'row'");
            resultRows = std::move(rows);
            resultRows.push_back(std::make_shared<MapExpr>(std::move(rowEntries)));
        } else {
            std::string key = requireNamedString({"key", "field"}, 1, "key");
            std::string expected = requireNamedString({"value"}, 2, "value");
            std::shared_ptr<Expr> patch;
            if (term.name == "csv:updateRows") {
                if (!evalNamed("patch", 3, patch)) throw InterpreterError("csv.updateRows expects map argument 'patch'");
                std::vector<MapEntry> patchEntries;
                if (!exprAsMapEntries(patch, patchEntries)) throw InterpreterError("csv.updateRows expects map argument 'patch'");
            }
            for (const auto& row : rows) {
                const bool match = mapFieldMatches(row, key, expected);
                if (term.name == "csv:findRows") {
                    if (match) resultRows.push_back(cloneExprOrNil(row));
                } else if (term.name == "csv:deleteRows") {
                    if (!match) resultRows.push_back(cloneExprOrNil(row));
                } else {
                    auto patched = match ? patchMapValue(row, patch) : cloneExprOrNil(row);
                    if (!patched) throw InterpreterError("csv.updateRows expects map rows in 'data'");
                    resultRows.push_back(patched);
                }
            }
        }
        out = std::make_shared<ArrayExpr>(std::move(resultRows));
        return true;
    }

    if (term.name == "visualize:dataJson" || term.name == "visualize:graphJson") {
        out = std::make_shared<StringExpr>(visualizeDataJson(optionalNamedBool({"loadImports", "imports"}, 0, false)));
        return true;
    }

    if (term.name == "visualize:dataHtml") {
        out = std::make_shared<StringExpr>(visualizeDataHtml(optionalNamedBool({"loadImports", "imports"}, 0, false)));
        return true;
    }

    if (term.name == "thread:createThread") {
        std::string functionName = requireNamedString({"function", "name"}, 0, "function");
        out = makeThreadHandle(createThreadTask(functionName));
        return true;
    }

    if (term.name == "thread:start" || term.name == "thread:status" || term.name == "thread:result") {
        std::shared_ptr<Expr> handle;
        if (!evalNamed("thread", 0, handle)) throw InterpreterError(term.name + " expects thread argument 'thread'");
        if (term.name == "thread:start") {
            out = std::make_shared<StringExpr>(startThreadTask(handle));
        } else if (term.name == "thread:status") {
            out = std::make_shared<StringExpr>(threadTaskStatus(handle));
        } else {
            out = threadTaskResult(handle);
        }
        return true;
    }

    if (term.name == "thread:pause" || term.name == "thread:stop") {
        throw InterpreterError(term.name + " is not supported; Felidae threads run immutable snapshots to completion");
    }

    if (term.name == "http:get" || term.name == "http:post" ||
        term.name == "http:put" || term.name == "http:delete") {
        std::string url = requireNamedString({"url"}, 0, "url");
        std::string body;
        std::string contentType = "text/plain";
        if (term.name == "http:post" || term.name == "http:put") {
            body = requireNamedString({"body", "data"}, 1, "body");
            if (const Arg* contentTypeArg = findTermArgByNameOrIndex(term, "contentType", 2)) {
                std::shared_ptr<Expr> contentTypeValue;
                if (!evalExprValue(contentTypeArg->value, env, contentTypeValue) ||
                    !argAsString(contentTypeValue, contentType)) {
                    throw InterpreterError(term.name + " expects string argument 'contentType'");
                }
            }
        }
        try {
            out = std::make_shared<StringExpr>(
                NativeHttp::request(term.name.substr(5), url, body, contentType));
        } catch (const NativeHttp::Error& ex) {
            throw InterpreterError(ex.what());
        }
        return true;
    }

    if (term.name == "http:serveStatic") {
        std::string host = requireNamedString({"host"}, 0, "host");
        double portNumber = requireNamedNumber({"port"}, 1, "port");
        if (std::floor(portNumber) != portNumber) throw InterpreterError("http.serveStatic expects integer port");
        std::string response = requireNamedString({"response", "body"}, 2, "response");
        std::string contentType = "text/plain";
        if (const Arg* contentTypeArg = findTermArgByNameOrIndex(term, "contentType", 3)) {
            std::shared_ptr<Expr> contentTypeValue;
            if (!evalExprValue(contentTypeArg->value, env, contentTypeValue) ||
                !argAsString(contentTypeValue, contentType)) {
                throw InterpreterError(term.name + " expects string argument 'contentType'");
            }
        }
        try {
            out = std::make_shared<StringExpr>(
                NativeHttp::serveStatic(host, static_cast<int>(portNumber), response, contentType));
        } catch (const NativeHttp::Error& ex) {
            throw InterpreterError(ex.what());
        }
        return true;
    }

    if (term.name == "process:platform") {
        out = std::make_shared<StringExpr>(NativeProcess::platform());
        return true;
    }

    if (term.name == "process:exec") {
        std::string command = requireNamedString({"command"}, 0, "command");
        try {
            out = std::make_shared<StringExpr>(NativeProcess::exec(command));
        } catch (const NativeProcess::Error& ex) {
            throw InterpreterError(ex.what());
        }
        return true;
    }

    if (term.name == "process:sleep") {
        double ms = requireNamedNumber({"milliseconds", "ms"}, 0, "milliseconds");
        if (std::floor(ms) != ms) throw InterpreterError("process.sleep expects integer milliseconds");
        try {
            out = std::make_shared<StringExpr>(NativeProcess::sleepMs(static_cast<int>(ms)));
        } catch (const NativeProcess::Error& ex) {
            throw InterpreterError(ex.what());
        }
        return true;
    }

    if (term.name.rfind("db:", 0) == 0) {
        const std::string op = term.name.substr(3);
        if (op == "types") {
            std::set<std::string> types;
            for (const auto& fact : memory_.facts()) types.insert(fact.type);
            for (const auto& parent : memory_.parents()) {
                types.insert(parent.first);
                types.insert(parent.second);
            }
            std::vector<std::shared_ptr<Expr>> items;
            for (const auto& type : types) items.push_back(std::make_shared<StringExpr>(type));
            out = std::make_shared<ArrayExpr>(std::move(items));
            return true;
        }

        std::string typeName = requireNamedString({"type", "fact"}, 0, "type");
        if (typeName.empty()) throw InterpreterError(term.name + " expects non-empty type");
        ensurePredicateLoaded(typeName);

        auto matchingFacts = [&]() {
            std::vector<std::shared_ptr<Expr>> rows;
            for (size_t factIndex : memory_.compatibleFactIndexes(typeName)) {
                rows.push_back(memory_.fact(factIndex).value->clone());
            }
            return rows;
        };

        if (op == "all") {
            out = std::make_shared<ArrayExpr>(matchingFacts());
            return true;
        }
        if (op == "count") {
            out = std::make_shared<NumberExpr>(static_cast<double>(matchingFacts().size()));
            return true;
        }
        if (op == "fields") {
            std::set<std::string> fields;
            for (const auto& row : matchingFacts()) {
                std::vector<MapEntry> entries;
                if (!exprAsMapEntries(row, entries)) continue;
                for (const auto& entry : entries) {
                    if (entry.key != "__type" && entry.key != "__parent") fields.insert(entry.key);
                }
            }
            std::vector<std::shared_ptr<Expr>> items;
            for (const auto& field : fields) items.push_back(std::make_shared<StringExpr>(field));
            out = std::make_shared<ArrayExpr>(std::move(items));
            return true;
        }
        if (op == "find" || op == "first") {
            std::string field = requireNamedString({"field", "key"}, 1, "field");
            std::shared_ptr<Expr> expected;
            if (!evalNamed("equals", 2, expected) && !evalNamed("value", 2, expected)) {
                throw InterpreterError(term.name + " expects argument 'equals'");
            }
            std::vector<std::shared_ptr<Expr>> rows;
            for (const auto& row : matchingFacts()) {
                auto actual = findMapValue(row, field);
                if (actual && exprContainsLiteral(actual, expected)) {
                    if (op == "first") {
                        out = row->clone();
                        return true;
                    }
                    rows.push_back(row->clone());
                }
            }
            if (op == "first") return false;
            out = std::make_shared<ArrayExpr>(std::move(rows));
            return true;
        }
        throw InterpreterError("Unknown db builtin: " + term.name);
    }

    if (term.name.rfind("math:", 0) == 0) {
        const std::string op = term.name.substr(5);
        if (op == "pi") {
            out = std::make_shared<NumberExpr>(3.14159265358979323846);
            return true;
        }
        if (op == "e") {
            out = std::make_shared<NumberExpr>(2.71828182845904523536);
            return true;
        }
        if (op == "random") {
            double min = term.args.empty() ? 0.0 : requireNamedNumber({"min"}, 0, "min");
            double max = term.args.size() < 2 ? 1.0 : requireNamedNumber({"max"}, 1, "max");
            if (max < min) throw InterpreterError("math.random expects max >= min");
            static thread_local std::mt19937 rng{std::random_device{}()};
            std::uniform_real_distribution<double> dist(min, max);
            out = std::make_shared<NumberExpr>(dist(rng));
            return true;
        }
        if (op == "pow" || op == "atan2") {
            double left = requireNamedNumber({"lhs", "left", "base", "y"}, 0, "lhs");
            double right = requireNamedNumber({"rhs", "right", "exponent", "x"}, 1, "rhs");
            out = std::make_shared<NumberExpr>(op == "pow" ? std::pow(left, right) : std::atan2(left, right));
            return true;
        }
        double value = requireNamedNumber({"value", "data", "x"}, 0, "value");
        if (op == "sqrt") out = std::make_shared<NumberExpr>(std::sqrt(value));
        else if (op == "sin") out = std::make_shared<NumberExpr>(std::sin(value));
        else if (op == "cos") out = std::make_shared<NumberExpr>(std::cos(value));
        else if (op == "tan") out = std::make_shared<NumberExpr>(std::tan(value));
        else if (op == "asin") out = std::make_shared<NumberExpr>(std::asin(value));
        else if (op == "acos") out = std::make_shared<NumberExpr>(std::acos(value));
        else if (op == "atan") out = std::make_shared<NumberExpr>(std::atan(value));
        else if (op == "log") out = std::make_shared<NumberExpr>(std::log(value));
        else if (op == "log10") out = std::make_shared<NumberExpr>(std::log10(value));
        else if (op == "exp") out = std::make_shared<NumberExpr>(std::exp(value));
        else if (op == "abs") out = std::make_shared<NumberExpr>(std::fabs(value));
        else if (op == "floor") out = std::make_shared<NumberExpr>(std::floor(value));
        else if (op == "ceil") out = std::make_shared<NumberExpr>(std::ceil(value));
        else if (op == "round") out = std::make_shared<NumberExpr>(std::round(value));
        else throw InterpreterError("Unknown math builtin: " + term.name);
        return true;
    }

    if (term.name.rfind("probability:", 0) == 0) {
        const std::string op = term.name.substr(12);
        static thread_local std::mt19937 rng{std::random_device{}()};

        auto requireProbability = [&](std::initializer_list<const char*> names, size_t index, const std::string& label) {
            double p = requireNamedNumber(names, index, label);
            if (p < 0.0 || p > 1.0) throw InterpreterError(term.name + " expects " + label + " between 0 and 1");
            return p;
        };

        if (op == "bernoulli") {
            double p = requireProbability({"p", "probability"}, 0, "p");
            std::bernoulli_distribution dist(p);
            out = std::make_shared<StringExpr>(dist(rng) ? "true" : "false");
            return true;
        }

        if (op == "binomialPmf" || op == "binomialCdf") {
            double trialsNumber = factorialTerm(requireNamedNumber({"trials", "n"}, 0, "trials"), term.name, "trials");
            double successesNumber = factorialTerm(requireNamedNumber({"successes", "k"}, 1, "successes"), term.name, "successes");
            double p = requireProbability({"p", "probability"}, 2, "p");
            int n = static_cast<int>(trialsNumber);
            int k = static_cast<int>(successesNumber);
            if (k > n) {
                out = std::make_shared<NumberExpr>(0.0);
                return true;
            }
            auto pmf = [&](int x) {
                double logComb = std::lgamma(n + 1.0) - std::lgamma(x + 1.0) - std::lgamma(n - x + 1.0);
                if (p == 0.0) return x == 0 ? 1.0 : 0.0;
                if (p == 1.0) return x == n ? 1.0 : 0.0;
                return std::exp(logComb + x * std::log(p) + (n - x) * std::log(1.0 - p));
            };
            double value = 0.0;
            if (op == "binomialPmf") {
                value = pmf(k);
            } else {
                for (int x = 0; x <= k; ++x) value += pmf(x);
            }
            out = std::make_shared<NumberExpr>(value);
            return true;
        }

        if (op == "poissonPmf" || op == "poissonCdf") {
            double lambda = requireNamedNumber({"lambda", "rate"}, 0, "lambda");
            double eventsNumber = factorialTerm(requireNamedNumber({"events", "k"}, 1, "events"), term.name, "events");
            if (lambda < 0.0) throw InterpreterError(term.name + " expects lambda >= 0");
            int k = static_cast<int>(eventsNumber);
            auto pmf = [&](int x) {
                if (lambda == 0.0) return x == 0 ? 1.0 : 0.0;
                return std::exp(x * std::log(lambda) - lambda - std::lgamma(x + 1.0));
            };
            double value = 0.0;
            if (op == "poissonPmf") {
                value = pmf(k);
            } else {
                for (int x = 0; x <= k; ++x) value += pmf(x);
            }
            out = std::make_shared<NumberExpr>(value);
            return true;
        }

        if (op == "normalPdf" || op == "normalCdf") {
            double x = requireNamedNumber({"x", "value"}, 0, "x");
            double mean = term.args.size() > 1 ? requireNamedNumber({"mean", "mu"}, 1, "mean") : 0.0;
            double stddev = term.args.size() > 2 ? requireNamedNumber({"stddev", "sigma"}, 2, "stddev") : 1.0;
            if (stddev <= 0.0) throw InterpreterError(term.name + " expects stddev > 0");
            double z = (x - mean) / stddev;
            double value = op == "normalPdf"
                ? std::exp(-0.5 * z * z) / (stddev * std::sqrt(2.0 * 3.14159265358979323846))
                : 0.5 * (1.0 + std::erf(z / std::sqrt(2.0)));
            out = std::make_shared<NumberExpr>(value);
            return true;
        }

        if (op == "uniformPdf" || op == "uniformCdf") {
            double x = requireNamedNumber({"x", "value"}, 0, "x");
            double min = requireNamedNumber({"min", "a"}, 1, "min");
            double max = requireNamedNumber({"max", "b"}, 2, "max");
            if (max <= min) throw InterpreterError(term.name + " expects max > min");
            double value = 0.0;
            if (op == "uniformPdf") {
                value = (x >= min && x <= max) ? 1.0 / (max - min) : 0.0;
            } else {
                value = x <= min ? 0.0 : (x >= max ? 1.0 : (x - min) / (max - min));
            }
            out = std::make_shared<NumberExpr>(value);
            return true;
        }

        std::shared_ptr<Expr> dataValue;
        if ((op == "mean" || op == "variance" || op == "stddev" || op == "normalize" || op == "entropy") &&
            (!evalNamed("data", 0, dataValue) && !evalNamed("values", 0, dataValue))) {
            throw InterpreterError(term.name + " expects numeric array argument 'data'");
        }

        if (op == "mean" || op == "variance" || op == "stddev" || op == "normalize" || op == "entropy") {
            auto values = requireNumberArray(dataValue, term.name, "data");
            if (values.empty()) throw InterpreterError(term.name + " expects a non-empty array");
            double total = 0.0;
            for (double value : values) total += value;
            if (op == "mean") {
                out = std::make_shared<NumberExpr>(total / static_cast<double>(values.size()));
                return true;
            }
            if (op == "variance" || op == "stddev") {
                double mean = total / static_cast<double>(values.size());
                double squared = 0.0;
                for (double value : values) squared += (value - mean) * (value - mean);
                double variance = squared / static_cast<double>(values.size());
                out = std::make_shared<NumberExpr>(op == "variance" ? variance : std::sqrt(variance));
                return true;
            }
            if (op == "normalize") {
                if (std::fabs(total) < 1e-12) throw InterpreterError("probability.normalize expects a non-zero sum");
                std::vector<double> normalized;
                normalized.reserve(values.size());
                for (double value : values) normalized.push_back(value / total);
                out = numbersToArray(normalized);
                return true;
            }
            double entropy = 0.0;
            for (double p : values) {
                if (p < 0.0) throw InterpreterError("probability.entropy expects non-negative probabilities");
                if (p > 0.0) entropy -= p * std::log2(p);
            }
            out = std::make_shared<NumberExpr>(entropy);
            return true;
        }

        if (op == "covariance" || op == "correlation") {
            std::shared_ptr<Expr> leftValue;
            std::shared_ptr<Expr> rightValue;
            if (!evalNamed("left", 0, leftValue) || !evalNamed("right", 1, rightValue)) {
                throw InterpreterError(term.name + " expects left and right arrays");
            }
            auto left = requireNumberArray(leftValue, term.name, "left");
            auto right = requireNumberArray(rightValue, term.name, "right");
            if (left.empty() || left.size() != right.size()) throw InterpreterError(term.name + " expects arrays of equal non-zero length");
            double leftMean = 0.0;
            double rightMean = 0.0;
            for (size_t i = 0; i < left.size(); ++i) {
                leftMean += left[i];
                rightMean += right[i];
            }
            leftMean /= static_cast<double>(left.size());
            rightMean /= static_cast<double>(right.size());
            double covariance = 0.0;
            double leftVar = 0.0;
            double rightVar = 0.0;
            for (size_t i = 0; i < left.size(); ++i) {
                double ld = left[i] - leftMean;
                double rd = right[i] - rightMean;
                covariance += ld * rd;
                leftVar += ld * ld;
                rightVar += rd * rd;
            }
            covariance /= static_cast<double>(left.size());
            if (op == "covariance") {
                out = std::make_shared<NumberExpr>(covariance);
                return true;
            }
            if (std::fabs(leftVar) < 1e-12 || std::fabs(rightVar) < 1e-12) {
                throw InterpreterError("probability.correlation expects non-constant arrays");
            }
            out = std::make_shared<NumberExpr>(covariance / std::sqrt((leftVar / left.size()) * (rightVar / right.size())));
            return true;
        }

        if (op == "sample" || op == "weightedChoice") {
            std::shared_ptr<Expr> valuesValue;
            if (!evalNamed("data", 0, valuesValue) && !evalNamed("values", 0, valuesValue)) {
                throw InterpreterError(term.name + " expects array argument 'data'");
            }
            std::vector<std::shared_ptr<Expr>> values;
            if (!exprAsArrayItems(valuesValue, values) || values.empty()) throw InterpreterError(term.name + " expects a non-empty array");
            if (op == "sample") {
                std::uniform_int_distribution<size_t> dist(0, values.size() - 1);
                out = values[dist(rng)]->clone();
                return true;
            }
            std::shared_ptr<Expr> weightsValue;
            if (!evalNamed("weights", 1, weightsValue)) throw InterpreterError("probability.weightedChoice expects weights");
            auto weights = requireNumberArray(weightsValue, term.name, "weights");
            if (weights.size() != values.size()) throw InterpreterError("probability.weightedChoice expects weights length to match data length");
            for (double weight : weights) {
                if (weight < 0.0) throw InterpreterError("probability.weightedChoice expects non-negative weights");
            }
            std::discrete_distribution<size_t> dist(weights.begin(), weights.end());
            out = values[dist(rng)]->clone();
            return true;
        }

        throw InterpreterError("Unknown probability builtin: " + term.name);
    }

    if (term.name.rfind("ml:", 0) == 0) {
        const std::string op = term.name.substr(3);
        if (op == "sigmoid" || op == "relu") {
            double value = requireNamedNumber({"value", "x"}, 0, "value");
            out = std::make_shared<NumberExpr>(op == "sigmoid" ? 1.0 / (1.0 + std::exp(-value)) : std::max(0.0, value));
            return true;
        }
        if (op == "dot" || op == "meanSquaredError") {
            std::shared_ptr<Expr> leftExpr;
            std::shared_ptr<Expr> rightExpr;
            if (!evalNamed("left", 0, leftExpr) || !evalNamed("right", 1, rightExpr)) {
                throw InterpreterError(term.name + " expects left and right arrays");
            }
            std::vector<std::shared_ptr<Expr>> left;
            std::vector<std::shared_ptr<Expr>> right;
            if (!exprAsArray(leftExpr, left) || !exprAsArray(rightExpr, right) || left.size() != right.size()) {
                throw InterpreterError(term.name + " expects arrays of equal length");
            }
            if (left.empty()) throw InterpreterError(term.name + " expects non-empty arrays");
#ifdef FELIDAE_HAS_EIGEN
            Eigen::VectorXd lhs(static_cast<Eigen::Index>(left.size()));
            Eigen::VectorXd rhs(static_cast<Eigen::Index>(right.size()));
            for (size_t i = 0; i < left.size(); ++i) {
                lhs(static_cast<Eigen::Index>(i)) = requireNumber(left[i], term.name, "left");
                rhs(static_cast<Eigen::Index>(i)) = requireNumber(right[i], term.name, "right");
            }
            if (op == "dot") {
                out = std::make_shared<NumberExpr>(lhs.dot(rhs));
            } else {
                out = std::make_shared<NumberExpr>((lhs - rhs).squaredNorm() / static_cast<double>(left.size()));
            }
#else
            double total = 0.0;
            for (size_t i = 0; i < left.size(); ++i) {
                double a = requireNumber(left[i], term.name, "left");
                double b = requireNumber(right[i], term.name, "right");
                total += op == "dot" ? a * b : (a - b) * (a - b);
            }
            out = std::make_shared<NumberExpr>(op == "dot" ? total : total / static_cast<double>(left.size()));
#endif
            return true;
        }
        throw InterpreterError("Unknown ml builtin: " + term.name);
    }

    if (term.name == "ParseDoc") {
        if (args.size() != 1) throw InterpreterError("ParseDoc expects one argument");
        std::string text;
        if (!argAsString(args[0], text)) throw InterpreterError("ParseDoc expects a string");
        out = std::make_shared<StringExpr>("Parsed: " + text);
        return true;
    }

    if (term.name == "count" || term.name == "length") {
        if (args.size() != 1) throw InterpreterError(term.name + " expects one argument");
        if (auto array = std::dynamic_pointer_cast<ArrayExpr>(args[0])) {
            out = std::make_shared<NumberExpr>(static_cast<double>(array->items.size()));
            return true;
        }
        std::string text;
        if (argAsString(args[0], text)) {
            out = std::make_shared<NumberExpr>(static_cast<double>(text.size()));
            return true;
        }
        auto items = arrayItems(args[0]);
        if (!items.empty()) {
            out = std::make_shared<NumberExpr>(static_cast<double>(items.size()));
            return true;
        }
        throw InterpreterError(term.name + " expects an array, collection, or string");
    }

    if (term.name == "sum" || term.name == "average") {
        if (args.size() != 1) throw InterpreterError(term.name + " expects one numeric array");
        auto array = std::dynamic_pointer_cast<ArrayExpr>(args[0]);
        if (!array) throw InterpreterError(term.name + " expects a numeric array");
        double total = 0.0;
        for (const auto& item : array->items) {
            double number = 0.0;
            if (!argAsNumber(item, number)) throw InterpreterError(term.name + " expects only numbers");
            total += number;
        }
        if (term.name == "average" && array->items.empty()) throw InterpreterError("average expects a non-empty array");
        out = std::make_shared<NumberExpr>(term.name == "average" ? total / array->items.size() : total);
        return true;
    }

    if (term.name == "min" || term.name == "max" || term.name == "sort") {
        if (args.size() != 1) throw InterpreterError(term.name + " expects one array");
        auto array = std::dynamic_pointer_cast<ArrayExpr>(args[0]);
        if (!array) throw InterpreterError(term.name + " expects an array");
        std::vector<std::shared_ptr<Expr>> items;
        for (const auto& item : array->items) items.push_back(item->clone());
        auto less = [](const std::shared_ptr<Expr>& lhs, const std::shared_ptr<Expr>& rhs) {
            double ln = 0.0, rn = 0.0;
            if (argAsNumber(lhs, ln) && argAsNumber(rhs, rn)) return ln < rn;
            std::string ls, rs;
            if (argAsString(lhs, ls) && argAsString(rhs, rs)) return ls < rs;
            return lhs->debug() < rhs->debug();
        };
        std::sort(items.begin(), items.end(), less);
        if (term.name == "sort") {
            out = std::make_shared<ArrayExpr>(std::move(items));
        } else {
            if (items.empty()) throw InterpreterError(term.name + " expects a non-empty array");
            out = term.name == "min" ? items.front() : items.back();
        }
        return true;
    }

    if (term.name == "contains" || term.name == "search") {
        if (args.size() != 2) throw InterpreterError(term.name + " expects two arguments");
        std::string query;
        if (!argAsString(args[1], query)) throw InterpreterError(term.name + " query must be a string");
        std::string text;
        if (argAsString(args[0], text)) {
            bool found = text.find(query) != std::string::npos;
            out = std::make_shared<StringExpr>(found ? "true" : "false");
            return true;
        }
        auto array = std::dynamic_pointer_cast<ArrayExpr>(args[0]);
        if (!array) throw InterpreterError(term.name + " expects a string or array");
        std::vector<std::shared_ptr<Expr>> matches;
        for (const auto& item : array->items) {
            std::string itemText;
            if (argAsString(item, itemText) && itemText.find(query) != std::string::npos) {
                if (term.name == "contains") {
                    out = std::make_shared<StringExpr>("true");
                    return true;
                }
                matches.push_back(item->clone());
            } else if (term.name == "contains" && exprEqualsLiteral(item, args[1])) {
                out = std::make_shared<StringExpr>("true");
                return true;
            }
        }
        out = term.name == "contains"
            ? std::static_pointer_cast<Expr>(std::make_shared<StringExpr>("false"))
            : std::static_pointer_cast<Expr>(std::make_shared<ArrayExpr>(std::move(matches)));
        return true;
    }

    if (term.name == "lower" || term.name == "upper") {
        if (args.size() != 1) throw InterpreterError(term.name + " expects one string");
        std::string text;
        if (!argAsString(args[0], text)) throw InterpreterError(term.name + " expects a string");
        out = std::make_shared<StringExpr>(term.name == "lower" ? lowerText(text) : upperText(text));
        return true;
    }

    return false;
}

bool Interpreter::evalExprValue(const std::shared_ptr<Expr>& expr, const Env& env, std::shared_ptr<Expr>& out) {
    auto resolved = resolveExpr(expr, env);
    if (auto lambda = std::dynamic_pointer_cast<LambdaExpr>(resolved)) {
        auto sourceValues = valuesForLambdaSource(lambda->source, env);
        std::vector<std::shared_ptr<Expr>> results;
        for (const auto& item : sourceValues) {
            Env lambdaEnv = env;
            lambdaEnv[lambda->variable] = item;
            if (!lambda->op.empty()) {
                std::shared_ptr<Expr> left;
                std::shared_ptr<Expr> right;
                if (!evalExprValue(lambda->body, lambdaEnv, left) ||
                    !evalExprValue(lambda->right, lambdaEnv, right)) {
                    continue;
                }
                bool ok = lambda->op == "==" ? unifyExpr(left, right, lambdaEnv) :
                          lambda->op == "!=" ? !unifyExpr(left, right, lambdaEnv) :
                          compareResolved(left, lambda->op, right);
                if (ok) results.push_back(item->clone());
                continue;
            }
            std::shared_ptr<Expr> mapped;
            if (evalExprValue(lambda->body, lambdaEnv, mapped) && !isMethodTruthTupleWithFalse(mapped)) {
                results.push_back(mapped);
            }
        }
        out = std::make_shared<ArrayExpr>(std::move(results));
        return true;
    }
    if (auto term = std::dynamic_pointer_cast<TermExpr>(resolved)) {
        return evalBuiltinTerm(*term, env, out);
    }
    if (auto binary = std::dynamic_pointer_cast<BinaryExpr>(resolved)) {
        std::shared_ptr<Expr> left;
        std::shared_ptr<Expr> right;
        if (!evalExprValue(binary->left, env, left) || !evalExprValue(binary->right, env, right)) {
            return false;
        }
        auto leftNumber = std::dynamic_pointer_cast<NumberExpr>(left);
        auto rightNumber = std::dynamic_pointer_cast<NumberExpr>(right);
        if (leftNumber && rightNumber) {
            if (binary->op == "+") {
                out = std::make_shared<NumberExpr>(leftNumber->value + rightNumber->value);
                return true;
            }
            if (binary->op == "-") {
                out = std::make_shared<NumberExpr>(leftNumber->value - rightNumber->value);
                return true;
            }
            if (binary->op == "*") {
                out = std::make_shared<NumberExpr>(leftNumber->value * rightNumber->value);
                return true;
            }
            if (binary->op == "/") {
                if (std::fabs(rightNumber->value) < 1e-12) {
                    throw InterpreterError("DivisionByZero");
                }
                out = std::make_shared<NumberExpr>(leftNumber->value / rightNumber->value);
                return true;
            }
        }
        throw InterpreterError("Operator '" + binary->op + "' expects numeric operands");
    }
    if (auto array = std::dynamic_pointer_cast<ArrayExpr>(resolved)) {
        std::vector<std::shared_ptr<Expr>> items;
        items.reserve(array->items.size());
        for (const auto& item : array->items) {
            std::shared_ptr<Expr> value;
            if (!evalExprValue(item, env, value)) return false;
            items.push_back(value);
        }
        out = std::make_shared<ArrayExpr>(std::move(items));
        return true;
    }
    if (auto map = std::dynamic_pointer_cast<MapExpr>(resolved)) {
        std::vector<MapEntry> entries;
        entries.reserve(map->entries.size());
        for (const auto& entry : map->entries) {
            std::shared_ptr<Expr> value;
            if (!evalExprValue(entry.value, env, value)) return false;
            entries.push_back(MapEntry{entry.key, value});
        }
        out = std::make_shared<MapExpr>(std::move(entries));
        return true;
    }
    if (auto access = std::dynamic_pointer_cast<AccessExpr>(resolved)) {
        std::shared_ptr<Expr> target;
        if (!evalExprValue(access->target, env, target)) return false;
        auto value = findMapValue(target, access->key);
        if (!value) return false;
        return evalExprValue(value, env, out);
    }
    if (auto var = std::dynamic_pointer_cast<VarExpr>(resolved)) {
        auto globalIt = globals_.find(var->name);
        if (globalIt == globals_.end()) return false;
        return evalExprValue(globalIt->second, env, out);
    }
    out = resolved->clone();
    return true;
}

bool Interpreter::compareResolved(const std::shared_ptr<Expr>& left,
                                  const std::string& op,
                                  const std::shared_ptr<Expr>& right) const {
    auto ln = std::dynamic_pointer_cast<NumberExpr>(left);
    auto rn = std::dynamic_pointer_cast<NumberExpr>(right);
    if (ln && rn) {
        if (op == "<") return ln->value < rn->value;
        if (op == "<=") return ln->value <= rn->value;
        if (op == ">") return ln->value > rn->value;
        if (op == ">=") return ln->value >= rn->value;
        return false;
    }

    auto ls = std::dynamic_pointer_cast<StringExpr>(left);
    auto rs = std::dynamic_pointer_cast<StringExpr>(right);
    if (ls && rs) {
        if (op == "<") return ls->value < rs->value;
        if (op == "<=") return ls->value <= rs->value;
        if (op == ">") return ls->value > rs->value;
        if (op == ">=") return ls->value >= rs->value;
        return false;
    }

    return false;
}

bool Interpreter::unifyCall(const Call& goal, const Call& head, Env& env) {
    if (goal.name != head.name) return false;

    bool headHasNamed = false;
    for (const auto& a : head.args) if (!a.name.empty()) headHasNamed = true;

    if (goal.args.size() == 1 && goal.args[0].name.empty() && headHasNamed) return false;

    // This supports partial named matching. A goal can specify only the fields it cares about:
    // Employee(name: X) can match Employee(name: "Alice", role: "Engineer").
    for (size_t i = 0; i < goal.args.size(); ++i) {
        const Arg& goalArg = goal.args[i];
        const Arg* headArg = findArg(head, goalArg, i);
        if (!headArg) return false;
        if (!unifyExpr(goalArg.value, headArg->value, env)) return false;
    }

    // For positional predicates, arity should match. For named predicates, partial matching is allowed.
    bool goalHasNamed = false;
    for (const auto& a : goal.args) if (!a.name.empty()) goalHasNamed = true;
    if (!goalHasNamed && !headHasNamed && goal.args.size() != head.args.size()) return false;

    return true;
}

std::vector<Env> Interpreter::unifyCallAlternatives(const Call& goal, const Call& head, const Env& env) {
    if (goal.name != head.name) return {};

    bool headHasNamed = false;
    bool goalHasNamed = false;
    for (const auto& arg : head.args) if (!arg.name.empty()) headHasNamed = true;
    for (const auto& arg : goal.args) if (!arg.name.empty()) goalHasNamed = true;

    if (goal.args.size() == 1 && goal.args[0].name.empty() && headHasNamed) return {};
    if (!goalHasNamed && !headHasNamed && goal.args.size() != head.args.size()) return {};

    std::vector<Env> states{env};
    for (size_t i = 0; i < goal.args.size(); ++i) {
        const Arg& goalArg = goal.args[i];
        std::vector<const Arg*> candidates;
        if (!goalArg.name.empty()) {
            for (const auto& headArg : head.args) {
                if (headArg.name == goalArg.name) candidates.push_back(&headArg);
            }
            if (candidates.empty() && i < head.args.size() && head.args[i].name.empty()) {
                candidates.push_back(&head.args[i]);
            }
        } else if (i < head.args.size()) {
            candidates.push_back(&head.args[i]);
        }
        if (candidates.empty()) return {};

        std::vector<Env> nextStates;
        for (const auto& state : states) {
            for (const Arg* candidate : candidates) {
                Env attempt = state;
                if (unifyExpr(goalArg.value, candidate->value, attempt)) {
                    nextStates.push_back(std::move(attempt));
                }
            }
        }
        states = std::move(nextStates);
        if (states.empty()) return {};
    }
    return states;
}

const Arg* Interpreter::findArg(const Call& call, const Arg& wanted, size_t index) const {
    if (!wanted.name.empty()) {
        for (const auto& a : call.args) {
            if (a.name == wanted.name) return &a;
        }
        if (index < call.args.size() && call.args[index].name.empty()) return &call.args[index];
        return nullptr;
    }
    if (index < call.args.size()) return &call.args[index];
    return nullptr;
}

bool Interpreter::unifyExpr(const std::shared_ptr<Expr>& a,
                            const std::shared_ptr<Expr>& b,
                            Env& env) {
    auto ra = resolveExpr(a, env);
    auto rb = resolveExpr(b, env);

    if (isSameVariable(ra, rb)) return true;

    if (auto va = std::dynamic_pointer_cast<VarExpr>(ra)) {
        if (va->name.rfind("__anon", 0) == 0) return true;
    }
    if (auto vb = std::dynamic_pointer_cast<VarExpr>(rb)) {
        if (vb->name.rfind("__anon", 0) == 0) return true;
    }

    if (auto va = std::dynamic_pointer_cast<VarExpr>(ra)) {
        std::shared_ptr<Expr> value;
        env[va->name] = evalExprValue(rb, env, value) ? value : rb->clone();
        return true;
    }
    if (auto vb = std::dynamic_pointer_cast<VarExpr>(rb)) {
        std::shared_ptr<Expr> value;
        env[vb->name] = evalExprValue(ra, env, value) ? value : ra->clone();
        return true;
    }

    if (auto sa = std::dynamic_pointer_cast<StringExpr>(ra)) {
        auto sb = std::dynamic_pointer_cast<StringExpr>(rb);
        return sb && sa->value == sb->value;
    }
    if (auto na = std::dynamic_pointer_cast<NumberExpr>(ra)) {
        auto nb = std::dynamic_pointer_cast<NumberExpr>(rb);
        return nb && std::fabs(na->value - nb->value) < 1e-12;
    }
    if (std::dynamic_pointer_cast<NilExpr>(ra) || std::dynamic_pointer_cast<NilExpr>(rb)) {
        return static_cast<bool>(std::dynamic_pointer_cast<NilExpr>(ra)) &&
               static_cast<bool>(std::dynamic_pointer_cast<NilExpr>(rb));
    }
    if (auto ta = std::dynamic_pointer_cast<TermExpr>(ra)) {
        auto tb = std::dynamic_pointer_cast<TermExpr>(rb);
        if (!tb || ta->name != tb->name || ta->args.size() != tb->args.size()) return false;
        for (size_t i = 0; i < ta->args.size(); ++i) {
            if (!unifyExpr(ta->args[i].value, tb->args[i].value, env)) return false;
        }
        return true;
    }
    if (auto aa = std::dynamic_pointer_cast<ArrayExpr>(ra)) {
        auto ab = std::dynamic_pointer_cast<ArrayExpr>(rb);
        if (!ab || aa->items.size() != ab->items.size()) return false;
        for (size_t i = 0; i < aa->items.size(); ++i) {
            if (!unifyExpr(aa->items[i], ab->items[i], env)) return false;
        }
        return true;
    }
    if (auto ma = std::dynamic_pointer_cast<MapExpr>(ra)) {
        auto mb = std::dynamic_pointer_cast<MapExpr>(rb);
        if (!mb || ma->entries.size() != mb->entries.size()) return false;
        for (const auto& entry : ma->entries) {
            auto other = findMapValue(rb, entry.key);
            if (!other || !unifyExpr(entry.value, other, env)) return false;
        }
        return true;
    }

    return false;
}

std::shared_ptr<Expr> Interpreter::resolveExpr(const std::shared_ptr<Expr>& expr, const Env& env) const {
    auto var = std::dynamic_pointer_cast<VarExpr>(expr);
    if (!var) {
        if (auto access = std::dynamic_pointer_cast<AccessExpr>(expr)) {
            std::shared_ptr<Expr> target;
            if (!const_cast<Interpreter*>(this)->evalExprValue(access->target, env, target)) return expr;
            auto value = findMapValue(target, access->key);
            if (!value) return expr;
            return resolveExpr(value, env);
        }
        return expr;
    }

    auto it = env.find(var->name);
    if (it == env.end()) {
        auto globalIt = globals_.find(var->name);
        if (globalIt != globals_.end()) return resolveExpr(globalIt->second, env);
        return expr;
    }

    // Follow variable aliases: X -> __r1_name -> "Alice".
    return resolveExpr(it->second, env);
}

std::string Interpreter::exprToString(const std::shared_ptr<Expr>& expr, const Env& env) const {
    auto resolved = resolveExpr(expr, env);
    std::shared_ptr<Expr> value;
    if (const_cast<Interpreter*>(this)->evalExprValue(resolved, env, value)) {
        return value->debug();
    }
    return resolved->debug();
}

std::shared_ptr<ClauseStmt> Interpreter::standardizeApart(const ClauseStmt& clause) {
    std::string prefix = "__r" + std::to_string(++renameCounter_) + "_";
    Call head;
    head.name = clause.head.name;
    bool methodHead = isMethodClause(clause);
    for (const auto& arg : clause.head.args) {
        auto typeExpr = std::dynamic_pointer_cast<VarExpr>(arg.value);
        if (methodHead && typeExpr && isTypeAnnotationName(typeExpr->name)) {
            head.args.push_back(Arg{arg.name, arg.value->clone()});
        } else {
            head.args.push_back(Arg{arg.name, renameExpr(arg.value, prefix)});
        }
    }
    std::vector<std::shared_ptr<Goal>> body;
    for (const auto& g : clause.body) body.push_back(renameGoal(g, prefix));
    std::vector<std::vector<std::shared_ptr<Goal>>> fallbackBranches;
    fallbackBranches.reserve(clause.fallbackBranches.size());
    for (const auto& branch : clause.fallbackBranches) {
        std::vector<std::shared_ptr<Goal>> renamedBranch;
        renamedBranch.reserve(branch.size());
        for (const auto& goal : branch) renamedBranch.push_back(renameGoal(goal, prefix));
        fallbackBranches.push_back(std::move(renamedBranch));
    }
    return std::make_shared<ClauseStmt>(std::move(head), clause.parentName, std::move(body), std::move(fallbackBranches));
}

Call Interpreter::renameCall(const Call& call, const std::string& prefix) {
    Call out;
    out.name = call.name;
    for (const auto& a : call.args) {
        if ((call.name == "throw" && a.name == "target") ||
            (call.name == "instanceof" && (a.name == "type" || a.name == "parent" || a.name == "of"))) {
            out.args.push_back(Arg{a.name, a.value->clone()});
            continue;
        }
        out.args.push_back(Arg{a.name, renameExpr(a.value, prefix)});
    }
    return out;
}

std::shared_ptr<Goal> Interpreter::renameGoal(const std::shared_ptr<Goal>& goal, const std::string& prefix) {
    if (auto cg = std::dynamic_pointer_cast<CallGoal>(goal)) {
        return std::make_shared<CallGoal>(renameCall(cg->call, prefix));
    }
    if (auto ag = std::dynamic_pointer_cast<AssignGoal>(goal)) {
        if (ag->goal) return std::make_shared<AssignGoal>(prefix + ag->name, renameGoal(ag->goal, prefix));
        return std::make_shared<AssignGoal>(prefix + ag->name, renameExpr(ag->expr, prefix));
    }
    if (auto mag = std::dynamic_pointer_cast<MultiAssignGoal>(goal)) {
        std::vector<AssignmentTarget> targets;
        targets.reserve(mag->targets.size());
        for (const auto& target : mag->targets) {
            targets.push_back(AssignmentTarget{prefix + target.name, target.type});
        }
        return std::make_shared<MultiAssignGoal>(std::move(targets), renameExpr(mag->expr, prefix));
    }
    if (auto bg = std::dynamic_pointer_cast<BinaryGoal>(goal)) {
        return std::make_shared<BinaryGoal>(renameExpr(bg->left, prefix), bg->op, renameExpr(bg->right, prefix));
    }
    if (auto wg = std::dynamic_pointer_cast<WhereGoal>(goal)) {
        return std::make_shared<WhereGoal>(renameGoal(wg->condition, prefix));
    }
    if (auto rg = std::dynamic_pointer_cast<ReturnGoal>(goal)) {
        std::vector<Arg> fields;
        fields.reserve(rg->fields.size());
        for (const auto& field : rg->fields) fields.push_back(Arg{field.name, renameExpr(field.value, prefix)});
        return std::make_shared<ReturnGoal>(std::move(fields));
    }
    if (auto gg = std::dynamic_pointer_cast<GroupGoal>(goal)) {
        std::vector<std::shared_ptr<Goal>> goals;
        goals.reserve(gg->goals.size());
        for (const auto& groupedGoal : gg->goals) goals.push_back(renameGoal(groupedGoal, prefix));
        return std::make_shared<GroupGoal>(std::move(goals));
    }
    if (auto og = std::dynamic_pointer_cast<OrGoal>(goal)) {
        std::vector<std::vector<std::shared_ptr<Goal>>> branches;
        branches.reserve(og->branches.size());
        for (const auto& branch : og->branches) {
            std::vector<std::shared_ptr<Goal>> renamedBranch;
            renamedBranch.reserve(branch.size());
            for (const auto& branchGoal : branch) renamedBranch.push_back(renameGoal(branchGoal, prefix));
            branches.push_back(std::move(renamedBranch));
        }
        return std::make_shared<OrGoal>(std::move(branches));
    }
    throw InterpreterError("Unknown goal while renaming");
}

std::shared_ptr<Expr> Interpreter::renameExpr(const std::shared_ptr<Expr>& expr, const std::string& prefix) {
    if (auto v = std::dynamic_pointer_cast<VarExpr>(expr)) {
        if (globals_.count(v->name) > 0) return v->clone();
        return std::make_shared<VarExpr>(prefix + v->name);
    }
    if (auto term = std::dynamic_pointer_cast<TermExpr>(expr)) {
        std::vector<Arg> args;
        args.reserve(term->args.size());
        for (const auto& arg : term->args) args.push_back(Arg{arg.name, renameExpr(arg.value, prefix)});
        return std::make_shared<TermExpr>(term->name, std::move(args));
    }
    if (auto array = std::dynamic_pointer_cast<ArrayExpr>(expr)) {
        std::vector<std::shared_ptr<Expr>> items;
        items.reserve(array->items.size());
        for (const auto& item : array->items) items.push_back(renameExpr(item, prefix));
        return std::make_shared<ArrayExpr>(std::move(items));
    }
    if (auto map = std::dynamic_pointer_cast<MapExpr>(expr)) {
        std::vector<MapEntry> entries;
        entries.reserve(map->entries.size());
        for (const auto& entry : map->entries) {
            entries.push_back(MapEntry{entry.key, renameExpr(entry.value, prefix)});
        }
        return std::make_shared<MapExpr>(std::move(entries));
    }
    if (auto access = std::dynamic_pointer_cast<AccessExpr>(expr)) {
        return std::make_shared<AccessExpr>(renameExpr(access->target, prefix), access->key);
    }
    if (auto lambda = std::dynamic_pointer_cast<LambdaExpr>(expr)) {
        return std::make_shared<LambdaExpr>(
            renameExpr(lambda->source, prefix),
            prefix + lambda->variable,
            renameExpr(lambda->body, prefix),
            lambda->op,
            lambda->right ? renameExpr(lambda->right, prefix) : nullptr);
    }
    if (auto binary = std::dynamic_pointer_cast<BinaryExpr>(expr)) {
        return std::make_shared<BinaryExpr>(
            renameExpr(binary->left, prefix),
            binary->op,
            renameExpr(binary->right, prefix));
    }
    return expr->clone();
}

bool Interpreter::isSameVariable(const std::shared_ptr<Expr>& a, const std::shared_ptr<Expr>& b) const {
    auto va = std::dynamic_pointer_cast<VarExpr>(a);
    auto vb = std::dynamic_pointer_cast<VarExpr>(b);
    return va && vb && va->name == vb->name;
}

bool Interpreter::isGroundLiteral(const std::shared_ptr<Expr>& expr) const {
    return static_cast<bool>(std::dynamic_pointer_cast<StringExpr>(expr)) ||
           static_cast<bool>(std::dynamic_pointer_cast<NumberExpr>(expr)) ||
           static_cast<bool>(std::dynamic_pointer_cast<NilExpr>(expr));
}

bool Interpreter::isBuiltinFunctionName(const std::string& name) const {
    static const std::set<std::string> names = {
        "count", "sum", "average", "min", "max", "sort", "search", "contains",
        "lower", "upper", "length", "ParseDoc",
        "str:len", "str:contains", "str:concat", "str:lower", "str:upper",
        "str:trim", "str:split", "str:replace", "str:startsWith", "str:endsWith",
        "console:readLine", "console:writeLine", "console:write", "system:print",
        "file:readFile", "file:readLines", "file:readLine", "file:writeFile", "file:writeLines", "file:appendFile", "file:exists", "file:deleteFile",
        "csv:parse", "csv:toFacts", "csv:toText", "csv:toFelidaeFacts",
        "csv:addRow", "csv:findRows", "csv:updateRows", "csv:deleteRows",
        "db:all", "db:find", "db:count", "db:first", "db:types", "db:fields",
        "json:parse", "json:get", "json:has", "json:keys", "json:set", "json:remove", "json:toText",
        "visualize:dataJson", "visualize:dataHtml", "visualize:graphJson",
        "thread:createThread", "thread:start", "thread:pause", "thread:stop", "thread:status", "thread:result",
        "http:get", "http:post", "http:put", "http:delete", "http:serveStatic",
        "process:platform", "process:exec", "process:sleep",
        "math:pi", "math:e", "math:random", "math:pow", "math:atan2",
        "math:sqrt", "math:sin", "math:cos", "math:tan", "math:asin", "math:acos", "math:atan",
        "math:log", "math:log10", "math:exp", "math:abs", "math:floor", "math:ceil", "math:round",
        "probability:mean", "probability:variance", "probability:stddev", "probability:normalize",
        "probability:entropy", "probability:covariance", "probability:correlation",
        "probability:bernoulli", "probability:binomialPmf", "probability:binomialCdf",
        "probability:poissonPmf", "probability:poissonCdf", "probability:normalPdf",
        "probability:normalCdf", "probability:uniformPdf", "probability:uniformCdf",
        "probability:sample", "probability:weightedChoice",
        "ml:sigmoid", "ml:relu", "ml:dot", "ml:meanSquaredError"
    };
    return names.count(name) > 0;
}

bool Interpreter::isMethodClause(const ClauseStmt& clause) const {
    if (clause.emptyDeclaration) return true;
    if (!clause.fallbackBranches.empty()) return true;
    if (clause.body.empty()) return false;
    if (clause.head.name == "main") return true;
    bool hasReturn = false;
    for (const auto& goal : clause.body) {
        if (std::dynamic_pointer_cast<ReturnGoal>(goal)) {
            hasReturn = true;
            break;
        }
    }
    for (const auto& arg : clause.head.args) {
        if (!arg.name.empty()) {
            auto typeExpr = std::dynamic_pointer_cast<VarExpr>(arg.value);
            if (typeExpr && (isTypeAnnotationName(typeExpr->name) || typeExpr->name != arg.name)) {
                return true;
            }
        }
    }
    if (!hasReturn) return false;
    if (clause.head.args.empty()) return true;
    for (const auto& arg : clause.head.args) {
        auto typeExpr = std::dynamic_pointer_cast<VarExpr>(arg.value);
        if (!typeExpr || !isTypeAnnotationName(typeExpr->name)) {
            return false;
        }
    }
    return true;
}

std::shared_ptr<MapExpr> Interpreter::factToMap(const ClauseStmt& clause, const std::string& parentType) {
    std::vector<MapEntry> entries;
    if (!parentType.empty()) {
        std::set<std::string> seen;
        std::string current = parentType;
        while (!current.empty()) {
            if (seen.count(current)) {
                throw InterpreterError("Inheritance cycle detected for " + clause.head.name);
            }
            seen.insert(current);
            const auto& facts = memory_.facts();
            auto parent = std::find_if(facts.begin(), facts.end(), [&](const FactRecord& fact) {
                return fact.type == current;
            });
            if (parent == facts.end()) {
                throw InterpreterError("Unknown parent fact/type '" + current + "'");
            }
            entries = cloneEntries(parent->value->entries);
            current.clear();
        }
    }
    upsertEntry(entries, "__type", std::make_shared<StringExpr>(clause.head.name));
    if (!parentType.empty()) upsertEntry(entries, "__parent", std::make_shared<StringExpr>(parentType));
    Env env;
    for (const auto& arg : clause.head.args) {
        std::shared_ptr<Expr> value;
        if (!evalExprValue(arg.value, env, value)) {
            throw InterpreterError("Cannot evaluate fact field '" + arg.name + "' for " + clause.head.name);
        }
        appendFactEntry(entries, arg.name, value->clone());
    }
    return std::make_shared<MapExpr>(std::move(entries));
}

std::vector<std::shared_ptr<Expr>> Interpreter::valuesForLambdaSource(const std::shared_ptr<Expr>& source,
                                                                      const Env& env) {
    auto var = std::dynamic_pointer_cast<VarExpr>(source);
    if (var) {
        auto globalIt = globals_.find(var->name);
        if (globalIt != globals_.end()) {
            std::shared_ptr<Expr> value;
            if (!evalExprValue(globalIt->second, env, value)) return {};
            if (auto array = std::dynamic_pointer_cast<ArrayExpr>(value)) return array->items;
            return {value};
        }
    }
    if (var && !var->name.empty() && std::isupper(static_cast<unsigned char>(var->name.front()))) {
        ensurePredicateLoaded(var->name);
        std::vector<std::shared_ptr<Expr>> values;
        for (size_t factIndex : memory_.compatibleFactIndexes(var->name)) {
            values.push_back(memory_.fact(factIndex).value);
        }
        return values;
    }

    std::shared_ptr<Expr> value;
    if (!evalExprValue(source, env, value)) return {};
    if (auto array = std::dynamic_pointer_cast<ArrayExpr>(value)) return array->items;
    return {value};
}

const Arg* Interpreter::findArgByNameOrIndex(const Call& call, const std::string& name, size_t index) const {
    if (!name.empty()) {
        for (const auto& arg : call.args) {
            if (arg.name == name) return &arg;
        }
    }
    if (index < call.args.size()) return &call.args[index];
    return nullptr;
}

std::string Interpreter::solveCacheKey(const std::vector<std::shared_ptr<Goal>>& goals,
                                       size_t maxSolutions) const {
    std::ostringstream out;
    out << maxSolutions << '|';
    for (const auto& goal : goals) {
        out << goal->debug() << ';';
    }
    return out.str();
}

void Interpreter::invalidateCaches() {
    memory_.invalidateCaches();
    solveCache_.clear();
}

bool Interpreter::ensurePredicateLoaded(const std::string& predicate) {
    bool loadedAny = false;
    for (size_t index = 0; index < lazyModules_.size(); ++index) {
        if (lazyModules_[index].loaded) continue;
        loadLazyModule(lazyModules_[index]);
        loadedAny = true;
        if (clauses_.find(predicate) != clauses_.end()) return true;
    }
    return loadedAny && clauses_.find(predicate) != clauses_.end();
}

void Interpreter::loadAllImports() {
    for (size_t index = 0; index < lazyModules_.size(); ++index) {
        if (!lazyModules_[index].loaded) loadLazyModule(lazyModules_[index]);
    }
}

void Interpreter::touchClauses(const std::vector<std::shared_ptr<ClauseStmt>>& clauses) {
    if (clauses.empty()) return;
    auto originIt = clauseOrigins_.find(clauses.front().get());
    if (originIt == clauseOrigins_.end()) return;
    for (auto& module : lazyModules_) {
        for (const auto& file : module.files) {
            if (file == originIt->second) {
                module.useCount++;
                module.lastUsed = solveEpoch_;
                return;
            }
        }
    }
}

void Interpreter::evictColdModules() {
    // Facts, globals, and type hierarchy entries loaded from modules are currently
    // shared runtime state. Evicting only clauses would leave stale facts/globals
    // behind and reloading the file could duplicate them. Keep modules resident
    // until fact/global origin tracking is added for whole-module eviction.
}

void Interpreter::loadLazyModule(LazyModule& module) {
    std::vector<fs::path> files = module.files;
    fs::path nativeLibrary = module.nativeLibrary;
    module.loaded = true;
    for (const auto& file : files) {
        loadProgramFile(file);
    }
    if (!nativeLibrary.empty()) {
        loadNativeLibrary(nativeLibrary);
    }
}

void Interpreter::loadProgramFile(const std::filesystem::path& file) {
    fs::path normalized = fs::absolute(file).lexically_normal();
    if (loadedFiles_.count(normalized)) return;
    loadedFiles_.insert(normalized);

    Program program = parseProgramFile(normalized);
    fs::path baseDir = normalized.parent_path();

    for (const auto& stmt : program.statements) {
        if (auto imp = std::dynamic_pointer_cast<ImportStmt>(stmt)) {
            for (const auto& path : imp->paths) addLazyImport(baseDir, path);
        }
    }
    fs::path previous = currentLoadingFile_;
    currentLoadingFile_ = normalized;
    addProgram(program);
    currentLoadingFile_ = previous;
}

std::vector<std::filesystem::path> Interpreter::expandImportPattern(const std::filesystem::path& baseDir,
                                                                    const std::string& pattern) const {
    std::vector<fs::path> files;
    if (isBareModuleImport(pattern)) {
        fs::path coreFile = resolveCoreImport(baseDir, pattern);
        if (fs::exists(coreFile) && fs::is_regular_file(coreFile)) {
            files.push_back(coreFile);
            return files;
        }
        fs::path nativeFile = resolveNativeImport(baseDir, pattern);
        if (!nativeFile.empty()) return files;
        std::ostringstream message;
        message << "Module '" << pattern << "' not found. Looked for "
                << coreFile.string() << " or native library files";
        for (const auto& fileName : nativeLibraryFileNames(pattern)) message << " " << fileName;
        throw InterpreterError(message.str());
    }

    fs::path raw(pattern);
    fs::path target = raw.is_absolute() ? raw : (baseDir / raw);
    target = fs::absolute(target).lexically_normal();

    if (pattern.size() >= 2 && pattern.substr(pattern.size() - 2) == "/*") {
        fs::path dir = fs::absolute(baseDir / pattern.substr(0, pattern.size() - 2)).lexically_normal();
        if (!fs::exists(dir) || !fs::is_directory(dir)) {
            throw InterpreterError("Import directory not found: " + dir.string());
        }
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".fx") {
                files.push_back(entry.path());
            }
        }
        return files;
    }

    if (fs::exists(target) && fs::is_directory(target)) {
        for (const auto& entry : fs::recursive_directory_iterator(target)) {
            if (entry.is_regular_file() && entry.path().extension() == ".fx") {
                files.push_back(entry.path());
            }
        }
        return files;
    }

    if (!fs::exists(target)) {
        fs::path nativeFile = resolveNativeImport(baseDir, pattern);
        if (!nativeFile.empty()) return files;
        throw InterpreterError("Import file not found: " + target.string());
    }
    if (fs::is_regular_file(target) && hasNativeLibraryExtension(target)) {
        return files;
    }
    files.push_back(target);
    return files;
}

std::filesystem::path Interpreter::resolveNativeImport(const std::filesystem::path& baseDir,
                                                       const std::string& pattern) const {
    fs::path raw(pattern);
    std::vector<fs::path> candidates;

    auto addCandidate = [&](const fs::path& candidate) {
        candidates.push_back(fs::absolute(candidate).lexically_normal());
    };

    if (raw.has_extension() && hasNativeLibraryExtension(raw)) {
        addCandidate(raw.is_absolute() ? raw : (baseDir / raw));
    }

    fs::path root = sourceRootFromBase(baseDir);
    std::string moduleName = raw.stem().string();
    if (isBareModuleImport(pattern)) {
        moduleName = pattern;
    }
    if (!moduleName.empty() && moduleName.find('*') == std::string::npos) {
        for (const auto& fileName : nativeLibraryFileNames(moduleName)) {
            addCandidate(baseDir / fileName);
            addCandidate(baseDir / "native_modules" / moduleName / fileName);
            addCandidate(root / "native_modules" / moduleName / fileName);
            addCandidate(root / "modules" / moduleName / fileName);
        }
    }

    for (const auto& candidate : candidates) {
        if (fs::exists(candidate) && fs::is_regular_file(candidate)) return candidate;
    }

    return {};
}

std::string Interpreter::runtimeGraphJson() const {
    std::ostringstream out;
    bool firstNode = true;
    bool firstEdge = true;
    std::set<std::string> emittedEdges;
    std::set<std::string> methodCallNodes;
    std::set<std::string> factCallNodes;
    std::set<std::string> libraryCallNodes;

    auto nodeId = [](const std::string& kind, const std::string& name) {
        return kind + ":" + name;
    };

    auto hasNonFactClause = [](const std::vector<std::shared_ptr<ClauseStmt>>& clauses) {
        return std::any_of(clauses.begin(), clauses.end(), [](const auto& clause) {
            return clause && !clause->isFact();
        });
    };

    auto isMethodName = [&](const std::string& name) {
        auto found = clauses_.find(name);
        return found != clauses_.end() && hasNonFactClause(found->second);
    };

    auto classifyCall = [&](const std::string& name) {
        if (isMethodName(name)) return std::string("method");
        if (isBuiltinFunctionName(name)) return std::string("library");
        return std::string("fact");
    };

    auto rememberCall = [&](const std::string& name) {
        auto kind = classifyCall(name);
        if (kind == "method") methodCallNodes.insert(name);
        else if (kind == "library") libraryCallNodes.insert(name);
        else factCallNodes.insert(name);
    };

    std::function<void(const std::shared_ptr<Expr>&)> collectExprCalls;
    collectExprCalls = [&](const std::shared_ptr<Expr>& expr) {
        if (!expr) return;
        if (auto term = std::dynamic_pointer_cast<TermExpr>(expr)) {
            rememberCall(term->name);
            for (const auto& arg : term->args) collectExprCalls(arg.value);
            return;
        }
        if (auto lambda = std::dynamic_pointer_cast<LambdaExpr>(expr)) {
            collectExprCalls(lambda->source);
            collectExprCalls(lambda->body);
            collectExprCalls(lambda->right);
            return;
        }
        if (auto binary = std::dynamic_pointer_cast<BinaryExpr>(expr)) {
            collectExprCalls(binary->left);
            collectExprCalls(binary->right);
            return;
        }
        if (auto array = std::dynamic_pointer_cast<ArrayExpr>(expr)) {
            for (const auto& item : array->items) collectExprCalls(item);
            return;
        }
        if (auto map = std::dynamic_pointer_cast<MapExpr>(expr)) {
            for (const auto& entry : map->entries) collectExprCalls(entry.value);
            return;
        }
        if (auto access = std::dynamic_pointer_cast<AccessExpr>(expr)) {
            collectExprCalls(access->target);
        }
    };

    std::function<void(const std::shared_ptr<Goal>&)> collectGoalCalls;
    collectGoalCalls = [&](const std::shared_ptr<Goal>& goal) {
        if (!goal) return;
        if (auto call = std::dynamic_pointer_cast<CallGoal>(goal)) {
            rememberCall(call->call.name);
            for (const auto& arg : call->call.args) collectExprCalls(arg.value);
            return;
        }
        if (auto assign = std::dynamic_pointer_cast<AssignGoal>(goal)) {
            collectGoalCalls(assign->goal);
            collectExprCalls(assign->expr);
            return;
        }
        if (auto binary = std::dynamic_pointer_cast<BinaryGoal>(goal)) {
            collectExprCalls(binary->left);
            collectExprCalls(binary->right);
            return;
        }
        if (auto where = std::dynamic_pointer_cast<WhereGoal>(goal)) {
            collectGoalCalls(where->condition);
            return;
        }
        if (auto ret = std::dynamic_pointer_cast<ReturnGoal>(goal)) {
            for (const auto& field : ret->fields) collectExprCalls(field.value);
            return;
        }
        if (auto group = std::dynamic_pointer_cast<GroupGoal>(goal)) {
            for (const auto& child : group->goals) collectGoalCalls(child);
            return;
        }
        if (auto orGoal = std::dynamic_pointer_cast<OrGoal>(goal)) {
            for (const auto& branch : orGoal->branches) {
                for (const auto& child : branch) collectGoalCalls(child);
            }
        }
    };

    for (const auto& item : clauses_) {
        for (const auto& clause : item.second) {
            for (const auto& goal : clause->body) collectGoalCalls(goal);
            for (const auto& branch : clause->fallbackBranches) {
                for (const auto& goal : branch) collectGoalCalls(goal);
            }
        }
    }

    auto writeNode = [&](const std::string& id,
                         const std::string& label,
                         const std::string& kind,
                         const std::string& detail = std::string()) {
        if (!firstNode) out << ",";
        firstNode = false;
        out << "{\"id\":\"" << jsonEscape(id)
            << "\",\"label\":\"" << jsonEscape(label)
            << "\",\"kind\":\"" << jsonEscape(kind) << "\"";
        if (!detail.empty()) out << ",\"detail\":\"" << jsonEscape(detail) << "\"";
        out << "}";
    };

    auto writeEdge = [&](const std::string& from,
                         const std::string& to,
                         const std::string& label) {
        auto edgeKey = from + "\n" + to + "\n" + label;
        if (!emittedEdges.insert(edgeKey).second) return;
        if (!firstEdge) out << ",";
        firstEdge = false;
        out << "{\"from\":\"" << jsonEscape(from)
            << "\",\"to\":\"" << jsonEscape(to)
            << "\",\"label\":\"" << jsonEscape(label) << "\"}";
    };

    out << "{\"nodes\":[";

    struct FactTypeProfile {
        size_t records = 0;
        std::unordered_map<std::string, size_t> fieldPresence;
    };
    std::unordered_map<std::string, FactTypeProfile> factProfiles;
    for (const auto& fact : memory_.facts()) {
        auto& profile = factProfiles[fact.type];
        profile.records++;
        if (!fact.value) continue;
        for (const auto& entry : fact.value->entries) {
            profile.fieldPresence[entry.key]++;
        }
    }
    auto factDetail = [&](const std::string& type) {
        const auto found = factProfiles.find(type);
        if (found == factProfiles.end()) return std::string("records=0 fields=0");
        std::ostringstream detail;
        detail << "records=" << found->second.records
               << " fields=" << found->second.fieldPresence.size();
        return detail.str();
    };
    auto fieldDetail = [&](const std::string& type, const std::string& field) {
        const auto found = factProfiles.find(type);
        if (found == factProfiles.end() || found->second.records == 0) return std::string();
        const auto fieldFound = found->second.fieldPresence.find(field);
        const size_t present = fieldFound == found->second.fieldPresence.end() ? 0 : fieldFound->second;
        const size_t missing = found->second.records > present ? found->second.records - present : 0;
        const double coverage = (static_cast<double>(present) * 100.0) / static_cast<double>(found->second.records);
        std::ostringstream detail;
        detail << "present=" << present
               << " missing=" << missing
               << " coverage=" << std::fixed << std::setprecision(1) << coverage << "%";
        return detail.str();
    };

    std::set<std::string> factTypes;
    for (const auto& fact : memory_.facts()) factTypes.insert(fact.type);
    for (const auto& item : memory_.parents()) {
        factTypes.insert(item.first);
        factTypes.insert(item.second);
    }
    for (const auto& type : factTypes) {
        writeNode(nodeId("fact", type), type, "fact", factDetail(type));
    }
    for (const auto& name : factCallNodes) {
        if (!factTypes.count(name)) writeNode(nodeId("fact", name), name, "fact", "referenced fact/type with no loaded records");
    }

    for (const auto& item : clauses_) {
        if (hasNonFactClause(item.second)) {
            writeNode(nodeId("method", item.first), item.first, "method");
        }
    }
    for (const auto& name : methodCallNodes) {
        if (!isMethodName(name)) writeNode(nodeId("method", name), name, "method");
    }
    for (const auto& name : libraryCallNodes) {
        writeNode(nodeId("library", name), name, "library");
    }

    for (const auto& item : globals_) {
        writeNode(nodeId("global", item.first), item.first, "global");
    }

    std::set<std::string> fieldNodeIds;
    for (const auto& fact : memory_.facts()) {
        if (!fact.value) continue;
        for (const auto& entry : fact.value->entries) {
            auto fieldId = nodeId("field", fact.type + "." + entry.key);
            if (fieldNodeIds.insert(fieldId).second) {
                writeNode(fieldId, entry.key, "field", fieldDetail(fact.type, entry.key));
            }
        }
    }

    out << "],\"edges\":[";

    for (const auto& item : memory_.parents()) {
        writeEdge(nodeId("fact", item.first), nodeId("fact", item.second), "extends");
    }

    for (const auto& fact : memory_.facts()) {
        if (!fact.parentType.empty()) {
            writeEdge(nodeId("fact", fact.type), nodeId("fact", fact.parentType), "extends");
        }
        if (!fact.value) continue;
        for (const auto& entry : fact.value->entries) {
            writeEdge(nodeId("fact", fact.type), nodeId("field", fact.type + "." + entry.key), "field");
        }
    }

    for (const auto& item : clauses_) {
        if (!hasNonFactClause(item.second)) continue;
        auto from = nodeId("method", item.first);
        std::function<void(const std::shared_ptr<Goal>&, const std::string&)> writeGoalEdges;
        writeGoalEdges = [&](const std::shared_ptr<Goal>& goal, const std::string& label) {
            std::function<void(const std::shared_ptr<Expr>&)> writeExprEdges;
            writeExprEdges = [&](const std::shared_ptr<Expr>& expr) {
                if (!expr) return;
                if (auto term = std::dynamic_pointer_cast<TermExpr>(expr)) {
                    auto targetKind = classifyCall(term->name);
                    writeEdge(from, nodeId(targetKind, term->name), label);
                    for (const auto& arg : term->args) writeExprEdges(arg.value);
                    return;
                }
                if (auto lambda = std::dynamic_pointer_cast<LambdaExpr>(expr)) {
                    writeExprEdges(lambda->source);
                    writeExprEdges(lambda->body);
                    writeExprEdges(lambda->right);
                    return;
                }
                if (auto binary = std::dynamic_pointer_cast<BinaryExpr>(expr)) {
                    writeExprEdges(binary->left);
                    writeExprEdges(binary->right);
                    return;
                }
                if (auto array = std::dynamic_pointer_cast<ArrayExpr>(expr)) {
                    for (const auto& item : array->items) writeExprEdges(item);
                    return;
                }
                if (auto map = std::dynamic_pointer_cast<MapExpr>(expr)) {
                    for (const auto& entry : map->entries) writeExprEdges(entry.value);
                    return;
                }
                if (auto access = std::dynamic_pointer_cast<AccessExpr>(expr)) {
                    writeExprEdges(access->target);
                }
            };

            if (!goal) return;
            if (auto call = std::dynamic_pointer_cast<CallGoal>(goal)) {
                auto targetKind = classifyCall(call->call.name);
                writeEdge(from, nodeId(targetKind, call->call.name), label);
                for (const auto& arg : call->call.args) writeExprEdges(arg.value);
                return;
            }
            if (auto assign = std::dynamic_pointer_cast<AssignGoal>(goal)) {
                writeGoalEdges(assign->goal, "assigns");
                writeExprEdges(assign->expr);
                return;
            }
            if (auto binary = std::dynamic_pointer_cast<BinaryGoal>(goal)) {
                writeExprEdges(binary->left);
                writeExprEdges(binary->right);
                return;
            }
            if (auto where = std::dynamic_pointer_cast<WhereGoal>(goal)) {
                writeGoalEdges(where->condition, "where");
                return;
            }
            if (auto ret = std::dynamic_pointer_cast<ReturnGoal>(goal)) {
                for (const auto& field : ret->fields) writeExprEdges(field.value);
                return;
            }
            if (auto group = std::dynamic_pointer_cast<GroupGoal>(goal)) {
                for (const auto& child : group->goals) writeGoalEdges(child, label);
                return;
            }
            if (auto orGoal = std::dynamic_pointer_cast<OrGoal>(goal)) {
                for (const auto& branch : orGoal->branches) {
                    for (const auto& child : branch) writeGoalEdges(child, "or");
                }
            }
        };

        for (const auto& clause : item.second) {
            for (const auto& goal : clause->body) {
                writeGoalEdges(goal, "calls");
            }
            for (const auto& branch : clause->fallbackBranches) {
                for (const auto& goal : branch) writeGoalEdges(goal, "else");
            }
        }
    }

    out << "]}";
    return out.str();
}

} // namespace Felidae
