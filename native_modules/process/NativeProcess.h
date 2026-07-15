#pragma once

#include <stdexcept>
#include <string>

namespace Felidae::NativeProcess {

class Error : public std::runtime_error {
public:
    explicit Error(const std::string& message) : std::runtime_error(message) {}
};

std::string platform();
std::string exec(const std::string& command);
std::string sleepMs(int milliseconds);

} // namespace Felidae::NativeProcess
