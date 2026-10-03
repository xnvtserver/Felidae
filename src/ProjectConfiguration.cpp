#include "ProjectConfiguration.h"

#include "AST.h"
#include "FelidaeRuntime.h"

#include <cmath>
#include <stdexcept>

namespace fs = std::filesystem;

namespace Felidae {
namespace {

const Arg& singleArgument(const Call& call, const std::string& setting) {
    if (call.args.size() != 1) {
        throw std::runtime_error(
            setting + " in init.fx expects exactly one argument");
    }
    return call.args.front();
}

} // namespace

ProjectConfiguration loadProjectConfiguration(const fs::path& projectDirectory) {
    std::error_code error;
    const fs::path root = fs::absolute(projectDirectory, error).lexically_normal();
    if (error || !fs::is_directory(root, error)) {
        throw std::runtime_error(
            "Felidae project directory does not exist: " + root.string());
    }

    ProjectConfiguration result;
    result.projectDirectory = root;
    result.manifestFile = root / "init.fx";
    if (!fs::is_regular_file(result.manifestFile, error)) {
        throw std::runtime_error(
            "Felidae project requires init.fx beside the entry program: " +
            result.manifestFile.string());
    }

    const Program manifest = parseProgramFile(result.manifestFile, true);
    if (manifest.statements.empty()) {
        throw std::runtime_error(
            "init.fx is empty; declare import \"db\" and db.location(...)");
    }

    bool importsDatabase = false;
    bool hasLocation = false;
    for (const auto& statement : manifest.statements) {
        if (statement->kind() == StatementKind::Import) {
            const auto import = std::static_pointer_cast<ImportStmt>(statement);
            for (const auto& path : import->paths) {
                if (path != "db") {
                    throw std::runtime_error(
                        "init.fx supports only the builtin \"db\" import; found \"" +
                        path + "\"");
                }
                if (importsDatabase) {
                    throw std::runtime_error(
                        "init.fx imports the builtin database more than once");
                }
                importsDatabase = true;
            }
            continue;
        }

        if (statement->kind() != StatementKind::Clause) {
            throw std::runtime_error(
                "init.fx may contain only imports and database configuration calls");
        }
        const auto clause = std::static_pointer_cast<ClauseStmt>(statement);
        if (!clause->isFact() || !clause->parentNames.empty() ||
            !clause->annotations.empty()) {
            throw std::runtime_error(
                "init.fx database settings must be plain terminated calls");
        }

        if (clause->head.name == "db.location") {
            if (hasLocation) {
                throw std::runtime_error(
                    "init.fx must declare db.location exactly once");
            }
            const Arg& argument = singleArgument(clause->head, "db.location");
            if (!argument.name.empty() && argument.name != "path") {
                throw std::runtime_error(
                    "db.location accepts a positional path or path: value");
            }
            const auto location = std::dynamic_pointer_cast<StringExpr>(argument.value);
            if (!location || location->value.empty()) {
                throw std::runtime_error(
                    "db.location in init.fx requires a non-empty string path");
            }
            fs::path configured(location->value);
            result.databaseDirectory = configured.is_absolute()
                ? configured.lexically_normal()
                : (root / configured).lexically_normal();
            hasLocation = true;
            continue;
        }

        if (clause->head.name == "db.configure") {
            const Arg& argument = singleArgument(clause->head, "db.configure");
            if (!argument.name.empty() && argument.name != "options") {
                throw std::runtime_error(
                    "db.configure accepts a positional map or options: map");
            }
            const auto options = std::dynamic_pointer_cast<MapExpr>(argument.value);
            if (!options || !options->factType.empty()) {
                throw std::runtime_error(
                    "db.configure in init.fx requires a plain map");
            }
            constexpr double maximumExactInteger = 9007199254740991.0;
            for (const auto& entry : options->entries) {
                const auto number = std::dynamic_pointer_cast<NumberExpr>(entry.value);
                if (entry.key.empty() || !number || !std::isfinite(number->value) ||
                    number->value < 0 || number->value > maximumExactInteger ||
                    std::floor(number->value) != number->value ||
                    !result.databaseOptions.emplace(
                        entry.key, static_cast<std::uint64_t>(number->value)).second) {
                    throw std::runtime_error(
                        "db.configure requires unique option names and non-negative integers");
                }
            }
            continue;
        }

        throw std::runtime_error(
            "Unsupported init.fx property '" + clause->head.name + "'");
    }

    if (!importsDatabase) {
        throw std::runtime_error("init.fx must contain import \"db\".");
    }
    if (!hasLocation) {
        throw std::runtime_error(
            "init.fx must declare db.location(\"path\"). exactly once");
    }
    if (result.databaseDirectory == result.projectDirectory ||
        result.databaseDirectory == result.manifestFile) {
        throw std::runtime_error(
            "db.location must identify a dedicated RocksDB directory");
    }
    return result;
}

} // namespace Felidae
