#include "debugger/AstAnalyzer.h"
#include "BuiltinRegistry.h"
#include "OperatorAnnotation.h"
#include "tooling/SourceParser.h"
#include "Version.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace fs = std::filesystem;
using namespace Felidae;

struct DebugOptions {
    bool stopOnEntry = false;
    bool loadImports = false;
    bool checkJson = false;
    bool metricsJson = false;
    bool lspMode = false;
    bool listLibraries = false;
    bool listBuiltins = false;
    bool symbolsJson = false;
    bool operatorsJson = false;
    bool version = false;
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
            // Accepted for compatibility with documented usage; the default
            // (non --check-json) path below already does exactly this: parse,
            // print FELIDAE_DIAGNOSTIC records, and report FELIDAE_CHECK_OK.
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
        if (arg == "--symbols-json") {
            options.symbolsJson = true;
            continue;
        }
        if (arg == "--operators-json") {
            options.operatorsJson = true;
            continue;
        }
        if (arg == "--version" || arg == "-v") {
            options.version = true;
            continue;
        }
        if (arg == "--help" || arg == "-h") {
            options.help = true;
            continue;
        }
        if (arg == "--inspect-graph" || arg == "--visualize-data-json" ||
            arg == "--visualize-data-html" || arg == "--json" || arg == "--html") {
            throw std::runtime_error(
                "Visualization options are no longer supported");
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
        << "Pipeline: SentencePiece IDs -> Integer Parser -> AST Analyzer\n"
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
        << "  --list-builtins  Print a JSON array of {name, effect} builtin functions.\n"
        << "  --symbols-json   Print a JSON symbol table (methods, facts, globals with\n"
        << "                   source spans and declared params, plus resolved files and\n"
        << "                   unresolved imports) for <file.fx>. Params back editor\n"
        << "                   signature help. Always follows imports, like --lsp.\n"
        << "  --operators-json Print versioned, non-executing dynamic operator metadata\n"
        << "                   for <file.fx>, including imported public contracts.\n"
        << "  --version, -v    Print {name, version} as JSON.\n";
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

static std::string spanJson(const SourceSpan& span) {
    std::ostringstream out;
    out << "{\"startLine\":" << span.startLine
        << ",\"startColumn\":" << span.startColumn
        << ",\"endLine\":" << span.endLine
        << ",\"endColumn\":" << span.endColumn << "}";
    return out.str();
}

static std::string symbolDefinitionsJson(const std::vector<SymbolDefinition>& definitions) {
    std::ostringstream out;
    out << "[";
    for (size_t i = 0; i < definitions.size(); ++i) {
        if (i) out << ",";
        const auto& definition = definitions[i];
        out << "{\"name\":\"" << jsonEscape(definition.name)
            << "\",\"count\":" << definition.count
            << ",\"spans\":[";
        for (size_t s = 0; s < definition.spans.size(); ++s) {
            if (s) out << ",";
            out << spanJson(definition.spans[s]);
        }
        out << "],\"params\":[";
        for (size_t p = 0; p < definition.params.size(); ++p) {
            if (p) out << ",";
            out << "{\"name\":\"" << jsonEscape(definition.params[p].name)
                << "\",\"type\":\"" << jsonEscape(definition.params[p].type)
                << "\"}";
        }
        out << "]}";
    }
    out << "]";
    return out.str();
}

static std::string symbolsJson(
    const SymbolSummary& symbols,
    const std::vector<fs::path>& files,
    const std::vector<std::string>& unresolvedImports) {
    std::ostringstream out;
    out << "{\"methods\":" << symbolDefinitionsJson(symbols.methods)
        << ",\"facts\":" << symbolDefinitionsJson(symbols.facts)
        << ",\"globals\":" << symbolDefinitionsJson(symbols.globals)
        << ",\"files\":[";
    for (size_t i = 0; i < files.size(); ++i) {
        if (i) out << ",";
        out << "\"" << jsonEscape(files[i].string()) << "\"";
    }
    out << "],\"unresolvedImports\":[";
    for (size_t i = 0; i < unresolvedImports.size(); ++i) {
        if (i) out << ",";
        out << "\"" << jsonEscape(unresolvedImports[i]) << "\"";
    }
    out << "]}";
    return out.str();
}

static const char* operatorPrecedenceName(OperatorPrecedence value) {
    switch (value) {
        case OperatorPrecedence::Control: return "control";
        case OperatorPrecedence::LogicalOr: return "logical-or";
        case OperatorPrecedence::LogicalAnd: return "logical-and";
        case OperatorPrecedence::Relationship: return "relationship";
        case OperatorPrecedence::Ordering: return "ordering";
        case OperatorPrecedence::Pipeline: return "pipeline";
        case OperatorPrecedence::Additive: return "additive";
        case OperatorPrecedence::Multiplicative: return "multiplicative";
        case OperatorPrecedence::Prefix: return "prefix";
    }
    return "relationship";
}

static const char* operatorAssociativityName(OperatorAssociativity value) {
    switch (value) {
        case OperatorAssociativity::None: return "none";
        case OperatorAssociativity::Left: return "left";
        case OperatorAssociativity::Right: return "right";
    }
    return "none";
}

static const char* operatorFixityName(OperatorFixity value) {
    switch (value) {
        case OperatorFixity::Prefix: return "prefix";
        case OperatorFixity::Infix: return "infix";
        case OperatorFixity::Postfix: return "postfix";
        case OperatorFixity::Mixfix: return "mixfix";
    }
    return "infix";
}

static const char* operatorVisibilityName(OperatorVisibility value) {
    return value == OperatorVisibility::Public ? "public" : "private";
}

static const char* operatorCardinalityName(OperatorCardinality value) {
    switch (value) {
        case OperatorCardinality::One: return "one";
        case OperatorCardinality::Optional: return "optional";
        case OperatorCardinality::Many: return "many";
    }
    return "one";
}

static std::string operatorBindingsJson(const std::vector<OperatorTypeBinding>& bindings) {
    std::ostringstream out;
    out << "[";
    for (size_t i = 0; i < bindings.size(); ++i) {
        if (i) out << ",";
        out << "{\"name\":\"" << jsonEscape(bindings[i].name)
            << "\",\"nameId\":" << bindings[i].nameId
            << ",\"type\":\"" << jsonEscape(bindings[i].type)
            << "\",\"typeId\":" << bindings[i].typeId << "}";
    }
    out << "]";
    return out.str();
}

static std::string operatorMetadataJson(const OperatorRegistry& registry,
                                        const std::vector<fs::path>& files,
                                        const std::vector<std::string>& unresolvedImports) {
    std::ostringstream out;
    out << "{\"protocol\":\"felidae.operator-metadata\",\"version\":1,\"patterns\":[";
    const auto& patterns = registry.patterns();
    for (size_t i = 0; i < patterns.size(); ++i) {
        if (i) out << ",";
        const auto& pattern = patterns[i];
        out << "{\"operatorId\":" << pattern.operatorId
            << ",\"patternId\":" << pattern.patternId
            << ",\"operator\":\"" << jsonEscape(pattern.operatorName)
            << "\",\"pattern\":\"" << jsonEscape(pattern.pattern)
            << "\",\"precedence\":\"" << operatorPrecedenceName(pattern.precedence)
            << "\",\"associativity\":\"" << operatorAssociativityName(pattern.associativity)
            << "\",\"fixity\":\"" << operatorFixityName(pattern.fixity)
            << "\",\"visibility\":\"" << operatorVisibilityName(pattern.visibility)
            << "\",\"module\":\"" << jsonEscape(pattern.module) << "\",\"anchors\":[";
        for (size_t a = 0; a < pattern.anchors.size(); ++a) {
            if (a) out << ",";
            out << "\"" << jsonEscape(pattern.anchors[a]) << "\"";
        }
        out << "],\"captures\":[";
        for (size_t c = 0; c < pattern.captureNames.size(); ++c) {
            if (c) out << ",";
            out << "\"" << jsonEscape(pattern.captureNames[c]) << "\"";
        }
        out << "]}";
    }
    out << "],\"overloads\":[";
    const auto& overloads = registry.overloads();
    for (size_t i = 0; i < overloads.size(); ++i) {
        if (i) out << ",";
        const auto& overload = overloads[i];
        out << "{\"operatorId\":" << overload.operatorId
            << ",\"patternId\":" << overload.patternId
            << ",\"method\":\"" << jsonEscape(overload.methodName)
            << "\",\"methodId\":" << overload.methodId
            << ",\"captures\":" << operatorBindingsJson(overload.captures)
            << ",\"factors\":" << operatorBindingsJson(overload.factors)
            << ",\"result\":\"" << jsonEscape(overload.resultType)
            << "\",\"cardinality\":\"" << operatorCardinalityName(overload.cardinality)
            << "\",\"effects\":\"" << (overload.effect == OperatorEffect::Pure ? "pure" : "impure")
            << "\",\"visibility\":\"" << operatorVisibilityName(overload.visibility)
            << "\",\"module\":\"" << jsonEscape(overload.module) << "\"}";
    }
    out << "],\"matchers\":[";
    const auto& matchers = registry.matchers();
    for (size_t i = 0; i < matchers.size(); ++i) {
        if (i) out << ",";
        const auto& matcher = matchers[i];
        out << "{\"operatorId\":" << matcher.operatorId
            << ",\"patternId\":" << matcher.patternId
            << ",\"method\":\"" << jsonEscape(matcher.methodName)
            << "\",\"methodId\":" << matcher.methodId
            << ",\"captures\":" << operatorBindingsJson(matcher.captures)
            << ",\"produces\":" << operatorBindingsJson(matcher.produces)
            << ",\"visibility\":\"" << operatorVisibilityName(matcher.visibility)
            << "\",\"module\":\"" << jsonEscape(matcher.module) << "\"}";
    }
    out << "],\"files\":[";
    for (size_t i = 0; i < files.size(); ++i) {
        if (i) out << ",";
        out << "\"" << jsonEscape(files[i].string()) << "\"";
    }
    out << "],\"unresolvedImports\":[";
    for (size_t i = 0; i < unresolvedImports.size(); ++i) {
        if (i) out << ",";
        out << "\"" << jsonEscape(unresolvedImports[i]) << "\"";
    }
    out << "]}";
    return out.str();
}

static std::string versionJson(const char* toolName) {
    std::ostringstream out;
    out << "{\"name\":\"" << jsonEscape(toolName)
        << "\",\"version\":\"" << jsonEscape(LANGUAGE_VERSION) << "\"}";
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

static std::string symbolsFileJson(const fs::path& path, bool loadImports) {
    try {
        AstAnalysisSession analysis;
        auto loaded = Tooling::loadProgramStatements(
            path,
            loadImports,
            [&](const std::shared_ptr<Statement>& statement) {
                analysis.consume(statement);
            });
        return symbolsJson(analysis.symbols(), loaded.files, loaded.unresolvedImports);
    } catch (const std::exception& e) {
        return errorJson(e.what());
    }
}

static std::string operatorsFileJson(const fs::path& path, bool loadImports) {
    try {
        std::vector<std::shared_ptr<ClauseStmt>> clauses;
        auto loaded = Tooling::loadProgramStatements(
            path, loadImports, [&](const std::shared_ptr<Statement>& statement) {
                if (auto clause = std::dynamic_pointer_cast<ClauseStmt>(statement)) {
                    clauses.push_back(std::move(clause));
                }
            });
        if (!loaded.operators) return errorJson("Operator metadata registry was not created");
        for (const auto& clause : clauses) {
            for (const auto& annotation : clause->annotations) {
                if (annotation.builtinId != BuiltinId::OverloadAnnotation &&
                    annotation.builtinId != BuiltinId::MatcherAnnotation) continue;
                auto parsed = decodeOperatorAnnotation(annotation);
                const auto* pattern = parsed.pattern.empty()
                    ? loaded.operators->findPatternByOperator(parsed.operatorName)
                    : loaded.operators->findPattern(parsed.operatorName, parsed.pattern);
                if (!pattern) throw std::runtime_error(
                    "Operator annotation pattern was not registered for metadata");
                if (!parsed.hasVisibility) {
                    parsed.visibility = loaded.operators->visibilityForPattern(
                        pattern->patternId, clause->module);
                }
                if (annotation.builtinId == BuiltinId::MatcherAnnotation) {
                    loaded.operators->registerMatcher(makeOperatorMatcherDefinition(
                        parsed, *pattern, clause->head.name, clause->head.nameId, clause->module));
                } else {
                    loaded.operators->registerOverload(makeOperatorOverloadDefinition(
                        parsed, *pattern, clause->head.name, clause->head.nameId, clause->module));
                }
            }
        }
        return operatorMetadataJson(
            *loaded.operators, loaded.files, loaded.unresolvedImports);
    } catch (const std::exception& e) {
        return errorJson(e.what());
    }
}

static void writeLspMessage(const std::string& body) {
    std::cout << "Content-Length: " << body.size() << "\r\n\r\n" << body << std::flush;
}

// LSP SymbolKind values used for Felidae declarations.
constexpr int kSymbolKindMethod = 6;
constexpr int kSymbolKindConstant = 14;
constexpr int kSymbolKindStruct = 23;

// Builds a textDocument/documentSymbol result from the analyzer's own symbol
// table, so the outline comes from the real parse rather than each editor
// re-deriving declarations from source text with its own regex.
static std::string documentSymbolsJson(const SymbolSummary& symbols) {
    std::ostringstream out;
    out << "[";
    bool first = true;
    auto emit = [&](const std::vector<SymbolDefinition>& group, int kind) {
        for (const auto& definition : group) {
            if (definition.spans.empty()) continue;
            const SourceSpan& span = definition.spans.back();
            const int line = std::max(0, span.startLine - 1);
            const int column = std::max(0, span.startColumn - 1);
            const int endLine = std::max(line, span.endLine - 1);
            const int endColumn = std::max(0, span.endColumn - 1);

            std::string detail;
            if (!definition.params.empty()) {
                detail = "(";
                for (size_t i = 0; i < definition.params.size(); ++i) {
                    if (i) detail += ", ";
                    detail += definition.params[i].name;
                    if (!definition.params[i].type.empty()) {
                        detail += ": " + definition.params[i].type;
                    }
                }
                detail += ")";
            }

            if (!first) out << ",";
            first = false;
            out << "{\"name\":\"" << jsonEscape(definition.name)
                << "\",\"kind\":" << kind
                << ",\"detail\":\"" << jsonEscape(detail)
                << "\",\"range\":{\"start\":{\"line\":" << line
                << ",\"character\":" << column
                << "},\"end\":{\"line\":" << endLine
                << ",\"character\":" << endColumn
                << "}},\"selectionRange\":{\"start\":{\"line\":" << line
                << ",\"character\":" << column
                << "},\"end\":{\"line\":" << line
                << ",\"character\":" << column << "}}}";
        }
    };
    emit(symbols.methods, kSymbolKindMethod);
    emit(symbols.facts, kSymbolKindStruct);
    emit(symbols.globals, kSymbolKindConstant);
    out << "]";
    return out.str();
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
        size_t endLineKey = checkJson.rfind("\"endLine\"", pos);
        size_t endColumnKey = checkJson.rfind("\"endColumn\"", pos);
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
        // diagnosticsJson() always emits endLine/endColumn alongside
        // line/column (see diagnosticsJson in this file), so a real span is
        // normally available; only synthesize a single-character range as a
        // last resort (e.g. the minimal shape errorJson() emits).
        int endLine = endLineKey != std::string::npos && endLineKey > lineKey
            ? std::max(0, readNumber(endLineKey, line + 1) - 1)
            : line;
        int endColumn = endColumnKey != std::string::npos && endColumnKey > columnKey
            ? std::max(0, readNumber(endColumnKey, column + 2) - 1)
            : column + 1;
        if (!first) out << ",";
        first = false;
        out << "{\"range\":{\"start\":{\"line\":" << line << ",\"character\":" << column
            << "},\"end\":{\"line\":" << endLine << ",\"character\":" << endColumn
            << "}},\"severity\":" << severity
            << ",\"source\":\"Felidae Debugger\",\"message\":\"" << jsonEscape(message) << "\"}";
        pos += 9;
    }
    // `]` closes diagnostics, then one `}` for params and one for the
    // envelope. This emitted a third `}`, so every publishDiagnostics
    // notification was malformed JSON and a spec-compliant client dropped
    // the connection on it.
    out << "]}}";
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

// Reads params.position.line / .character out of a request body. The position
// object is the only one carrying these keys in the requests handled here.
static int lspPositionLine(const std::string& body) {
    const size_t key = body.find("\"line\"");
    if (key == std::string::npos) return 0;
    const size_t colon = body.find(':', key);
    return colon == std::string::npos ? 0 : std::max(0, std::atoi(body.c_str() + colon + 1));
}

static int lspPositionCharacter(const std::string& body) {
    const size_t key = body.find("\"character\"");
    if (key == std::string::npos) return 0;
    const size_t colon = body.find(':', key);
    return colon == std::string::npos ? 0 : std::max(0, std::atoi(body.c_str() + colon + 1));
}

// Analyses whichever text the server currently holds for `uri`, falling back
// to reading the file when the client has not sent contents.
static SymbolSummary symbolsForDocument(
    const std::map<std::string, std::string>& documents,
    const std::string& uri) {
    AstAnalysisSession analysis;
    try {
        const auto existing = documents.find(uri);
        if (existing != documents.end()) {
            // Unsaved editor contents: parse what the client sent, so the
            // outline tracks the buffer rather than the file on disk.
            Program program = Tooling::parseText(existing->second);
            for (const auto& statement : program.statements) analysis.consume(statement);
        } else {
            Tooling::loadProgramStatements(
                fileUriToPath(uri),
                false,
                [&](const std::shared_ptr<Statement>& statement) { analysis.consume(statement); });
        }
    } catch (const std::exception&) {
        // A syntax error mid-edit is normal; report whatever parsed.
    }
    return analysis.symbols();
}

// Namespaced declarations are written with either separator (`Dog.membership`
// or `Dog:membership`), so compare on a single normalized form.
static std::string normalizeSymbolLookupName(std::string name) {
    for (char& ch : name) {
        if (ch == '.') ch = ':';
    }
    return name;
}

// Word under the given zero-based position, using Felidae's identifier rules
// (letters, digits, underscore, plus `.`/`:` for namespaced names).
static std::string wordAtPosition(const std::string& text, int line, int character) {
    size_t offset = 0;
    for (int current = 0; current < line; ++current) {
        const size_t newline = text.find('\n', offset);
        if (newline == std::string::npos) return {};
        offset = newline + 1;
    }
    const size_t lineStart = offset;
    size_t lineEnd = text.find('\n', lineStart);
    if (lineEnd == std::string::npos) lineEnd = text.size();
    size_t cursor = std::min(lineStart + static_cast<size_t>(character), lineEnd);

    auto isNameChar = [](char ch) {
        return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '.' || ch == ':';
    };
    size_t start = cursor;
    size_t end = cursor;
    while (start > lineStart && isNameChar(text[start - 1])) --start;
    while (end < lineEnd && isNameChar(text[end])) ++end;
    if (start >= end) return {};
    std::string word = text.substr(start, end - start);
    while (!word.empty() && (word.front() == '.' || word.front() == ':')) word.erase(word.begin());
    while (!word.empty() && (word.back() == '.' || word.back() == ':')) word.pop_back();
    return word;
}

// textDocument/definition: resolve the name under the cursor against the
// analyzer's symbol table, which already records every declaration's span.
static std::string definitionJson(
    const std::map<std::string, std::string>& documents,
    const std::string& uri,
    int line,
    int character) {
    std::string text;
    const auto existing = documents.find(uri);
    if (existing != documents.end()) {
        text = existing->second;
    } else {
        std::ifstream input(fileUriToPath(uri), std::ios::binary);
        if (input) {
            text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        }
    }
    const std::string name = wordAtPosition(text, line, character);
    if (name.empty()) return "null";

    const SymbolSummary symbols = symbolsForDocument(documents, uri);
    const std::string normalized = normalizeSymbolLookupName(name);
    for (const auto* group : {&symbols.methods, &symbols.facts, &symbols.globals}) {
        for (const auto& definition : *group) {
            if (normalizeSymbolLookupName(definition.name) != normalized) continue;
            if (definition.spans.empty()) continue;
            const SourceSpan& span = definition.spans.back();
            std::ostringstream out;
            out << "{\"uri\":\"" << jsonEscape(uri)
                << "\",\"range\":{\"start\":{\"line\":" << std::max(0, span.startLine - 1)
                << ",\"character\":" << std::max(0, span.startColumn - 1)
                << "},\"end\":{\"line\":" << std::max(0, span.endLine - 1)
                << ",\"character\":" << std::max(0, span.endColumn - 1) << "}}}";
            return out.str();
        }
    }
    return "null";
}

static int runLspServer() {
#ifdef _WIN32
    // The CRT's default text-mode translation rewrites every '\n' written to
    // stdout as "\r\n" - since writeLspMessage()/publishDiagnostics() already
    // write explicit "\r\n" header terminators, that turned into "\r\r\n" on
    // Windows, corrupting the LSP wire format for any real client. Binary
    // mode on stdin avoids the equivalent corruption when reading message
    // bodies containing embedded newlines.
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    std::map<std::string, std::string> documents;
    std::string body;
    while (readLspMessage(body)) {
        std::string method = extractJsonString(body, "method");
        std::string id = extractJsonId(body);
        if (method == "initialize") {
            writeLspMessage(
                "{\"jsonrpc\":\"2.0\",\"id\":" + id +
                ",\"result\":{\"capabilities\":{\"textDocumentSync\":1,"
                "\"documentSymbolProvider\":true,"
                "\"definitionProvider\":true},"
                "\"serverInfo\":{\"name\":\"felidae_debug\",\"version\":\"" +
                jsonEscape(LANGUAGE_VERSION) + "\"}}}");
        } else if (method == "textDocument/documentSymbol") {
            const std::string uri = extractJsonString(body, "uri");
            writeLspMessage(
                "{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":" +
                documentSymbolsJson(symbolsForDocument(documents, uri)) + "}");
        } else if (method == "textDocument/definition") {
            const std::string uri = extractJsonString(body, "uri");
            writeLspMessage(
                "{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":" +
                definitionJson(documents, uri, lspPositionLine(body), lspPositionCharacter(body)) +
                "}");
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
        } else if (method == "textDocument/didClose") {
            // Without this the server kept every document it had ever seen,
            // growing without bound over a long editing session.
            documents.erase(extractJsonString(body, "uri"));
        } else if (id != "null") {
            // A request (has an id, so the client is waiting on a reply) for
            // a method this diagnostics-only server doesn't implement, e.g.
            // textDocument/completion or textDocument/hover. Previously this
            // fell through with no response at all, leaving a spec-compliant
            // client blocked forever on that id. Notifications (no id) are
            // still silently ignored, per the LSP spec.
            writeLspMessage(
                "{\"jsonrpc\":\"2.0\",\"id\":" + id +
                ",\"error\":{\"code\":-32601,\"message\":\"Method not found: " +
                jsonEscape(method) + "\"}}");
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
        if (options.version) {
            std::cout << versionJson("felidae_debug") << "\n" << std::flush;
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

        if (options.symbolsJson) {
            std::cout << symbolsFileJson(*options.programFile, options.loadImports) << "\n" << std::flush;
            return 0;
        }
        if (options.operatorsJson) {
            std::cout << operatorsFileJson(*options.programFile, options.loadImports) << "\n" << std::flush;
            return 0;
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
