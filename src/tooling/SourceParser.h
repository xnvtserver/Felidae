#pragma once

#include "AST.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Felidae::Tooling {

struct LoadedProgram {
    Program program;
    std::vector<std::filesystem::path> files;
    std::vector<std::string> unresolvedImports;
    std::shared_ptr<OperatorRegistry> operators;
};

struct LoadedSources {
    std::vector<std::filesystem::path> files;
    std::vector<std::string> unresolvedImports;
    std::shared_ptr<OperatorRegistry> operators;
};

Program parseText(std::string text);
Program parseFile(const std::filesystem::path& path);
void parseFileStatements(
    const std::filesystem::path& path,
    const std::function<void(std::shared_ptr<Statement>)>& consume);
LoadedProgram loadProgram(const std::filesystem::path& entryFile, bool loadImports);
LoadedSources loadProgramStatements(
    const std::filesystem::path& entryFile,
    bool loadImports,
    const std::function<void(const std::shared_ptr<Statement>&)>& consume);
std::filesystem::path resolveEntryPath(const std::filesystem::path& path);
std::vector<std::string> listCoreLibraries(const std::filesystem::path& startDir);

} // namespace Felidae::Tooling
