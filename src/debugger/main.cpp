#include "debugger/AstAnalyzer.h"
#include "BuiltinRegistry.h"
#include "tooling/SourceParser.h"
#include "Version.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;
using namespace Felidae;

struct DebugOptions {
    bool stopOnEntry = false;
    bool loadImports = false;
    bool checkOnly = false;
    bool checkJson = false;
    bool metricsJson = false;
    bool lspMode = false;
    bool listLibraries = false;
    bool listBuiltins = false;
    bool help = false;
    std::optional<fs::path> programFile;
};

static DebugOptions parseDebugCli(int argc, char** argv) {
    DebugOptions options;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--stop-on-entry") {
            options.stopOnEntry = true;
            continue;
        }
        if (arg == "--load-imports") {
            options.loadImports = true;
            continue;
        }
        if (arg == "--check") {
            options.checkOnly = true;
            continue;
        }
        if (arg == "--check-json") {
            options.checkJson = true;
            continue;
        }
        if (arg == "--metrics-json") {
            options.metricsJson = true;
            continue;
        }
        if (arg == "--lsp") {
            options.lspMode = true;
            continue;
        }
        if (arg == "--list-libraries") {
            options.listLibraries = true;
            continue;
        }
        if (arg == "--list-builtins") {
            options.listBuiltins = true;
            continue;
        }
        if (arg == "--help" || arg == "-h") {
            options.help = true;
            continue;
        }
        if (arg == "--inspect-graph" || arg == "--visualize-data-json" ||
            arg == "--visualize-data-html" || arg == "--json" || arg == "--html") {
            throw std::runtime_error(
                "Visualization is owned by Celidae. Run celidae program.fx --json or --html");
        }
        if (arg == "--query" || (!arg.empty() && arg.front() == '?')) {
            throw std::runtime_error(
                "felidae_debug does not execute queries. Run felidae program.fx '? Query(...)'");
        }
        if (!options.programFile) {
            options.programFile = fs::path(arg);
            continue;
        }
        throw std::runtime_error("Unexpected debugger argument: " + arg);
    }
    return options;
}

static void printDebugUsage(std::ostream& out) {
    out << "Felidae AST debugger " << LANGUAGE_VERSION << "\n"
        << "Pipeline: Lexer -> Parser -> AST Analyzer\n"
        << "Usage: felidae_debug <file.fx> [--check|--check-json] [--load-imports]\n"
        << "\n"
        << "Options:\n"
        << "  --check          Parse and emit FELIDAE_DIAGNOSTIC records.\n"
        << "  --check-json     Emit structured JSON diagnostics for editor integrations.\n"
        << "  --load-imports   Parse source imports before AST analysis; native imports are ignored.\n"
        << "  --stop-on-entry  Wait for a debugger command before analysis.\n"
        << "  --lsp            Start the Felidae JSON-RPC diagnostics server over stdio.\n"
        << "  --list-libraries Print a JSON array of importable core library names.\n"
        << "                   Resolved relative to <file.fx> when given, otherwise the\n"
        << "                   current directory. Editor integrations should call this\n"
        << "                   instead of hand-maintaining a copy of the module list.\n"
        << "  --list-builtins  Print a JSON array of {name, effect} builtin functions.\n";
}

static std::string trimText(const std::string& text);

static void waitForContinue() {
    std::cout << "FELIDAE_DEBUG_STOPPED reason=entry\n" << std::flush;
    std::string command;
    while (std::getline(std::cin, command)) {
        command = trimText(command);
        if (command == "continue" || command == "c" ||
            command == "next" || command == "stepIn" || command == "stepOut") {
            std::cout << "FELIDAE_DEBUG_CONTINUED\n" << std::flush;
            return;
        }
        if (command == "disconnect" || command == "terminate" || command == "exit") {
            std::cout << "FELIDAE_DEBUG_TERMINATED\n" << std::flush;
            std::exit(0);
        }
    }
}

static std::string trimText(const std::string& text) {
    std::size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) start++;
    std::size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) end--;
    return text.substr(start, end - start);
}

static void printAstDiagnostics(const std::vector<AstDiagnostic>& diagnostics) {
    for (const auto& diagnostic : diagnostics) {
        std::cout << "FELIDAE_DIAGNOSTIC "
                  << "severity=" << diagnostic.severity
                  << " code=" << (diagnostic.code.empty() ? "felidae.ast" : diagnostic.code)
                  << " line=" << diagnostic.line
                  << " column=" << diagnostic.column
                  << " message=" << diagnostic.message << "\n";
    }
}

static std::string jsonEscape(const std::string& value) {
    std::ostringstream out;
    for (char ch : value) {
        switch (ch) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    out << "\\u";
                    const char* hex = "0123456789abcdef";
                    out << "00" << hex[(ch >> 4) & 0x0f] << hex[ch & 0x0f];
                } else {
                    out << ch;
                }
        }
    }
    return out.str();
}

static const char* builtinEffectName(BuiltinEffect effect) {
    switch (effect) {
        case BuiltinEffect::Pure: return "pure";
        case BuiltinEffect::ReadsExternalState: return "reads";
        case BuiltinEffect::WritesExternalState: return "writes";
        case BuiltinEffect::Volatile: return "volatile";
    }
    return "volatile";
}

static std::string librariesJson(const std::vector<std::string>& names) {
    std::ostringstream out;
    out << "[";
    for (size_t i = 0; i < names.size(); ++i) {
        if (i) out << ",";
        out << "\"" << jsonEscape(names[i]) << "\"";
    }
    out << "]";
    return out.str();
}

static std::string builtinsJson(const std::vector<BuiltinInfo>& infos) {
    std::ostringstream out;
    out << "[";
    for (size_t i = 0; i < infos.size(); ++i) {
        if (i) out << ",";
        out << "{\"name\":\"" << jsonEscape(infos[i].name)
            << "\",\"effect\":\"" << builtinEffectName(infos[i].effect) << "\"}";
    }
    out << "]";
    return out.str();
}

static int lspSeverity(const std::string& severity) {
    if (severity == "error") return 1;
    if (severity == "info") return 3;
    if (severity == "hint") return 4;
    return 2;
}

static bool hasErrorDiagnostic(const std::vector<AstDiagnostic>& diagnostics) {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const AstDiagnostic& diagnostic) {
        return diagnostic.severity == "error" || diagnostic.severity == "fatal";
    });
}

static std::string diagnosticsJson(const std::vector<AstDiagnostic>& diagnostics) {
    std::ostringstream out;
    out << "{\"ok\":" << (hasErrorDiagnostic(diagnostics) ? "false" : "true") << ",\"diagnostics\":[";
    for (size_t i = 0; i < diagnostics.size(); ++i) {
        const auto& diagnostic = diagnostics[i];
        if (i) out << ",";
        int line = std::max(1, diagnostic.line);
        int column = std::max(1, diagnostic.column);
        int endLine = std::max(line, diagnostic.endLine);
        int endColumn = std::max(
            endLine == line ? column : 1,
            diagnostic.endColumn);
        out << "{\"severity\":\"" << jsonEscape(diagnostic.severity)
            << "\",\"lspSeverity\":" << lspSeverity(diagnostic.severity)
            << ",\"line\":" << line
            << ",\"column\":" << column
            << ",\"endLine\":" << endLine
            << ",\"endColumn\":" << endColumn
            << ",\"code\":\"" << jsonEscape(
                diagnostic.code.empty() ? "felidae.ast" : diagnostic.code) << "\""
            << ",\"file\":\"" << jsonEscape(diagnostic.file) << "\""
            << ",\"message\":\"" << jsonEscape(diagnostic.message) << "\"}";
    }
    out << "]}";
    return out.str();
}

static std::string errorJson(const std::string& message) {
    return "{\"ok\":false,\"diagnostics\":[{\"severity\":\"error\",\"lspSeverity\":1,"
           "\"line\":1,\"column\":1,\"message\":\"" + jsonEscape(message) + "\"}]}";
}

static std::string extractJsonString(const std::string& json, const std::string& key, size_t start = 0) {
    std::string needle = "\"" + key + "\"";
    size_t keyPos = json.find(needle, start);
    if (keyPos == std::string::npos) return {};
    size_t colon = json.find(':', keyPos + needle.size());
    if (colon == std::string::npos) return {};
    size_t quote = json.find('"', colon + 1);
    if (quote == std::string::npos) return {};
    std::string result;
    for (size_t i = quote + 1; i < json.size(); ++i) {
        char ch = json[i];
        if (ch == '"') return result;
        if (ch == '\\' && i + 1 < json.size()) {
            char esc = json[++i];
            switch (esc) {
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                default: result.push_back(esc); break;
            }
        } else {
            result.push_back(ch);
        }
    }
    return {};
}

static bool hasJsonKey(const std::string& json, const std::string& key, size_t start = 0) {
    std::string needle = "\"" + key + "\"";
    size_t keyPos = json.find(needle, start);
    if (keyPos == std::string::npos) return false;
    size_t colon = json.find(':', keyPos + needle.size());
    return colon != std::string::npos;
}

static std::string extractJsonId(const std::string& json) {
    size_t keyPos = json.find("\"id\"");
    if (keyPos == std::string::npos) return "null";
    size_t colon = json.find(':', keyPos + 4);
    if (colon == std::string::npos) return "null";
    size_t pos = colon + 1;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) pos++;
    if (pos >= json.size()) return "null";
    if (json[pos] == '"') return "\"" + jsonEscape(extractJsonString(json, "id")) + "\"";
    size_t end = pos;
    while (end < json.size() && (std::isdigit(static_cast<unsigned char>(json[end])) || json[end] == '-')) end++;
    return end > pos ? json.substr(pos, end - pos) : "null";
}

static int hexValue(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return 10 + ch - 'a';
    if (ch >= 'A' && ch <= 'F') return 10 + ch - 'A';
    return -1;
}

static std::string percentDecode(const std::string& text) {
    std::string decoded;
    decoded.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            int hi = hexValue(text[i + 1]);
            int lo = hexValue(text[i + 2]);
            if (hi >= 0 && lo >= 0) {
                decoded.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        decoded.push_back(text[i]);
    }
    return decoded;
}

static std::string fileUriToPath(std::string uri) {
    const std::string prefix = "file:///";
    if (uri.rfind(prefix, 0) == 0) {
        uri = uri.substr(prefix.size());
#ifdef _WIN32
        for (char& ch : uri) {
            if (ch == '/') ch = '\\';
        }
#else
        uri = "/" + uri;
#endif
    }
    return percentDecode(uri);
}

static std::string analyzeTextJson(const fs::path& path, const std::string& text) {
    try {
        constexpr size_t kMaxTextCacheEntries = 128;
        struct CachedTextAnalysis {
            size_t size = 0;
            size_t hash = 0;
            size_t lastUsed = 0;
            std::vector<AstDiagnostic> diagnostics;
        };
        static std::unordered_map<std::string, CachedTextAnalysis> textCache;
        static size_t textCacheClock = 0;

        const auto key = fs::absolute(path).lexically_normal().string();
        const auto hash = std::hash<std::string>{}(text);
        std::vector<AstDiagnostic> diagnostics;
        auto cached = textCache.find(key);
        if (cached != textCache.end() && cached->second.size == text.size() && cached->second.hash == hash) {
            cached->second.lastUsed = ++textCacheClock;
            diagnostics = cached->second.diagnostics;
        } else {
            Program program = Tooling::parseText(text);
            diagnostics = analyzeProgramAst(program);
            for (auto& diagnostic : diagnostics) diagnostic.file = key;
            // Only compact diagnostics survive this request. Keeping Program here
            // retained every statement and expression for each open editor file.
            textCache[key] = CachedTextAnalysis{
                text.size(), hash, ++textCacheClock, diagnostics};
            if (textCache.size() > kMaxTextCacheEntries) {
                auto oldest = textCache.end();
                for (auto it = textCache.begin(); it != textCache.end(); ++it) {
                    if (oldest == textCache.end() || it->second.lastUsed < oldest->second.lastUsed) {
                        oldest = it;
                    }
                }
                if (oldest != textCache.end()) textCache.erase(oldest);
            }
        }
        return diagnosticsJson(diagnostics);
    } catch (const std::exception& e) {
        return errorJson(e.what());
    }
}

static std::string analyzeFileJson(const fs::path& path) {
    try {
        AstAnalysisSession analysis;
        Tooling::loadProgramStatements(
            path,
            true,
            [&](const std::shared_ptr<Statement>& statement) {
                analysis.consume(statement);
            });
        auto diagnostics = analysis.finish();
        const auto file = fs::absolute(path).lexically_normal().string();
        for (auto& diagnostic : diagnostics) diagnostic.file = file;
        return diagnosticsJson(diagnostics);
    } catch (const std::exception& e) {
        return errorJson(e.what());
    }
}

static void writeLspMessage(const std::string& body) {
    std::cout << "Content-Length: " << body.size() << "\r\n\r\n" << body << std::flush;
}

static void publishDiagnostics(const std::string& uri, const std::string& checkJson) {
    std::ostringstream out;
    out << "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\""
        << jsonEscape(uri) << "\",\"diagnostics\":[";
    size_t pos = 0;
    bool first = true;
    while ((pos = checkJson.find("\"message\"", pos)) != std::string::npos) {
        std::string message = extractJsonString(checkJson, "message", pos);
        size_t lineKey = checkJson.rfind("\"line\"", pos);
        size_t columnKey = checkJson.rfind("\"column\"", pos);
        size_t severityKey = checkJson.rfind("\"lspSeverity\"", pos);
        auto readNumber = [&](size_t key, int fallback) {
            if (key == std::string::npos) return fallback;
            size_t colon = checkJson.find(':', key);
            if (colon == std::string::npos) return fallback;
            return std::max(0, std::atoi(checkJson.c_str() + colon + 1));
        };
        int line = std::max(0, readNumber(lineKey, 1) - 1);
        int column = std::max(0, readNumber(columnKey, 1) - 1);
        int severity = readNumber(severityKey, 2);
        if (!first) out << ",";
        first = false;
        out << "{\"range\":{\"start\":{\"line\":" << line << ",\"character\":" << column
            << "},\"end\":{\"line\":" << line << ",\"character\":" << (column + 1)
            << "}},\"severity\":" << severity
            << ",\"source\":\"Felidae Debugger\",\"message\":\"" << jsonEscape(message) << "\"}";
        pos += 9;
    }
    out << "]}}}";
    writeLspMessage(out.str());
}

static bool readLspMessage(std::string& body) {
    std::string line;
    size_t contentLength = 0;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;
        const std::string header = "Content-Length:";
        if (line.rfind(header, 0) == 0) {
            contentLength = static_cast<size_t>(std::stoul(trimText(line.substr(header.size()))));
        }
    }
    if (contentLength == 0) return false;
    body.assign(contentLength, '\0');
    std::cin.read(&body[0], static_cast<std::streamsize>(contentLength));
    return static_cast<size_t>(std::cin.gcount()) == contentLength;
}

static int runLspServer() {
    std::map<std::string, std::string> documents;
    std::string body;
    while (readLspMessage(body)) {
        std::string method = extractJsonString(body, "method");
        std::string id = extractJsonId(body);
        if (method == "initialize") {
            writeLspMessage("{\"jsonrpc\":\"2.0\",\"id\":" + id +
                            ",\"result\":{\"capabilities\":{\"textDocumentSync\":1}}}");
        } else if (method == "shutdown") {
            writeLspMessage("{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":null}");
        } else if (method == "exit") {
            return 0;
        } else if (method == "textDocument/didOpen" || method == "textDocument/didChange" || method == "textDocument/didSave") {
            std::string uri = extractJsonString(body, "uri");
            if (hasJsonKey(body, "text")) {
                documents[uri] = extractJsonString(body, "text");
            }
            auto existing = documents.find(uri);
            std::string result = existing != documents.end()
                ? analyzeTextJson(fileUriToPath(uri), existing->second)
                : analyzeFileJson(fileUriToPath(uri));
            publishDiagnostics(uri, result);
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    DebugOptions options;
    try {
        options = parseDebugCli(argc, argv);
        if (options.help) {
            printDebugUsage(std::cout);
            return 0;
        }
        if (options.lspMode) {
            return runLspServer();
        }
        if (options.listLibraries) {
            fs::path startDir = options.programFile
                ? Tooling::resolveEntryPath(*options.programFile).parent_path()
                : fs::current_path();
            std::cout << librariesJson(Tooling::listCoreLibraries(startDir)) << "\n" << std::flush;
            return 0;
        }
        if (options.listBuiltins) {
            std::cout << builtinsJson(allBuiltins()) << "\n" << std::flush;
            return 0;
        }
        if (!options.programFile) {
            std::cerr << "error: felidae_debug requires a .fx program file\n";
            printDebugUsage(std::cerr);
            return 1;
        }
        if (options.programFile->extension() != FILE_EXTENSION) {
            throw std::runtime_error("Felidae source files must use .fx extension");
        }

        const bool dataOutputOnly = options.checkJson;
        if (!dataOutputOnly) {
            std::cout << "FELIDAE_DEBUG_READY program=" << options.programFile->string() << "\n" << std::flush;
        }
        if (options.stopOnEntry) {
            waitForContinue();
        }

        AstAnalysisSession analysis;
        const auto analysisStarted = std::chrono::steady_clock::now();
        Tooling::loadProgramStatements(
            *options.programFile,
            options.loadImports,
            [&](const std::shared_ptr<Statement>& statement) {
                analysis.consume(statement);
            });
        auto astDiagnostics = analysis.finish();
        const auto analyzedFile =
            fs::absolute(*options.programFile).lexically_normal().string();
        for (auto& diagnostic : astDiagnostics) diagnostic.file = analyzedFile;
        const auto analysisMicros = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - analysisStarted).count();
        if (!dataOutputOnly) {
            std::cerr << "Felidae AST debugger analyzing " << options.programFile->string() << "\n";
        }

        if (options.checkJson) {
            std::cout << diagnosticsJson(astDiagnostics) << "\n" << std::flush;
            if (hasErrorDiagnostic(astDiagnostics)) {
                return 1;
            }
        } else {
            printAstDiagnostics(astDiagnostics);
            std::cout << "FELIDAE_CHECK_OK\n" << std::flush;
        }

        if (!dataOutputOnly) {
            std::cout << "FELIDAE_DEBUG_EXIT code=0\n" << std::flush;
        }
        if (options.metricsJson) {
            std::cerr << "FELIDAE_DEBUG_METRICS {\"analysisMs\":"
                      << static_cast<double>(analysisMicros) / 1000.0 << "}\n";
        }
        return 0;
    } catch (const std::exception& e) {
        if (options.checkJson) {
            std::cout << errorJson(e.what()) << "\n" << std::flush;
            return 1;
        }
        std::cerr << "error: " << e.what() << "\n";
        std::cout << "FELIDAE_DEBUG_EXIT code=1\n" << std::flush;
        return 1;
    }
}
