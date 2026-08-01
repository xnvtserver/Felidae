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

struct RenderNode {
    std::string id;
    std::string label;
    std::string kind;
    std::string detail;
};

struct RenderEdge {
    std::string from;
    std::string to;
    std::string label;
};

// A fully built graph for one DiagramType: both the structured node/edge
// list (used by the SVG layout engine) and its JSON serialization (used by
// --json/--inspect-graph and embedded as data in the interactive HTML), so
// neither representation duplicates the fact/method/reference classification
// logic in SchemaGraphAccumulator::buildGraph.
struct RenderedGraph {
    std::vector<RenderNode> nodes;
    std::vector<RenderEdge> edges;
    std::string json;
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
    RenderedGraph buildGraph(DiagramType type,
                             const std::vector<std::string>& unresolvedImports = {}) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::string graphJson(const Program& program,
                      const std::vector<std::string>& unresolvedImports = {});
std::string graphJsonEnvelope(const std::string& json);

// Self-contained interactive visualizer: the returned HTML embeds its own
// copy of cytoscape/chart.js/heroicons/Tailwind CSS (see src/celidae/webui/,
// vendored independently via npm and compiled into GeneratedVisualizerAssets.h
// - not read from or shared with vs-code-extension). No network access or
// external files are needed to open the result. schemaJson/graphJson/erJson
// are the three DiagramType views bundled into one file so a user can switch
// between them (and between force/tree/circle layouts within each) without
// re-running Celidae.
std::string standaloneHtml(const std::string& schemaJson,
                           const std::string& graphJson,
                           const std::string& erJson);

// Static vector export of one DiagramType using a server-computed layout —
// portable, no JS, suitable for embedding directly in documents/slides.
std::string standaloneSvg(const RenderedGraph& graph, DiagramType type);

} // namespace Felidae::Celidae
