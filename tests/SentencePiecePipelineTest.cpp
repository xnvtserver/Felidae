#include "FelidaeRuntime.h"
#include "Interpreter.h"

#include <cassert>
#include <memory>

int main() {
    // This covers the production path: source -> SentencePiece IDs -> integer
    // parser -> executable intermediate tree -> interpreter.
    const auto program = Felidae::parseProgramFile(FELIDAE_PIPELINE_FIXTURE_PATH);
    assert(program.clauses.size() == 1);
    assert(program.clauses.front()->head.name == "main");

    Felidae::Interpreter interpreter;
    interpreter.addProgram(program);
    assert(interpreter.hasMethod("main"));

    const auto result = interpreter.callMain(
        std::make_shared<Felidae::ArrayExpr>(std::vector<std::shared_ptr<Felidae::Expr>>{}));
    const auto returned = std::dynamic_pointer_cast<Felidae::MapExpr>(result);
    assert(returned);
    assert(returned->entries.size() == 1);
    assert(returned->entries.front().key == "answer");
    const auto answer = std::dynamic_pointer_cast<Felidae::NumberExpr>(
        returned->entries.front().value);
    assert(answer && answer->value == 42.0);
}
