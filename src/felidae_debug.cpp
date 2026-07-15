#include "AstAnalyzer.h"
#include "FelidaeRuntime.h"
#include "Version.h"
#include "Visualization.h"

#include <algorithm>
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
    bool inspectGraph = false;
    bool loadImports = false;
    bool visualizeDataJson = false;
    bool visualizeDataHtml = false;
    bool checkOnly = false;
    bool checkJson = false;
    bool lspMode = false;
    bool help = false;
    std::optional<fs::path> programFile;
    std::optional<std::string> query;
    std::vector<std::string> remainingArgs;
};

static DebugOptions parseDebugCli(int argc, char** argv) {
    DebugOptions options;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--stop-on-entry") {
            options.stopOnEntry = true;
            continue;
        }
        if (arg == "--inspect-graph") {
            options.inspectGraph = true;
            continue;
        }
        if (arg == "--load-imports") {
            options.loadImports = true;
            continue;
        }
        if (arg == "--visualize-data-json") {
            options.visualizeDataJson = true;
            continue;
        }
        if (arg == "--visualize-data-html") {
            options.visualizeDataHtml = true;
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
        if (arg == "--lsp") {
            options.lspMode = true;
            continue;
        }
        if (arg == "--help" || arg == "-h") {
            options.help = true;
            continue;
        }
        if (arg == "--query" && i + 1 < argc) {
            options.query = argv[++i];
            continue;
        }
        if (!options.programFile) {
            options.programFile = fs::path(arg);
            continue;
        }
        if (!options.query && !arg.empty() && arg[0] == '?') {
            options.query = arg;
            continue;
        }
        options.remainingArgs.push_back(arg);
    }
    return options;
}

static void printDebugUsage(std::ostream& out) {
    out << "Celidae debugger and analytics host " << LANGUAGE_VERSION << "\n"
        << "Usage: celidae <file.fx> [--check|--inspect-graph|--visualize-data-json|--visualize-data-html|--query <query>] [--load-imports]\n"
        << "\n"
        << "Options:\n"
        << "  --check          Parse, load imports, and emit FELIDAE_DIAGNOSTIC records.\n"
        << "  --check-json     Emit structured JSON diagnostics for editor integrations.\n"
        << "  --inspect-graph  Print a lightweight runtime graph JSON between FELIDAE_GRAPH markers.\n"
        << "  --load-imports   Load imported modules/fact DBs before query or visualization output.\n"
        << "  --visualize-data-json  Print viewer JSON between FELIDAE_GRAPH markers.\n"
        << "  --visualize-data-html  Print a standalone HTML data visualization document.\n"
        << "  --query <query>  Execute a query against the loaded program.\n"
        << "  --stop-on-entry  Wait for a debugger command before running.\n"
        << "  --lsp            Start the Celidae JSON-RPC language server over stdio.\n";
}

static void waitForContinue() {
    std::cout << "FELIDAE_DEBUG_STOPPED reason=entry\n" << std::flush;
    std::string command;
    while (std::getline(std::cin, command)) {
        command = trim(command);
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

static void printAstDiagnostics(const std::vector<AstDiagnostic>& diagnostics) {
    for (const auto& diagnostic : diagnostics) {
        std::cout << "FELIDAE_DIAGNOSTIC "
                  << "severity=" << diagnostic.severity
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
        out << "{\"severity\":\"" << jsonEscape(diagnostic.severity)
            << "\",\"lspSeverity\":" << lspSeverity(diagnostic.severity)
            << ",\"line\":" << line
            << ",\"column\":" << column
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
        struct CachedTextProgram {
            size_t size = 0;
            size_t hash = 0;
            size_t lastUsed = 0;
            Program program;
            std::vector<AstDiagnostic> diagnostics;
        };
        static std::unordered_map<std::string, CachedTextProgram> textCache;
        static size_t textCacheClock = 0;

        const auto key = fs::absolute(path).lexically_normal().string();
        const auto hash = std::hash<std::string>{}(text);
        Program program;
        std::vector<AstDiagnostic> diagnostics;
        auto cached = textCache.find(key);
        if (cached != textCache.end() && cached->second.size == text.size() && cached->second.hash == hash) {
            cached->second.lastUsed = ++textCacheClock;
            program = cached->second.program;
            diagnostics = cached->second.diagnostics;
        } else {
            program = parseProgramText(text);
            diagnostics = analyzeProgramAst(program);
            textCache[key] = CachedTextProgram{text.size(), hash, ++textCacheClock, program, diagnostics};
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
        Interpreter interpreter;
        loadProgramRoot(path, program, interpreter);
        interpreter.loadAllImports();
        return diagnosticsJson(diagnostics);
    } catch (const std::exception& e) {
        return errorJson(e.what());
    }
}

static std::string analyzeFileJson(const fs::path& path) {
    try {
        Program program = parseProgramFile(path);
        auto diagnostics = analyzeProgramAst(program);
        Interpreter interpreter;
        loadProgramRoot(path, program, interpreter);
        interpreter.loadAllImports();
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
            << ",\"source\":\"Celidae\",\"message\":\"" << jsonEscape(message) << "\"}";
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
            contentLength = static_cast<size_t>(std::stoul(trim(line.substr(header.size()))));
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
        setProgramAstCacheEnabled(true);
        options = parseDebugCli(argc, argv);
        if (options.help) {
            printDebugUsage(std::cout);
            return 0;
        }
        if (options.lspMode) {
            return runLspServer();
        }
        if (!options.programFile) {
            std::cerr << "error: Celidae requires a .fx program file\n";
            printDebugUsage(std::cerr);
            return 1;
        }
        if (options.programFile->extension() != FILE_EXTENSION) {
            throw std::runtime_error("Felidae source files must use .fx extension");
        }

        const bool dataOutputOnly = options.checkJson || options.visualizeDataJson || options.visualizeDataHtml;
        if (!dataOutputOnly) {
            std::cout << "FELIDAE_DEBUG_READY program=" << options.programFile->string() << "\n" << std::flush;
        }
        if (options.stopOnEntry) {
            waitForContinue();
        }

        Program program = parseProgramFile(*options.programFile);
        auto astDiagnostics = analyzeProgramAst(program);

        Interpreter interpreter;
        loadProgramRoot(*options.programFile, program, interpreter);
        if (!dataOutputOnly) {
            std::cerr << "Celidae analytics host running " << options.programFile->string() << "\n";
        }

        if (options.loadImports && !options.checkJson && !options.checkOnly) {
            interpreter.loadAllImports();
        }

        if (options.checkJson) {
            interpreter.loadAllImports();
            std::cout << diagnosticsJson(astDiagnostics) << "\n" << std::flush;
            if (hasErrorDiagnostic(astDiagnostics)) {
                return 1;
            }
        } else if (options.checkOnly) {
            interpreter.loadAllImports();
            printAstDiagnostics(astDiagnostics);
            std::cout << "FELIDAE_CHECK_OK\n" << std::flush;
        } else if (options.visualizeDataHtml) {
            std::cout << interpreter.visualizeDataHtml(false) << std::flush;
        } else if (options.inspectGraph || options.visualizeDataJson) {
            std::cout << graphJsonEnvelope(interpreter.visualizeDataJson(false)) << std::flush;
        } else if (options.query) {
            auto queryGoals = parseQueryText(*options.query);
            auto solutions = interpreter.solve(queryGoals, 1000);
            printSolutions(interpreter, queryGoals, solutions, std::cout);
        } else if (interpreter.hasMethod("main")) {
            auto result = interpreter.callMain(makeSystemInput(options.remainingArgs));
            std::cout << interpreter.valueToString(result) << "\n";
        } else {
            std::cout << "Program loaded successfully. No main() method found.\n"
                      << "Use a query argument or run with --repl.\n";
        }

        if (!dataOutputOnly) {
            std::cout << "FELIDAE_DEBUG_EXIT code=0\n" << std::flush;
        }
        return 0;
    } catch (const std::exception& e) {
        if (options.checkJson || options.visualizeDataJson) {
            std::cout << errorJson(e.what()) << "\n" << std::flush;
            return 1;
        }
        if (options.visualizeDataHtml) {
            std::cerr << "error: " << e.what() << "\n";
            return 1;
        }
        std::cerr << "error: " << e.what() << "\n";
        std::cout << "FELIDAE_DEBUG_EXIT code=1\n" << std::flush;
        return 1;
    }
}
