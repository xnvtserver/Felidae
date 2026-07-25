#include "tooling/SourceParser.h"

#include "Lexer.h"
#include "Parser.h"

#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>

namespace fs = std::filesystem;

namespace Felidae::Tooling {

namespace {

std::string readFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open file: " + path.string());
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    input.seekg(0, std::ios::beg);
    std::string text;
    if (size > 0) text.resize(static_cast<std::size_t>(size));
    if (!text.empty()) input.read(&text[0], static_cast<std::streamsize>(text.size()));
    if (!input && !input.eof()) throw std::runtime_error("Cannot read file: " + path.string());
    return text;
}

fs::path findProjectRoot(fs::path start) {
    std::error_code ec;
    start = fs::absolute(start, ec).lexically_normal();
    if (ec) return {};
    if (!fs::is_directory(start, ec)) start = start.parent_path();
    while (!start.empty()) {
        if (fs::is_directory(start / "core", ec) && fs::is_directory(start / "src", ec)) {
            return start;
        }
        const fs::path parent = start.parent_path();
        if (parent == start) break;
        start = parent;
    }
    return {};
}

void addFxFiles(const fs::path& directory, std::vector<fs::path>& files) {
    std::error_code ec;
    if (!fs::is_directory(directory, ec)) return;
    for (fs::recursive_directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec) && it->path().extension() == ".fx") {
            files.push_back(fs::absolute(it->path()).lexically_normal());
        }
    }
}

std::vector<fs::path> resolveImport(const fs::path& baseDir,
                                    const fs::path& projectRoot,
                                    const std::string& importName) {
    std::vector<fs::path> files;
    std::vector<fs::path> candidates;
    const bool wildcard = importName.find('*') != std::string::npos;
    std::string normalizedName = importName;
    std::replace(normalizedName.begin(), normalizedName.end(), ':', '/');

    if (wildcard) {
        const auto star = normalizedName.find('*');
        fs::path relativeDirectory = normalizedName.substr(0, star);
        candidates.push_back(baseDir / relativeDirectory);
        if (!projectRoot.empty()) {
            candidates.push_back(projectRoot / relativeDirectory);
            candidates.push_back(projectRoot / "core" / relativeDirectory);
            candidates.push_back(projectRoot / "stdlib" / relativeDirectory);
        }
        for (const auto& candidate : candidates) addFxFiles(candidate, files);
    } else {
        fs::path relative = normalizedName;
        candidates.push_back(baseDir / relative);
        if (relative.extension().empty()) candidates.push_back(baseDir / (relative.string() + ".fx"));
        if (!projectRoot.empty()) {
            candidates.push_back(projectRoot / relative);
            candidates.push_back(projectRoot / "core" / relative);
            candidates.push_back(projectRoot / "stdlib" / relative);
            if (relative.extension().empty()) {
                candidates.push_back(projectRoot / (relative.string() + ".fx"));
                candidates.push_back(projectRoot / "core" / (relative.string() + ".fx"));
                candidates.push_back(projectRoot / "stdlib" / (relative.string() + ".fx"));
            }
        }
        std::error_code ec;
        for (const auto& candidate : candidates) {
            if (fs::is_regular_file(candidate, ec)) {
                files.push_back(fs::absolute(candidate).lexically_normal());
                break;
            }
            if (fs::is_directory(candidate, ec)) {
                addFxFiles(candidate, files);
                break;
            }
        }
    }

    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());
    return files;
}

void appendProgram(Program& destination, Program source) {
    for (auto& statement : source.statements) {
        destination.addStatement(std::move(statement));
    }
}

} // namespace

Program parseText(std::string text) {
    Lexer lexer(std::move(text));
    Parser parser(lexer.tokenize());
    return parser.parseProgram();
}

Program parseFile(const fs::path& path) {
    return parseText(readFile(resolveEntryPath(path)));
}

fs::path resolveEntryPath(const fs::path& path) {
    std::error_code ec;
    fs::path normalized = fs::absolute(path, ec).lexically_normal();
    if (ec) throw std::runtime_error("Cannot resolve path: " + path.string());
    if (fs::is_directory(normalized, ec)) {
        const fs::path mainFile = normalized / "main.fx";
        if (!fs::is_regular_file(mainFile, ec)) {
            throw std::runtime_error("Project directory does not contain main.fx: " + normalized.string());
        }
        return mainFile;
    }
    if (!fs::is_regular_file(normalized, ec)) {
        throw std::runtime_error("Cannot open file: " + normalized.string());
    }
    return normalized;
}

LoadedProgram loadProgram(const fs::path& entryFile, bool loadImports) {
    LoadedProgram loaded;
    const fs::path entry = resolveEntryPath(entryFile);
    const fs::path projectRoot = findProjectRoot(entry);
    std::vector<fs::path> pending{entry};
    std::set<fs::path> visited;

    while (!pending.empty()) {
        fs::path file = fs::absolute(pending.back()).lexically_normal();
        pending.pop_back();
        if (!visited.insert(file).second) continue;

        Program parsed = parseFile(file);
        loaded.files.push_back(file);
        if (loadImports) {
            for (const auto& import : parsed.imports) {
                for (const auto& importName : import->paths) {
                    auto resolved = resolveImport(file.parent_path(), projectRoot, importName);
                    if (resolved.empty()) {
                        loaded.unresolvedImports.push_back(importName);
                    } else {
                        pending.insert(pending.end(), resolved.begin(), resolved.end());
                    }
                }
            }
        }
        appendProgram(loaded.program, std::move(parsed));
    }

    std::sort(loaded.files.begin(), loaded.files.end());
    std::sort(loaded.unresolvedImports.begin(), loaded.unresolvedImports.end());
    loaded.unresolvedImports.erase(
        std::unique(loaded.unresolvedImports.begin(), loaded.unresolvedImports.end()),
        loaded.unresolvedImports.end());
    return loaded;
}

std::vector<std::string> listCoreLibraries(const fs::path& startDir) {
    std::vector<std::string> names;
    const fs::path root = findProjectRoot(startDir);
    if (root.empty()) return names;
    std::error_code ec;
    const fs::path core = root / "core";
    for (fs::directory_iterator it(core, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec) && it->path().extension() == ".fx") {
            names.push_back(it->path().stem().string());
        }
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

} // namespace Felidae::Tooling
