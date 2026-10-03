#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

namespace Felidae {

struct ProjectConfiguration {
    std::filesystem::path projectDirectory;
    std::filesystem::path manifestFile;
    std::filesystem::path databaseDirectory;
    std::map<std::string, std::uint64_t> databaseOptions;
};

// init.fx is Felidae's project manifest. It is resolved only from the entry
// program's directory (or the current directory for the file-less REPL); no
// parent search or implicit database fallback is permitted.
ProjectConfiguration loadProjectConfiguration(
    const std::filesystem::path& projectDirectory);

} // namespace Felidae
