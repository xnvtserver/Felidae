#include "NativeProcess.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <thread>

namespace Felidae::NativeProcess {

std::string platform() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}

std::string exec(const std::string& command) {
#if defined(_WIN32)
    FILE* pipe = _popen(command.c_str(), "r");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif
    if (!pipe) throw Error("process.exec failed to start command");
    std::string output;
    std::array<char, 4096> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        output += buffer.data();
    }
#if defined(_WIN32)
    const int status = _pclose(pipe);
#else
    const int status = pclose(pipe);
#endif
    if (status != 0) throw Error("process.exec command failed with status " + std::to_string(status));
    return output;
}

std::string sleepMs(int milliseconds) {
    if (milliseconds < 0) throw Error("process.sleep expects non-negative milliseconds");
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    return "ok";
}

} // namespace Felidae::NativeProcess
