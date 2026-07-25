#pragma once

#include <stdexcept>
#include <string>

#if defined(_WIN32)
#define FELIDAE_HTTP_EXPORT __declspec(dllexport)
#else
#define FELIDAE_HTTP_EXPORT __attribute__((visibility("default")))
#endif

namespace Felidae::NativeHttp {

class Error : public std::runtime_error {
public:
    explicit Error(const std::string& message) : std::runtime_error(message) {}
};

std::string request(const std::string& method,
                    const std::string& url,
                    const std::string& body,
                    const std::string& contentType);

std::string serveStatic(const std::string& host,
                        int port,
                        const std::string& response,
                        const std::string& contentType);

} // namespace Felidae::NativeHttp

extern "C" {
FELIDAE_HTTP_EXPORT char* felidae_native_call(const char* functionName, const char* argsJson);
FELIDAE_HTTP_EXPORT void felidae_native_free(char* value);
}
