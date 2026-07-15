#pragma once

#include "AST.h"

#include <string>
#include <vector>

namespace Felidae {

struct AstDiagnostic {
    std::string severity;
    std::string message;
    int line = 1;
    int column = 1;
};

std::vector<AstDiagnostic> analyzeProgramAst(const Program& program);

} // namespace Felidae
