#include "DebugSession.h"

#include "FelidaeRuntime.h"
#include "Interpreter.h"
#include "Symbol.h"

#include <cstdlib>
#include <iostream>
#include <set>
#include <sstream>
#include <string>

namespace Felidae {

struct DebugSession::Impl {
    enum class StepMode { None, Into, Over, Out };

    void onGoal(const Goal& goal, const Env& env, std::size_t depth) {
        if (terminated) return;
        const int line = goal.sourceSpan.valid() ? goal.sourceSpan.startLine : 0;
        const char* reason = nullptr;
        if (breakpoints.count(line)) reason = "breakpoint";
        else if (stepMode == StepMode::Into) reason = "step";
        else if (stepMode == StepMode::Over && depth <= stepDepth) reason = "step";
        else if (stepMode == StepMode::Out && depth < stepDepth) reason = "step";
        if (!reason) return;
        stepMode = StepMode::None;
        pauseAndWait(reason, line, depth, env);
    }

    void pauseAndWait(const char* reason, int line, std::size_t depth, const Env& env) {
        std::cout << "FELIDAE_DEBUG_STOPPED reason=" << reason << " line=" << line << std::endl;
        std::string commandLine;
        while (std::getline(std::cin, commandLine)) {
            commandLine = trim(commandLine);
            if (commandLine.empty()) continue;
            std::istringstream parsed(commandLine);
            std::string command;
            parsed >> command;
            if (command == "continue") return continued();
            if (command == "next") {
                stepMode = StepMode::Over;
                stepDepth = depth;
                return continued();
            }
            if (command == "stepIn") {
                stepMode = StepMode::Into;
                return continued();
            }
            if (command == "stepOut") {
                stepMode = StepMode::Out;
                stepDepth = depth;
                return continued();
            }
            if (command == "break" || command == "clear") {
                int requestedLine = 0;
                parsed >> requestedLine;
                if (command == "break") breakpoints.insert(requestedLine);
                else breakpoints.erase(requestedLine);
                std::cout << "FELIDAE_DEBUG_BREAKPOINT_"
                          << (command == "break" ? "SET" : "CLEARED")
                          << " line=" << requestedLine << std::endl;
                continue;
            }
            if (command == "locals") {
                std::cout << "FELIDAE_DEBUG_LOCALS_BEGIN" << std::endl;
                for (const auto& binding : env) {
                    if (isInternalGeneratedSymbolId(binding.first)) continue;
                    const std::string name = symbolNameForId(binding.first);
                    if (!name.empty()) {
                        std::cout << name << " = "
                                  << interpreter->valueToDisplayString(binding.second) << std::endl;
                    }
                }
                std::cout << "FELIDAE_DEBUG_LOCALS_END" << std::endl;
                continue;
            }
            if (command == "print") {
                std::string name;
                parsed >> name;
                const auto found = env.find(name);
                std::cout << "FELIDAE_DEBUG_VALUE " << name << " = "
                          << (found != env.end()
                                  ? interpreter->valueToDisplayString(found->second)
                                  : "<unbound>")
                          << std::endl;
                continue;
            }
            if (command == "terminate" || command == "quit") terminate();
            std::cout << "FELIDAE_DEBUG_ERROR unknown command '" << command << "'" << std::endl;
        }
        terminate();
    }

    void continued() { std::cout << "FELIDAE_DEBUG_CONTINUED" << std::endl; }

    [[noreturn]] void terminate() {
        terminated = true;
        std::cout << "FELIDAE_DEBUG_TERMINATED" << std::endl;
        std::exit(0);
    }

    Interpreter* interpreter = nullptr;
    std::set<int> breakpoints;
    StepMode stepMode = StepMode::None;
    std::size_t stepDepth = 0;
    bool terminated = false;
};

DebugSession::DebugSession() : impl_(std::make_unique<Impl>()) {}
DebugSession::~DebugSession() = default;

void DebugSession::attach(Interpreter& interpreter) {
    impl_->interpreter = &interpreter;
    impl_->stepMode = Impl::StepMode::Into;
    interpreter.setGoalHook([state = impl_.get()](const Goal& goal, const Env& env, std::size_t depth) {
        state->onGoal(goal, env, depth);
    });
}

} // namespace Felidae
