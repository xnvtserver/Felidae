#include "FelidaeRuntime.h"
#include "Interpreter.h"

#include <emscripten/bind.h>

#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string runProgram(const std::string& source, const std::string& query) {
    try {
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
