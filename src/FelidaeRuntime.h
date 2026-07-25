#pragma once

#include "AST.h"
#include "Interpreter.h"

#include <filesystem>
#include <memory>
#include <functional>
#include <ostream>
#include <string>
#include <vector>

namespace Felidae {

Program parseProgramFile(const std::filesystem::path& path);
Program parseProgramText(std::string text);
void parseProgramFileChunks(
    const std::filesystem::path& path,
    const std::function<void(Program&&)>& consume,
    std::size_t statementsPerChunk = 1);
std::string readSourceFile(const std::filesystem::path& path);
void readSourceLines(const std::filesystem::path& path,
                     const std::function<void(const std::string&)>& onLine);
void setProgramAstCacheEnabled(bool enabled);
void clearProgramAstCache();
std::filesystem::path resolveProgramEntryPath(const std::filesystem::path& path);
void loadProgramRoot(const std::filesystem::path& file, Interpreter& interpreter);
void loadProgramRoot(const std::filesystem::path& file,
                     const Program& program,
                     Interpreter& interpreter);
std::vector<std::shared_ptr<Goal>> parseQueryText(const std::string& query);
void printSolutions(Interpreter& interpreter,
                    const std::vector<std::shared_ptr<Goal>>& queryGoals,
                    const std::vector<Solution>& solutions,
                    std::ostream& out);
std::shared_ptr<Expr> makeSystemInput(const std::vector<std::string>& args);
std::string trim(const std::string& text);
bool isBareIdentifier(const std::string& text);

// Bare-import library names resolvable from `startDir` (the directory a
// program is loaded from), i.e. the set of valid `import "name"` values that
// resolve to a declaration file under `core/`. Editor/tooling integrations
// should call this (via `celidae --list-libraries`) instead of hand-maintaining
// a copy of the core module list.
std::vector<std::string> listCoreLibraries(const std::filesystem::path& startDir);

} // namespace Felidae
