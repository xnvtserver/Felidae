#pragma once

#include "AST.h"
#include "Interpreter.h"
#include "ParserMetrics.h"

#include <filesystem>
#include <memory>
#include <functional>
#include <ostream>
#include <string>
#include <vector>

namespace Felidae {

// `manifest` selects project-manifest parsing (init.fx), whose plain calls are
// configuration entry calls rather than persistent facts.
Program parseProgramFile(const std::filesystem::path& path, bool manifest = false);
Program parseProgramText(
    std::string text,
    std::shared_ptr<ByteTokenizer> tokenizer = {},
    std::shared_ptr<OperatorRegistry> operators = {},
    bool manifest = false);
enum class InteractiveProgramLoad {
    Empty,
    Expression,
    Incomplete,
    Loaded
};
InteractiveProgramLoad loadInteractiveProgramText(
    std::string text,
    const std::filesystem::path& importBase,
    Interpreter& interpreter);
void parseProgramFileChunks(
    const std::filesystem::path& path,
    const std::function<void(Program&&)>& consume,
    std::size_t statementsPerChunk = 1,
    std::shared_ptr<OperatorRegistry> operators = {},
    std::shared_ptr<ByteTokenizer> tokenizer = {},
    std::shared_ptr<CallSignatureRegistry> signatures = {});
// `sourceOverride`, when given, is the text of `path` instead of reading the
// file (the path then only names the file: project directory, import base and
// diagnostics). Null, the default, reads from disk exactly as before.
void parseProgramFileStatements(
    const std::filesystem::path& path,
    const std::function<void(std::shared_ptr<Statement>)>& consume,
    std::shared_ptr<OperatorRegistry> operators = {},
    ParserMetrics* metrics = nullptr,
    std::shared_ptr<ByteTokenizer> tokenizer = {},
    std::shared_ptr<CallSignatureRegistry> signatures = {},
    const std::string* sourceOverride = nullptr);
std::string readSourceFile(const std::filesystem::path& path);
void readSourceLines(const std::filesystem::path& path,
                     const std::function<void(const std::string&)>& onLine);
std::filesystem::path resolveProgramEntryPath(const std::filesystem::path& path);
void loadProgramRoot(const std::filesystem::path& file, Interpreter& interpreter);
void loadProgramRoot(
    const std::filesystem::path& file,
    Interpreter& interpreter,
    const std::function<void(const Program&)>& afterChunk);
void loadProgramRoot(const std::filesystem::path& file,
                     const Program& program,
                     Interpreter& interpreter);
// Loads `source` as the entry program named `logicalFile`, which need not exist
// on disk: it only locates init.fx and relative imports (felidae --stdin).
void loadProgramRootFromText(const std::filesystem::path& logicalFile,
                             const std::string& source,
                             Interpreter& interpreter);
std::shared_ptr<Expr> makeSystemInput(const std::vector<std::string>& args);
std::string trim(const std::string& text);
bool isBareIdentifier(const std::string& text);

// Bare-import library names resolvable from `startDir` (the directory a
// program is loaded from), i.e. the set of valid `import "name"` values that
// resolve to a declaration file under `core/`.
std::vector<std::string> listCoreLibraries(const std::filesystem::path& startDir);

} // namespace Felidae
