#pragma once

#include <memory>

namespace Felidae {

class Interpreter;

class DebugSession {
public:
    DebugSession();
    ~DebugSession();
    DebugSession(const DebugSession&) = delete;
    DebugSession& operator=(const DebugSession&) = delete;

    void attach(Interpreter& interpreter);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Felidae
