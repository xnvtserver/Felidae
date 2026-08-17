#pragma once

#include "AST.h"
#include "form/IrModule.h"

#include <filesystem>
#include <optional>
#include <string>

namespace Felidae {

class MixfixStateModel;

struct CompilerOptions {
    // Optional, verifier-gated compiler SSM. Normal syntax remains fully
    // deterministic; only unresolved custom mixfix assembly reaches it.
    MixfixStateModel* mixfixModel = nullptr;
};

// Source-only frontend boundary.  The Form VM never includes this header.
std::string readSourceFile(const std::filesystem::path& path);
std::filesystem::path resolveProgramEntryPath(const std::filesystem::path& path);
Program parseProgramFile(const std::filesystem::path& path, const CompilerOptions& options = {});
Program parseProgramText(std::string text, const CompilerOptions& options = {});
IrModule compileProgramTextToIr(std::string text, const CompilerOptions& options = {});
IrModule compileProgramFileToIr(const std::filesystem::path& path, const CompilerOptions& options = {});
std::optional<FelidaeIr> tryCompileExpressionTextToIr(const std::string& text);

} // namespace Felidae
