#pragma once

#include "AST.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Felidae::Celidae {

enum class DiagramType {
    Schema,     // fact types and their fields
    Graph,      // methods, globals, imports and the calls between them
    Er,         // entity/relationship view: facts, fields, inheritance only
    Timeline,   // fact records ordered by a date-like field's literal value
    Hierarchy,  // inheritance tree, parents containing their children
    Stats       // per-fact-type record and field statistics
};

// Every diagram type, in the order --help lists them.
inline constexpr DiagramType kAllDiagramTypes[] = {
    DiagramType::Schema,
    DiagramType::Graph,
    DiagramType::Er,
    DiagramType::Timeline,
    DiagramType::Hierarchy,
    DiagramType::Stats
};

// Lower-case name used by --type/--template and as the JSON "mode" field.
const char* diagramTypeName(DiagramType type);

// Parses a --type/--template value; returns false when unrecognised.
bool parseDiagramType(const std::string& name, DiagramType& out);

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
// external files are needed to open the result.
//
// `payloads` carries one JSON document per DiagramType the caller wants
// available in the produced file. Keyed by type rather than passed
// positionally so adding a diagram type does not change this signature, and
// so `--template` can emit a single-view file by supplying just one entry.
// The template renders only the views it was given.
std::string standaloneHtml(const std::map<DiagramType, std::string>& payloads);

// Static vector export of one DiagramType using a server-computed layout —
// portable, no JS, suitable for embedding directly in documents/slides.
std::string standaloneSvg(const RenderedGraph& graph, DiagramType type);

} // namespace Felidae::Celidae
