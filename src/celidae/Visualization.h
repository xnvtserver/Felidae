#pragma once

#include "AST.h"

#include <string>
#include <vector>

namespace Felidae::Celidae {

std::string graphJson(const Program& program,
                      const std::vector<std::string>& unresolvedImports = {});
std::string graphJsonEnvelope(const std::string& json);
std::string standaloneHtml(const std::string& json);

} // namespace Felidae::Celidae
