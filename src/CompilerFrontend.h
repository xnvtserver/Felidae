#pragma once

#include "AST.h"
#include "form/IrModule.h"

#include <filesystem>
#include <optional>
#include <string>

namespace Felidae {

// Source-only frontend boundary.  The Form VM never includes this header.
std::string readSourceFile(const std::filesystem::path& path);
std::filesystem::path resolveProgramEntryPath(const std::filesystem::path& path);
Program parseProgramFile(const std::filesystem::path& path);
Program parseProgramText(std::string text);
IrModule compileProgramTextToIr(std::string text);
IrModule compileProgramFileToIr(const std::filesystem::path& path);
std::optional<FelidaeIr> tryCompileExpressionTextToIr(const std::string& text);

} // namespace Felidae
