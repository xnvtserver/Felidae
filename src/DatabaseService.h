#pragma once

#include <chrono>
#include <atomic>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

namespace Felidae {

struct DatabaseServiceResponse {
    int exitCode = 1;
};

using DatabaseServiceHandler =
    std::function<int(
        const std::vector<std::string>&, const std::atomic_bool&,
        std::ostream&, std::ostream&)>;

// Runs the sole loopback owner for one RocksDB directory. Requests are
// authenticated with a per-service random token and framed with a versioned,
// bounded binary protocol. Sessions execute concurrently inside this sole
// owner process; their RocksFactStore instances share one native DB handle,
// permit overlapping reads, and serialize write transactions.
int runDatabaseService(const std::filesystem::path& databaseDirectory,
                       std::chrono::seconds idleTimeout,
                       const DatabaseServiceHandler& handler);

// Discovers the owner recorded inside databaseDirectory, starts it when
// absent, then forwards one argv-style request and waits for its response.
DatabaseServiceResponse requestDatabaseExecution(
    const std::filesystem::path& executable,
    const std::filesystem::path& databaseDirectory,
    const std::vector<std::string>& arguments,
    std::ostream& standardOutput,
    std::ostream& standardError);

bool stopDatabaseService(const std::filesystem::path& databaseDirectory);

// Interactive debugging and the REPL must retain the caller's terminal. They
// run in the foreground while holding the same per-directory ownership lock
// as the daemon, so RocksDB still has exactly one local writer owner.
int runInteractiveDatabaseOwner(
    const std::filesystem::path& databaseDirectory,
    const std::function<int()>& execution);

} // namespace Felidae
