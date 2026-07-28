#pragma once

#include "AST.h"

#include <memory>
#include <string>
#include <vector>

namespace Felidae::Celidae {

enum class DiagramType {
    Schema,
    Graph,
    Er
};

class SchemaGraphAccumulator {
public:
    SchemaGraphAccumulator();
    ~SchemaGraphAccumulator();
    SchemaGraphAccumulator(SchemaGraphAccumulator&&) noexcept;
    SchemaGraphAccumulator& operator=(SchemaGraphAccumulator&&) noexcept;
    SchemaGraphAccumulator(const SchemaGraphAccumulator&) = delete;
    SchemaGraphAccumulator& operator=(const SchemaGraphAccumulator&) = delete;

    void consume(const std::shared_ptr<Statement>& statement);
    std::string json(DiagramType type = DiagramType::Schema,
                     const std::vector<std::string>& unresolvedImports = {}) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::string graphJson(const Program& program,
                      const std::vector<std::string>& unresolvedImports = {});
std::string graphJsonEnvelope(const std::string& json);
std::string standaloneHtml(const std::string& json);

} // namespace Felidae::Celidae
