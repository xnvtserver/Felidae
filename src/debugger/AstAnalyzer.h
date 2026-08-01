#pragma once

#include "AST.h"

#include <memory>
#include <string>
#include <vector>

namespace Felidae {

struct AstDiagnostic {
    std::string severity;
    std::string message;
    int line = 1;
    int column = 1;
    int endLine = 1;
    int endColumn = 1;
    std::string code;
    std::string file;
};

struct SymbolDefinition {
    std::string name;
    std::size_t count = 0;
    std::vector<SourceSpan> spans;
};

struct SymbolSummary {
    std::vector<SymbolDefinition> methods;
    std::vector<SymbolDefinition> facts;
    std::vector<SymbolDefinition> globals;
};

class AstAnalysisSession {
public:
    AstAnalysisSession();
    ~AstAnalysisSession();
    AstAnalysisSession(AstAnalysisSession&&) noexcept;
    AstAnalysisSession& operator=(AstAnalysisSession&&) noexcept;
    AstAnalysisSession(const AstAnalysisSession&) = delete;
    AstAnalysisSession& operator=(const AstAnalysisSession&) = delete;

    void consume(const std::shared_ptr<Statement>& statement);
    std::vector<AstDiagnostic> finish();
    SymbolSummary symbols() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::vector<AstDiagnostic> analyzeProgramAst(const Program& program);

} // namespace Felidae
