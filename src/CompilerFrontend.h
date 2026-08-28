#pragma once

#include "AST.h"
#include "FelidaeIr.h"
#include "form/IrModule.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Felidae {

class MixfixStateModel;
class OperatorRegistry;

struct CompilerOptions {
  // Optional, verifier-gated compiler SSM. Normal syntax remains fully
  // deterministic; only unresolved custom mixfix assembly reaches it.
  MixfixStateModel *mixfixModel = nullptr;
  // Optional out-parameter. When set, non-fatal parser diagnostics (see
  // IntegerParser::warnings()) are appended here; a compile that produces no
  // warnings leaves it untouched. Left null by default so every existing
  // caller is unaffected.
  std::vector<AstDiagnostic> *warnings = nullptr;
};

// Source-only frontend boundary. `Program` is Felidae's syntax-shaped,
// compiler-only HIR; the Form VM never includes this header or receives its
// nodes.
std::string readSourceFile(const std::filesystem::path &path);
std::filesystem::path
resolveProgramEntryPath(const std::filesystem::path &path);
Program parseProgramFile(const std::filesystem::path &path,
                         const CompilerOptions &options = {});
Program parseProgramText(std::string text, const CompilerOptions &options = {});
Program parseProgramText(std::string text,
                         std::shared_ptr<OperatorRegistry> operators,
                         const CompilerOptions &options = {});
IrModule compileProgramTextToIr(std::string text,
                                const CompilerOptions &options = {});
IrModule compileProgramFileToIr(const std::filesystem::path &path,
                                const CompilerOptions &options = {});
std::optional<FelidaeIr> tryCompileExpressionTextToIr(const std::string &text);

} // namespace Felidae
