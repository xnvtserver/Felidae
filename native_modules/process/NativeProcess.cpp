#include "NativeProcess.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Felidae::NativeProcess {

#if defined(_WIN32)
namespace {

struct Handle {
    HANDLE value = nullptr;

    Handle() = default;
    explicit Handle(HANDLE handle) : value(handle) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    Handle(Handle&& other) noexcept : value(other.value) {
        other.value = nullptr;
    }

    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            reset();
            value = other.value;
            other.value = nullptr;
        }
        return *this;
    }

    ~Handle() {
        reset();
    }

    void reset() {
        if (value && value != INVALID_HANDLE_VALUE) {
            CloseHandle(value);
        }
        value = nullptr;
    }

};

bool requiresShell(const std::string& command) {
    if (command.find("cmd ") == 0 || command.find("cmd.exe ") == 0) return true;
    return command.find_first_of("<>|&") != std::string::npos;
}

std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) {
        size = MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        if (size <= 0) throw Error("process.exec failed to convert command text");
        std::wstring out(static_cast<size_t>(size), L'\0');
        MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
        return out;
    }
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

std::string execDirectWindows(const std::string& command) {
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    HANDLE rawRead = nullptr;
    HANDLE rawWrite = nullptr;
    if (!CreatePipe(&rawRead, &rawWrite, &security, 0)) {
        throw Error("process.exec failed to create output pipe");
    }
    Handle readPipe(rawRead);
    Handle writePipe(rawWrite);
    SetHandleInformation(readPipe.value, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = writePipe.value;
    startup.hStdError = writePipe.value;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION processInfo{};
    std::wstring commandLine = utf8ToWide(command);
    if (!CreateProcessW(nullptr,
                        commandLine.data(),
                        nullptr,
                        nullptr,
                        TRUE,
                        CREATE_NO_WINDOW,
                        nullptr,
                        nullptr,
                        &startup,
                        &processInfo)) {
        throw Error("process.exec failed to start command");
    }

    Handle process(processInfo.hProcess);
    Handle thread(processInfo.hThread);
    writePipe.reset();

    std::string output;
    std::array<char, 4096> buffer{};
    DWORD bytesRead = 0;
    while (ReadFile(readPipe.value, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) && bytesRead > 0) {
        output.append(buffer.data(), bytesRead);
    }

    WaitForSingleObject(process.value, INFINITE);
    DWORD exitCode = 0;
    if (!GetExitCodeProcess(process.value, &exitCode)) {
        throw Error("process.exec failed to read command exit code");
    }
    if (exitCode != 0) {
        throw Error("process.exec command failed with status " + std::to_string(exitCode));
    }
    return output;
}

} // namespace
#endif

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
#if defined(__EMSCRIPTEN__)
    (void)command;
    throw Error("process.exec is not available in the browser WASM runtime");
#elif defined(_WIN32)
    if (!requiresShell(command)) return execDirectWindows(command);
    FILE* pipe = _popen(command.c_str(), "r");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif
#if !defined(__EMSCRIPTEN__)
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
#endif
}

std::string sleepMs(int milliseconds) {
    if (milliseconds < 0) throw Error("process.sleep expects non-negative milliseconds");
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    return "ok";
}

} // namespace Felidae::NativeProcess

