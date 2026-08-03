#pragma once

#include "AST.h"
#include "celidae/Analytics.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Felidae::Celidae {

enum class DiagramType {
    // Structural views: what the program declares and how it is wired.
    Schema,        // fact types and their fields
    Graph,         // methods, globals, imports and the calls between them
    Er,            // entity/relationship view: facts, fields, inheritance only
    Hierarchy,     // inheritance tree, parents containing their children

    // Data views: what the declared values actually say. These are the
    // business-facing half - an IDE already draws the structural ones.
    Timeline,      // fact records ordered by a date-like field, with trend
    Stats,         // per-fact-type record, field and data-quality statistics
    Distribution,  // value distributions per field, with outliers
    Comparison,    // fact types and fields compared across measures
    Cluster        // PCA projection with k-means segments
};

// Every diagram type, in the order --help lists them.
inline constexpr DiagramType kAllDiagramTypes[] = {
    DiagramType::Schema,
    DiagramType::Graph,
    DiagramType::Er,
    DiagramType::Hierarchy,
    DiagramType::Timeline,
    DiagramType::Stats,
    DiagramType::Distribution,
    DiagramType::Comparison,
    DiagramType::Cluster
};

// Lower-case name used by --type/--template and as the JSON "mode" field.
const char* diagramTypeName(DiagramType type);

// One-line description of what the view answers, for --help and the UI.
const char* diagramTypeSummary(DiagramType type);

// Parses a --type/--template value; returns false when unrecognised.
bool parseDiagramType(const std::string& name, DiagramType& out);

// Colour per node kind. Single source of truth: the SVG export uses it
// directly and the HTML export receives it as a "palette" JSON member, so the
// two can no longer drift apart the way a duplicated JS table did.
const char* kindColor(const std::string& kind);
const std::vector<std::string>& allNodeKinds();

struct RenderNode {
    std::string id;
    std::string label;
    std::string kind;

    // Measured values behind this node. Renderers read these directly; before
    // they existed, the HTML view had to regex numbers back out of `detail`,
    // which broke silently whenever the wording of a detail string changed.
    std::map<std::string, double> metrics;
    // Unit suffix for a metric, used only when rendering `detail` as prose.
    // The metric itself stays a bare number so a chart can plot it; without
    // this, "coverage=100" is ambiguous where "coverage=100%" is not.
    std::map<std::string, std::string> units;
    // Non-numeric facts about the node (the field a timeline is ordered by,
    // a detected field type, a cluster label).
    std::map<std::string, std::string> attributes;
    // Free-text remarks that are not key/value data - truncation notices and
    // the like. `detail` below is derived from all three.
    std::vector<std::string> notes;

    // Human-readable summary, generated from metrics/attributes/notes so the
    // tooltip text and the machine-readable values cannot disagree.
    std::string detail() const;
};

struct RenderEdge {
    std::string from;
    std::string to;
    std::string label;
    std::map<std::string, double> metrics;
};

// A chart specification emitted by C++ and rendered generically by the HTML
// page. Keeping the shape of a view's charts on this side means adding a view
// does not mean adding a branch of chart-building JavaScript, and the SVG and
// HTML exports describe the same thing.
struct RenderPanel {
    // bar, hbar, histogram, line, scatter, heatmap, treemap, boxplot, parallel
    std::string type;
    std::string title;
    std::string subtitle;
    std::string color;
    std::string valueSuffix;
    std::string xLabel;
    std::string yLabel;

    std::vector<std::string> categories;
    struct Series {
        std::string name;
        std::vector<double> values;
    };
    std::vector<Series> series;

    struct Point {
        double x = 0;
        double y = 0;
        std::string label;
        std::string group;
    };
    std::vector<Point> points;

    // Nested shapes that do not fit the category/series model (a treemap's
    // tree, a heatmap's square matrix), pre-serialised as JSON members. Kept
    // as an escape hatch rather than the default so the SVG exporter can draw
    // the common panel types from structured data.
    std::string extraJson;
};

// Something worth telling the reader about the data, surfaced above the
// chart. This is the output of the analysis layer, not decoration: an
// unflagged outlier or a weak clustering silently misleads.
struct RenderInsight {
    std::string level;  // info | notice | warn
    std::string text;
};

// A fully built view: the node/edge list (used by the SVG layout engine and
// the network renderer), any chart panels, and the JSON serialization used by
// --json and embedded in the interactive HTML - all from one analysis pass.
struct RenderedGraph {
    std::vector<RenderNode> nodes;
    std::vector<RenderEdge> edges;
    std::vector<RenderPanel> panels;
    std::vector<RenderInsight> insights;
    // "network" or "panels": which representation carries this view's meaning
    // by default. A view offering both lets the reader switch.
    std::string defaultRender = "network";
    std::string json;
};

// Which view the data shape argues for, and why. Emitted with every payload so
// a reader landing on a nine-tab page is pointed at the one that will actually
// tell them something.
struct Recommendation {
    DiagramType view;
    double score = 0;
    std::string rationale;
};

std::vector<Recommendation> recommendViews(const DataShape& shape);

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

    // Measured shape of everything consumed so far, for the view recommender.
    DataShape shape() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::string graphJson(const Program& program,
                      const std::vector<std::string>& unresolvedImports = {});
std::string graphJsonEnvelope(const std::string& json);

// Self-contained interactive visualizer: the returned HTML embeds its own
// copy of cytoscape/ECharts/heroicons/Tailwind CSS (see src/celidae/webui/,
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
