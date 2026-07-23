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
Program parseProgramText(const std::string& text);
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

} // namespace Felidae
