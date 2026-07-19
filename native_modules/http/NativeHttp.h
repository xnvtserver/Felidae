#pragma once

#include <stdexcept>
#include <string>

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
