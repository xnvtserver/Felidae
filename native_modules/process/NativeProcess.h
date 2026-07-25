#pragma once

#include <stdexcept>
#include <string>

#if defined(_WIN32)
#define FELIDAE_PROCESS_EXPORT __declspec(dllexport)
#else
#define FELIDAE_PROCESS_EXPORT __attribute__((visibility("default")))
#endif

namespace Felidae::NativeProcess {

class Error : public std::runtime_error {
public:
    explicit Error(const std::string& message) : std::runtime_error(message) {}
};

std::string platform();
std::string exec(const std::string& command);
std::string sleepMs(int milliseconds);

} // namespace Felidae::NativeProcess

extern "C" {
FELIDAE_PROCESS_EXPORT char* felidae_native_call(const char* functionName, const char* argsJson);
FELIDAE_PROCESS_EXPORT void felidae_native_free(char* value);
}
