#pragma once

// AST is compiler-front-end input only. FelidaeIr and RegisterVm do not
// include this header and never receive AST values.
#include "AST.h"
#include "form/IrModule.h"

namespace Felidae {

class IrCodeGenerator {
public:
    IrModule compile(Program program) const;
};

} // namespace Felidae
