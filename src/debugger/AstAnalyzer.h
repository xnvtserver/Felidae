#pragma once

#include "AST.h"

#include <memory>
#include <string>
#include <vector>

namespace Felidae {

// AstDiagnostic, diagnosticFor(), and diagnosticForSpan() now live in AST.h
// so the compiler can share them without linking this analyzer.

// One declared parameter of a method/fact head, e.g. the `name: string` in
// `Greeting(name: string) =>`. `type` is the annotation when the head writes
// one (a type-like identifier such as `string`/`any`/`Person`, or the kind of
// a literal default) and is empty when the head binds a plain variable
// instead, e.g. the `e` in `HasManager(employee: e)`.
struct SymbolParameter {
    std::string name;
    std::string type;
};

struct SymbolDefinition {
    std::string name;
    std::size_t count = 0;
    std::vector<SourceSpan> spans;
    // Parameters of the last-seen declaration of this symbol. Editor
    // integrations use these for signature help / parameter info, which is
    // why they come from the parsed AST here rather than each extension
    // re-deriving them from source text with its own regex.
    std::vector<SymbolParameter> params;
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
