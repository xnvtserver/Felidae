#pragma once

// Compatibility shim over FelidaeRuntime.h for src/debugger/main.cpp, which
// predates the interpreter rewamp that consolidated this project's file
// loading into FelidaeRuntime.h/.cpp (parseProgramFile/parseProgramText/
// parseProgramFileStatements/loadProgramRoot/resolveProgramEntryPath/
// listCoreLibraries - the same functions the interpreter's own main.cpp
// uses to load a program). Restoring src/tooling/SourceParser.cpp's original
// bespoke implementation would just be a second, competing copy of exactly
// that loading logic; this header only renames/re-shapes the already-shared
// implementation to the debugger's original call sites instead.
//
// The one thing the old Felidae::Tooling::LoadedSources carried that isn't
// reproduced here is unresolvedImports: FelidaeRuntime.h's addImport-based
// loading does not currently surface which imports failed to resolve as a
// distinct list the way the removed SourceParser did. loadProgramStatements
// below always returns it empty; a caller that needs it back would have to
// add that reporting to Interpreter::addImport itself.

#include "AST.h"
#include "FelidaeRuntime.h"
#include "Interpreter.h"
#include "Operator.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Felidae::Tooling {

struct LoadedSources {
    std::vector<std::filesystem::path> files;
    std::vector<std::string> unresolvedImports;
    std::shared_ptr<OperatorRegistry> operators;
};

inline Program parseText(std::string text) {
    return parseProgramText(std::move(text));
}

inline std::filesystem::path resolveEntryPath(const std::filesystem::path& path) {
    return resolveProgramEntryPath(path);
}

inline std::vector<std::string> listCoreLibraries(const std::filesystem::path& startDir) {
    return Felidae::listCoreLibraries(startDir);
}

inline LoadedSources loadProgramStatements(
    const std::filesystem::path& entryFile,
    bool loadImports,
    const std::function<void(const std::shared_ptr<Statement>&)>& consume) {
    LoadedSources result;
    if (!loadImports) {
        result.operators = std::make_shared<OperatorRegistry>();
        parseProgramFileStatements(
            entryFile,
            [&](std::shared_ptr<Statement> statement) { consume(statement); },
            result.operators);
        result.files = {resolveProgramEntryPath(entryFile)};
        return result;
    }
    // Following imports means registering the whole program the same way a
    // real run would, so mixfix/annotation resolution (operators-json) sees
    // the same operator registry state a live interpreter would build.
    Interpreter interpreter;
    loadProgramRoot(entryFile, interpreter, [&](const Program& chunk) {
        for (const auto& statement : chunk.statements) consume(statement);
    });
    result.files = interpreter.loadedSourceFiles();
    result.operators = interpreter.operatorRegistry();
    return result;
}

} // namespace Felidae::Tooling
