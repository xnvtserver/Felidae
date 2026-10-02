#include "DatabaseService.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <future>
#include <iomanip>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string_view>
#include <thread>

#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

namespace Felidae {
namespace {

constexpr std::uint32_t kProtocolVersion = 2;
constexpr std::uint32_t kMaximumFrameBytes = 64U * 1024U * 1024U;
constexpr std::size_t kResponseChunkBytes = 64U * 1024U;
constexpr std::string_view kRequestMagic = "FDBQ";
constexpr std::string_view kResponseMagic = "FDBR";

class SocketRuntime {
public:
    SocketRuntime() {
#ifdef _WIN32
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            throw std::runtime_error("Cannot initialize Windows sockets");
        }
#else
        // A cancelled client closes its socket while the service may still
        // be flushing a final frame. Convert that into send()'s error return
        // instead of allowing SIGPIPE to terminate the database owner.
        std::signal(SIGPIPE, SIG_IGN);
#endif
    }
    ~SocketRuntime() {
#ifdef _WIN32
        WSACleanup();
#endif
    }
};

void closeSocket(SocketHandle socket) noexcept {
    if (socket == kInvalidSocket) return;
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

class SocketOwner {
public:
    explicit SocketOwner(SocketHandle socket = kInvalidSocket) : socket_(socket) {}
    ~SocketOwner() { closeSocket(socket_); }
    SocketOwner(const SocketOwner&) = delete;
    SocketOwner& operator=(const SocketOwner&) = delete;
    SocketOwner(SocketOwner&& other) noexcept : socket_(other.socket_) {
        other.socket_ = kInvalidSocket;
    }
    SocketHandle get() const noexcept { return socket_; }
    explicit operator bool() const noexcept { return socket_ != kInvalidSocket; }
private:
    SocketHandle socket_;
};

void sendAll(SocketHandle socket, const char* data, std::size_t size) {
    while (size != 0) {
        const int chunk = static_cast<int>(std::min<std::size_t>(size, 1U << 20U));
        const int sent = send(socket, data, chunk, 0);
        if (sent <= 0) throw std::runtime_error("Database service connection closed while sending");
        data += sent;
        size -= static_cast<std::size_t>(sent);
    }
}

void receiveAll(SocketHandle socket, char* data, std::size_t size) {
    while (size != 0) {
        const int chunk = static_cast<int>(std::min<std::size_t>(size, 1U << 20U));
        const int received = recv(socket, data, chunk, 0);
        if (received <= 0) throw std::runtime_error("Database service connection closed while receiving");
        data += received;
        size -= static_cast<std::size_t>(received);
    }
}

void appendU32(std::string& buffer, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        buffer.push_back(static_cast<char>((value >> shift) & 0xffU));
    }
}

std::uint32_t readU32(const std::string& buffer, std::size_t& offset) {
    if (buffer.size() - offset < 4) throw std::runtime_error("Truncated database service frame");
    std::uint32_t value = 0;
    for (int index = 0; index < 4; ++index) {
        value = (value << 8U) | static_cast<unsigned char>(buffer[offset++]);
    }
    return value;
}

void appendBlob(std::string& buffer, const std::string& value) {
    if (value.size() > kMaximumFrameBytes) throw std::runtime_error("Database service value is too large");
    appendU32(buffer, static_cast<std::uint32_t>(value.size()));
    buffer.append(value);
}

std::string readBlob(const std::string& buffer, std::size_t& offset) {
    const auto size = readU32(buffer, offset);
    if (size > kMaximumFrameBytes || size > buffer.size() - offset) {
        throw std::runtime_error("Invalid database service value length");
    }
    std::string value = buffer.substr(offset, size);
    offset += size;
    return value;
}

void sendFrame(SocketHandle socket, const std::string& frame) {
    if (frame.size() > kMaximumFrameBytes) throw std::runtime_error("Database service frame is too large");
    std::string header;
    appendU32(header, static_cast<std::uint32_t>(frame.size()));
    sendAll(socket, header.data(), header.size());
    sendAll(socket, frame.data(), frame.size());
}

std::string receiveFrame(SocketHandle socket) {
    std::array<char, 4> header{};
    receiveAll(socket, header.data(), header.size());
    const std::string encoded(header.data(), header.size());
    std::size_t offset = 0;
    const auto size = readU32(encoded, offset);
    if (size > kMaximumFrameBytes) throw std::runtime_error("Database service frame exceeds limit");
    std::string frame(size, '\0');
    receiveAll(socket, frame.data(), frame.size());
    return frame;
}

std::string randomToken() {
    std::random_device random;
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (int index = 0; index < 8; ++index) out << std::setw(8) << random();
    return out.str();
}

struct Endpoint {
    std::uint16_t port = 0;
    std::string token;
};

class DatabaseServiceConnectionError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

std::filesystem::path endpointPath(const std::filesystem::path& directory) {
    return directory / ".felidae-service";
}

std::filesystem::path lockPath(const std::filesystem::path& directory) {
    return directory / ".felidae-service.lock";
}

class DatabaseDirectoryLock {
public:
    explicit DatabaseDirectoryLock(const std::filesystem::path& directory)
        : path_(lockPath(directory)) {}
    ~DatabaseDirectoryLock() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
#else
        if (descriptor_ >= 0) {
            flock(descriptor_, LOCK_UN);
            close(descriptor_);
        }
#endif
    }
    DatabaseDirectoryLock(const DatabaseDirectoryLock&) = delete;
    DatabaseDirectoryLock& operator=(const DatabaseDirectoryLock&) = delete;

    bool tryAcquire() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) return true;
        handle_ = CreateFileW(path_.c_str(), GENERIC_READ | GENERIC_WRITE,
                              0, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_HIDDEN, nullptr);
        return handle_ != INVALID_HANDLE_VALUE;
#else
        if (descriptor_ < 0) {
            descriptor_ = open(path_.c_str(), O_CREAT | O_RDWR,
                               S_IRUSR | S_IWUSR);
        }
        return descriptor_ >= 0 && flock(descriptor_, LOCK_EX | LOCK_NB) == 0;
#endif
    }

private:
    std::filesystem::path path_;
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int descriptor_ = -1;
#endif
};

std::optional<Endpoint> readEndpoint(const std::filesystem::path& directory) {
    std::ifstream input(endpointPath(directory), std::ios::binary);
    Endpoint endpoint;
    unsigned port = 0;
    if (!(input >> port >> endpoint.token) || port == 0 || port > 65535 || endpoint.token.empty()) {
        return std::nullopt;
    }
    endpoint.port = static_cast<std::uint16_t>(port);
    return endpoint;
}

void writeEndpoint(const std::filesystem::path& directory, const Endpoint& endpoint) {
    const auto temporary = directory / ".felidae-service.tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("Cannot write database service endpoint");
        output << endpoint.port << ' ' << endpoint.token << '\n';
        output.flush();
        if (!output) throw std::runtime_error("Cannot flush database service endpoint");
    }
#ifndef _WIN32
    chmod(temporary.c_str(), S_IRUSR | S_IWUSR);
#endif
    std::error_code error;
    std::filesystem::rename(temporary, endpointPath(directory), error);
    if (error) {
        std::filesystem::remove(endpointPath(directory), error);
        error.clear();
        std::filesystem::rename(temporary, endpointPath(directory), error);
    }
    if (error) throw std::runtime_error("Cannot publish database service endpoint: " + error.message());
}

SocketOwner connectEndpoint(const Endpoint& endpoint) {
    SocketOwner socket(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (!socket) return SocketOwner{};
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(endpoint.port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(socket.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        return SocketOwner{};
    }
    return socket;
}

#ifdef _WIN32
std::wstring quoteWindowsArgument(const std::wstring& value) {
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const wchar_t ch : value) {
        if (ch == L'\\') { ++slashes; continue; }
        if (ch == L'\"') {
            result.append(slashes * 2 + 1, L'\\');
            result.push_back(ch);
            slashes = 0;
            continue;
        }
        result.append(slashes, L'\\');
        slashes = 0;
        result.push_back(ch);
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}
#endif

void launchService(const std::filesystem::path& executable,
                   const std::filesystem::path& directory) {
#ifdef _WIN32
    std::array<wchar_t, 32768> module{};
    const DWORD moduleLength = GetModuleFileNameW(
        nullptr, module.data(), static_cast<DWORD>(module.size()));
    const std::filesystem::path program = moduleLength != 0 && moduleLength < module.size()
        ? std::filesystem::path(std::wstring(module.data(), moduleLength))
        : executable;
    std::wstring command = quoteWindowsArgument(program.wstring()) +
        L" --db-service --db " + quoteWindowsArgument(directory.wstring());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW | DETACHED_PROCESS,
                        nullptr, nullptr, &startup, &process)) {
        throw std::runtime_error("Cannot start Felidae database service");
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
#else
    std::filesystem::path program = executable;
#ifdef __APPLE__
    std::uint32_t required = 0;
    (void)_NSGetExecutablePath(nullptr, &required);
    std::string module(required, '\0');
    if (required != 0 && _NSGetExecutablePath(module.data(), &required) == 0) {
        module.resize(std::char_traits<char>::length(module.c_str()));
        program = module;
    }
#elif defined(__linux__)
    std::array<char, 4096> module{};
    const auto length = readlink("/proc/self/exe", module.data(), module.size() - 1);
    if (length > 0) program = std::string(module.data(), static_cast<std::size_t>(length));
#endif
    const pid_t child = fork();
    if (child < 0) throw std::runtime_error("Cannot fork Felidae database service");
    if (child == 0) {
        setsid();
        execl(program.c_str(), program.c_str(), "--db-service", "--db",
              directory.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
#endif
}

DatabaseServiceResponse sendRequest(const Endpoint& endpoint,
                                    const std::vector<std::string>& arguments,
                                    bool stop,
                                    std::ostream& standardOutput,
                                    std::ostream& standardError) {
    auto socket = connectEndpoint(endpoint);
    if (!socket) {
        throw DatabaseServiceConnectionError(
            "Cannot connect to Felidae database service");
    }
    std::string request(kRequestMagic);
    appendU32(request, kProtocolVersion);
    appendBlob(request, endpoint.token);
    request.push_back(stop ? 1 : 0);
    appendU32(request, static_cast<std::uint32_t>(arguments.size()));
    for (const auto& argument : arguments) appendBlob(request, argument);
    sendFrame(socket.get(), request);

    DatabaseServiceResponse result;
    for (;;) {
        const std::string response = receiveFrame(socket.get());
        std::size_t offset = 0;
        if (response.size() < kResponseMagic.size() ||
            response.compare(0, kResponseMagic.size(), kResponseMagic) != 0) {
            throw std::runtime_error("Invalid database service response");
        }
        offset += kResponseMagic.size();
        if (readU32(response, offset) != kProtocolVersion || offset >= response.size()) {
            throw std::runtime_error("Incompatible database service protocol");
        }
        const unsigned char kind = static_cast<unsigned char>(response[offset++]);
        if (kind == 1 || kind == 2) {
            std::string chunk = readBlob(response, offset);
            auto& destination = kind == 1 ? standardOutput : standardError;
            destination.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
            destination.flush();
            if (!destination) {
                throw std::runtime_error("Cannot write database service response");
            }
        } else if (kind == 3) {
            result.exitCode = static_cast<int>(readU32(response, offset));
            if (offset != response.size()) {
                throw std::runtime_error("Invalid database service completion suffix");
            }
            return result;
        } else {
            throw std::runtime_error("Unknown database service response frame");
        }
        if (offset != response.size()) {
            throw std::runtime_error("Invalid database service response suffix");
        }
    }
}

void sendResponseChunk(const SocketHandle socket, unsigned char kind,
                       const char* data, std::size_t size) {
    while (size != 0) {
        const auto chunkSize = std::min(size, kResponseChunkBytes);
        std::string encoded(kResponseMagic);
        appendU32(encoded, kProtocolVersion);
        encoded.push_back(static_cast<char>(kind));
        appendBlob(encoded, std::string(data, chunkSize));
        sendFrame(socket, encoded);
        data += chunkSize;
        size -= chunkSize;
    }
}

void sendCompletion(const SocketHandle socket, int exitCode) {
    std::string completed(kResponseMagic);
    appendU32(completed, kProtocolVersion);
    completed.push_back(3);
    appendU32(completed, static_cast<std::uint32_t>(exitCode));
    sendFrame(socket, completed);
}

// Writes each completed buffer directly as a protocol frame. send() provides
// the backpressure boundary: the interpreter never retains a complete query
// transcript, and a slow client cannot grow service memory without bound.
class ResponseStreamBuffer final : public std::streambuf {
public:
    ResponseStreamBuffer(SocketHandle socket, unsigned char kind)
        : socket_(socket), kind_(kind) {
        setp(buffer_.data(), buffer_.data() + buffer_.size());
    }

    ~ResponseStreamBuffer() override { (void)flushBuffer(); }
    bool failed() const noexcept { return failed_; }

protected:
    int_type overflow(int_type character) override {
        if (!flushBuffer()) return traits_type::eof();
        if (!traits_type::eq_int_type(character, traits_type::eof())) {
            *pptr() = traits_type::to_char_type(character);
            pbump(1);
        }
        return traits_type::not_eof(character);
    }

    std::streamsize xsputn(const char* data, std::streamsize size) override {
        if (size <= 0 || failed_) return 0;
        std::streamsize written = 0;
        while (written < size) {
            const auto available = static_cast<std::streamsize>(epptr() - pptr());
            if (available == 0 && !flushBuffer()) break;
            const auto count = std::min(size - written,
                                        static_cast<std::streamsize>(epptr() - pptr()));
            std::memcpy(pptr(), data + written, static_cast<std::size_t>(count));
            pbump(static_cast<int>(count));
            written += count;
        }
        return written;
    }

    int sync() override { return flushBuffer() ? 0 : -1; }

private:
    bool flushBuffer() noexcept {
        const auto size = static_cast<std::size_t>(pptr() - pbase());
        if (size == 0) return !failed_;
        try {
            sendResponseChunk(socket_, kind_, pbase(), size);
            pbump(-static_cast<int>(size));
            return true;
        } catch (...) {
            failed_ = true;
            return false;
        }
    }

    SocketHandle socket_;
    unsigned char kind_;
    std::array<char, kResponseChunkBytes> buffer_{};
    bool failed_ = false;
};

DatabaseServiceResponse requestWithStart(
    const std::filesystem::path& executable,
    const std::filesystem::path& directory,
    const std::vector<std::string>& arguments,
    bool stop,
    std::ostream& standardOutput,
    std::ostream& standardError) {
    const auto normalized = std::filesystem::absolute(directory).lexically_normal();
    std::filesystem::create_directories(normalized);
    if (const auto endpoint = readEndpoint(normalized)) {
        try { return sendRequest(*endpoint, arguments, stop, standardOutput, standardError); }
        catch (const DatabaseServiceConnectionError&) {}
    }
    if (stop) return DatabaseServiceResponse{0};
    launchService(std::filesystem::absolute(executable).lexically_normal(), normalized);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        if (const auto endpoint = readEndpoint(normalized)) {
            try { return sendRequest(*endpoint, arguments, false, standardOutput, standardError); }
            catch (const DatabaseServiceConnectionError&) {}
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    throw std::runtime_error("Felidae database service did not become ready");
}

} // namespace

int runDatabaseService(const std::filesystem::path& databaseDirectory,
                       std::chrono::seconds idleTimeout,
                       const DatabaseServiceHandler& handler) {
    SocketRuntime runtime;
    const auto directory = std::filesystem::absolute(databaseDirectory).lexically_normal();
    std::filesystem::create_directories(directory);
    DatabaseDirectoryLock lock(directory);
    if (!lock.tryAcquire()) return 2;
    SocketOwner listener(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (!listener) throw std::runtime_error("Cannot create database service socket");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = 0;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listener.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(listener.get(), 16) != 0) {
        throw std::runtime_error("Cannot bind database service to loopback");
    }
#ifdef _WIN32
    int addressSize = sizeof(address);
#else
    socklen_t addressSize = sizeof(address);
#endif
    if (getsockname(listener.get(), reinterpret_cast<sockaddr*>(&address), &addressSize) != 0) {
        throw std::runtime_error("Cannot inspect database service endpoint");
    }
    const Endpoint endpoint{ntohs(address.sin_port), randomToken()};
    writeEndpoint(directory, endpoint);
    using Clock = std::chrono::steady_clock;
    std::atomic_bool stopping{false};
    std::atomic_size_t activeRequests{0};
    std::atomic<Clock::rep> lastRequestTicks{
        Clock::now().time_since_epoch().count()};
    std::vector<std::future<void>> clients;
    const auto reapClients = [&] {
        clients.erase(std::remove_if(clients.begin(), clients.end(),
            [](std::future<void>& client) {
                if (client.wait_for(std::chrono::seconds(0)) !=
                    std::future_status::ready) return false;
                client.get();
                return true;
            }), clients.end());
    };
    constexpr std::size_t kMaximumConcurrentRequests = 32;
    while (!stopping.load()) {
        reapClients();
        if (activeRequests.load() >= kMaximumConcurrentRequests) {
            if (!clients.empty()) {
                (void)clients.front().wait_for(std::chrono::milliseconds(50));
            }
            continue;
        }
        fd_set readers;
        FD_ZERO(&readers);
        FD_SET(listener.get(), &readers);
        timeval timeout{1, 0};
#ifdef _WIN32
        const int ready = select(0, &readers, nullptr, nullptr, &timeout);
#else
        const int ready = select(listener.get() + 1, &readers, nullptr, nullptr, &timeout);
#endif
        if (ready < 0) throw std::runtime_error("Database service select failed");
        if (ready == 0) {
            if (idleTimeout.count() > 0 &&
                activeRequests.load() == 0) {
                const auto last = Clock::time_point(Clock::duration(
                    lastRequestTicks.load()));
                if (Clock::now() - last >= idleTimeout) break;
            }
            continue;
        }
        SocketOwner client(accept(listener.get(), nullptr, nullptr));
        if (!client) continue;
        lastRequestTicks.store(Clock::now().time_since_epoch().count());
        activeRequests.fetch_add(1);
        clients.push_back(std::async(std::launch::async,
            [client = std::move(client), &handler, &endpoint,
             &stopping, &activeRequests]() mutable {
                struct ActiveRequestScope {
                    std::atomic_size_t& count;
                    ~ActiveRequestScope() { count.fetch_sub(1); }
                } active{activeRequests};
                int exitCode = 1;
                try {
                    const std::string request = receiveFrame(client.get());
                    std::size_t offset = 0;
                    if (request.size() < kRequestMagic.size() ||
                        request.compare(0, kRequestMagic.size(), kRequestMagic) != 0) {
                        throw std::runtime_error("Invalid database service request");
                    }
                    offset += kRequestMagic.size();
                    if (readU32(request, offset) != kProtocolVersion) {
                        throw std::runtime_error("Incompatible database service protocol");
                    }
                    if (readBlob(request, offset) != endpoint.token) {
                        throw std::runtime_error("Database service authentication failed");
                    }
                    if (offset >= request.size()) {
                        throw std::runtime_error("Missing database service command");
                    }
                    const bool stop = request[offset++] != 0;
                    const auto count = readU32(request, offset);
                    std::vector<std::string> arguments;
                    arguments.reserve(count);
                    for (std::uint32_t index = 0; index < count; ++index) {
                        arguments.push_back(readBlob(request, offset));
                    }
                    if (offset != request.size()) {
                        throw std::runtime_error(
                            "Invalid database service request suffix");
                    }
                    if (stop) {
                        exitCode = 0;
                        stopping.store(true);
                    } else {
                        std::atomic_bool cancelled{false};
                        ResponseStreamBuffer outputBuffer(client.get(), 1);
                        ResponseStreamBuffer errorBuffer(client.get(), 2);
                        std::ostream standardOutput(&outputBuffer);
                        std::ostream standardError(&errorBuffer);
                        auto execution = std::async(std::launch::async, [&] {
                            return handler(arguments, cancelled,
                                           standardOutput, standardError);
                        });
                        while (execution.wait_for(std::chrono::milliseconds(50)) !=
                               std::future_status::ready) {
                            fd_set cancellationReaders;
                            FD_ZERO(&cancellationReaders);
                            FD_SET(client.get(), &cancellationReaders);
                            timeval cancellationTimeout{0, 0};
#ifdef _WIN32
                            const int readable = select(
                                0, &cancellationReaders, nullptr, nullptr,
                                &cancellationTimeout);
#else
                            const int readable = select(
                                client.get() + 1, &cancellationReaders,
                                nullptr, nullptr, &cancellationTimeout);
#endif
                            if (readable > 0) {
                                char probe = 0;
                                const int received = recv(
                                    client.get(), &probe, 1, MSG_PEEK);
                                if (received <= 0) cancelled.store(true);
                            }
                        }
                        exitCode = execution.get();
                        standardOutput.flush();
                        standardError.flush();
                        if (outputBuffer.failed() || errorBuffer.failed()) {
                            return;
                        }
                    }
                } catch (const std::exception& error) {
                    exitCode = 1;
                    try {
                        const std::string message =
                            std::string("error: ") + error.what() + "\n";
                        sendResponseChunk(
                            client.get(), 2, message.data(), message.size());
                    } catch (const std::exception&) {}
                }
                try {
                    sendCompletion(client.get(), exitCode);
                } catch (const std::exception&) {
                    // A disconnected client is the cancellation signal.
                }
            }));
    }
    for (auto& client : clients) client.get();
    std::error_code ignored;
    std::filesystem::remove(endpointPath(directory), ignored);
    return 0;
}

DatabaseServiceResponse requestDatabaseExecution(
    const std::filesystem::path& executable,
    const std::filesystem::path& databaseDirectory,
    const std::vector<std::string>& arguments,
    std::ostream& standardOutput,
    std::ostream& standardError) {
    SocketRuntime runtime;
    return requestWithStart(executable, databaseDirectory, arguments, false,
                            standardOutput, standardError);
}

bool stopDatabaseService(const std::filesystem::path& databaseDirectory) {
    SocketRuntime runtime;
    const auto directory = std::filesystem::absolute(databaseDirectory).lexically_normal();
    const auto endpoint = readEndpoint(directory);
    if (!endpoint) return false;
    try {
        std::ostringstream ignoredOutput;
        std::ostringstream ignoredError;
        return sendRequest(*endpoint, {}, true,
                           ignoredOutput, ignoredError).exitCode == 0;
    } catch (const std::exception&) {
        return false;
    }
}

int runInteractiveDatabaseOwner(
    const std::filesystem::path& databaseDirectory,
    const std::function<int()>& execution) {
    const auto directory =
        std::filesystem::absolute(databaseDirectory).lexically_normal();
    std::filesystem::create_directories(directory);
    (void)stopDatabaseService(directory);

    DatabaseDirectoryLock lock(directory);
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!lock.tryAcquire()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            throw std::runtime_error(
                "Timed out waiting for the Felidae database owner to stop");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    std::error_code ignored;
    std::filesystem::remove(endpointPath(directory), ignored);
    return execution();
}

} // namespace Felidae
