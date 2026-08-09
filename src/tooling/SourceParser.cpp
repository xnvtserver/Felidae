#include "tooling/SourceParser.h"

#include "IntegerParser.h"
#include "SentencePieceModel.h"

#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>

namespace fs = std::filesystem;

namespace Felidae::Tooling {

namespace {

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

std::string readText(const fs::path& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open file: " + file.string());
    return std::string(std::istreambuf_iterator<char>(input), {});
}

Program parseDirect(std::string text) {
    IntegerTokenList input(felidaeSentencePieceModel(), std::move(text));
    return IntegerParser(input).parseProgram();
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

std::vector<std::string> scanImports(const fs::path& file) {
    std::vector<std::string> imports;
    const Program program = parseDirect(readText(file));
    for (const auto& statement : program.imports) {
        imports.insert(imports.end(), statement->paths.begin(), statement->paths.end());
    }
    return imports;
}

struct SourceGraph {
    std::vector<fs::path> dependencyOrder;
    std::vector<std::string> unresolvedImports;
};

SourceGraph collectSourceGraph(const fs::path& entry,
                               const fs::path& projectRoot,
                               bool loadImports) {
    SourceGraph graph;
    std::set<fs::path> visited;
    std::set<fs::path> visiting;
    const auto visit = [&](const auto& self, fs::path file) -> void {
        file = fs::absolute(file).lexically_normal();
        if (visited.count(file) > 0) return;
        if (!visiting.insert(file).second) return;
        if (loadImports) {
            for (const auto& importName : scanImports(file)) {
                auto resolved = resolveImport(file.parent_path(), projectRoot, importName);
                if (resolved.empty()) {
                    graph.unresolvedImports.push_back(importName);
                    continue;
                }
                for (const auto& dependency : resolved) self(self, dependency);
            }
        }
        visiting.erase(file);
        visited.insert(file);
        graph.dependencyOrder.push_back(std::move(file));
    };
    visit(visit, entry);
    std::sort(graph.unresolvedImports.begin(), graph.unresolvedImports.end());
    graph.unresolvedImports.erase(
        std::unique(graph.unresolvedImports.begin(), graph.unresolvedImports.end()),
        graph.unresolvedImports.end());
    return graph;
}

void parseFileWithRegistry(
    const fs::path& file,
    const std::shared_ptr<OperatorRegistry>& operators,
    const std::function<void(std::shared_ptr<Statement>)>& consume) {
    (void)operators;
    Program program = parseDirect(readText(file));
    for (auto& statement : program.statements) consume(std::move(statement));
}

} // namespace

Program parseText(std::string text) {
    return parseDirect(std::move(text));
}

Program parseFile(const fs::path& path) {
    Program program;
    parseFileStatements(path, [&](std::shared_ptr<Statement> statement) {
        program.addStatement(std::move(statement));
    });
    return program;
}

void parseFileStatements(
    const fs::path& path,
    const std::function<void(std::shared_ptr<Statement>)>& consume) {
    const fs::path entry = resolveEntryPath(path);
    Program program = parseDirect(readText(entry));
    for (auto& statement : program.statements) consume(std::move(statement));
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
    auto graph = collectSourceGraph(entry, projectRoot, loadImports);
    loaded.files = graph.dependencyOrder;
    loaded.unresolvedImports = std::move(graph.unresolvedImports);
    loaded.operators = std::make_shared<OperatorRegistry>();
    for (const auto& file : graph.dependencyOrder) {
        parseFileWithRegistry(file, loaded.operators, [&](std::shared_ptr<Statement> statement) {
            loaded.program.addStatement(std::move(statement));
        });
    }
    return loaded;
}

LoadedSources loadProgramStatements(
    const fs::path& entryFile,
    bool loadImports,
    const std::function<void(const std::shared_ptr<Statement>&)>& consume) {
    LoadedSources loaded;
    const fs::path entry = resolveEntryPath(entryFile);
    const fs::path projectRoot = findProjectRoot(entry);
    auto graph = collectSourceGraph(entry, projectRoot, loadImports);
    loaded.files = graph.dependencyOrder;
    loaded.unresolvedImports = std::move(graph.unresolvedImports);
    loaded.operators = std::make_shared<OperatorRegistry>();
    for (const auto& file : graph.dependencyOrder) {
        parseFileWithRegistry(file, loaded.operators, consume);
    }
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
