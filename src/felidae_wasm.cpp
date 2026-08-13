#include "FelidaeRuntime.h"
#include "Interpreter.h"
#include "LegacyIrAdapter.h"

#include <emscripten/bind.h>

#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string runProgram(const std::string& source, const std::string& query) {
    try {
        if (Felidae::trim(query).empty()) {
            auto module = Felidae::compileProgramTextToIr(source);
            Felidae::IrVerifier::verify(module.ir);
            Felidae::LegacyVmRuntime runtime(module);
            Felidae::RegisterVm vm;
            const auto result = vm.execute(module.ir, runtime,
                                           Felidae::legacyVmValue(Felidae::makeSystemInput({})));
            if (!runtime.executedEntry()) {
                return "Program loaded successfully. Add main() or pass a query to execute it.";
            }
            return runtime.services().valueToString(Felidae::legacyExprFromVmValue(result));
        }
        Felidae::Interpreter interpreter;
        Felidae::Program program = Felidae::parseProgramText(source);
        Felidae::loadProgramRoot(std::filesystem::path("/playground.fx"), program, interpreter);

        const std::string trimmedQuery = Felidae::trim(query);
        if (!trimmedQuery.empty()) {
            auto queryGoals = Felidae::parseQueryText(trimmedQuery);
            auto solutions = interpreter.solve(queryGoals, 1000);
            std::ostringstream out;
            Felidae::printSolutions(interpreter, queryGoals, solutions, out);
            return out.str();
        }

        if (interpreter.hasMethod("main")) {
            auto result = interpreter.callMain(Felidae::makeSystemInput({}));
            return interpreter.valueToString(result);
        }

        return "Program loaded successfully. Add main() or pass a query to execute it.";
    } catch (const std::exception& ex) {
        return std::string("error: ") + ex.what();
    }
}

} // namespace

EMSCRIPTEN_BINDINGS(felidae_wasm) {
    emscripten::function("runProgram", &runProgram);
}
