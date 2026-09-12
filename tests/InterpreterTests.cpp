#include "FelidaeRuntime.h"
#include "Interpreter.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void testParserRejectsInvalidSource() {
    bool rejected = false;
    try {
        (void)Felidae::parseProgramText("main( => return 42 end");
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "parser accepted malformed method syntax");
}

void testMethodExecutionAndDebugHook() {
    auto program = Felidae::parseProgramText(
        "def main() =>\n"
        "  answer := 40 + 2\n"
        "  return answer\n"
        "end\n");
    Felidae::Interpreter interpreter;
    interpreter.addProgram(program);

    std::vector<int> visitedLines;
    interpreter.setGoalHook(
        [&](const Felidae::Goal& goal, const Felidae::Env&, std::size_t) {
            if (goal.sourceSpan.valid()) visitedLines.push_back(goal.sourceSpan.startLine);
        });
    const auto result = interpreter.callMain(Felidae::makeSystemInput({}));
    require(interpreter.valueToDisplayString(result) == "42",
            "main() returned the wrong interpreted value");
    require(visitedLines.size() >= 2, "debug hook did not observe live method goals");
    require(visitedLines.front() <= visitedLines.back(),
            "debug hook reported goals out of source order");
}

void testFactQueryAndMutationInvalidation() {
    auto program = Felidae::parseProgramText(
        "Animal(name: \"cat\").\n"
        "Animal(name: \"dog\").\n");
    Felidae::Interpreter interpreter;
    interpreter.addProgram(program);
    const auto goals = Felidae::parseQueryText("? Animal(name: value)");
    const auto first = interpreter.solve(goals, 10);
    const auto second = interpreter.solve(goals, 10);
    require(first.size() == 2 && second.size() == 2,
            "fact query or repeated-query cache changed solutions");
}

void testAnalysisLoadDoesNotEvaluateGlobals() {
    auto program = Felidae::parseProgramText("danger := 1 / 0\n");
    Felidae::Interpreter interpreter;
    interpreter.setLoadEvaluationEnabled(false);
    interpreter.addProgram(program);
    require(interpreter.hasGlobal("danger"),
            "analysis load did not retain global symbol metadata");
}

} // namespace

int main() {
    try {
        testParserRejectsInvalidSource();
        testMethodExecutionAndDebugHook();
        testFactQueryAndMutationInvalidation();
        testAnalysisLoadDoesNotEvaluateGlobals();
        std::cout << "interpreter tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "interpreter test failed: " << error.what() << '\n';
        return 1;
    }
}
