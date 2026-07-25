#pragma once

#include "AST.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Felidae::Tooling {

struct LoadedProgram {
    Program program;
    std::vector<std::filesystem::path> files;
    std::vector<std::string> unresolvedImports;
};

Program parseText(std::string text);
Program parseFile(const std::filesystem::path& path);
LoadedProgram loadProgram(const std::filesystem::path& entryFile, bool loadImports);
std::filesystem::path resolveEntryPath(const std::filesystem::path& path);
std::vector<std::string> listCoreLibraries(const std::filesystem::path& startDir);

} // namespace Felidae::Tooling
