#pragma once

#include "AST.h"
#include "celidae/Analytics.h"

#include <nlohmann/json.hpp>

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

    // Position from Laplacian eigenmaps over the edge list, normalised to
    // 0..1. Connected nodes end up near each other because that is precisely
    // the quantity the second and third eigenvectors minimise.
    //
    // Computed here rather than in the browser because it is an eigenproblem
    // and Eigen is already here. It gives the page a layout that reflects
    // connectivity and is *stable* - a force layout settles somewhere
    // different every run, so two screenshots of the same data do not match
    // and a node a reader located once has moved by the time they look again.
    double layoutX = 0;
    double layoutY = 0;
    bool positioned = false;

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
    // True when the category axis has a real order - calendar periods,
    // histogram bins, numeric levels - rather than being a set of labels.
    //
    // This is what decides whether a reader may be offered a line chart. A
    // line asserts that the space between two categories is traversable, so
    // "January, February, March" may be joined and "brass, polymer, steel"
    // may not: the same drawing that shows a trend in the first case invents
    // one in the second, and does it convincingly.
    bool orderedCategories = false;

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

    // Nested shapes that do not fit the category/series model: a treemap's
    // tree, a heatmap's square matrix, a decision tree's branches.
    //
    // A real JSON value rather than a pre-serialised string. It used to be the
    // latter, with each view splicing together its own fragment - which meant
    // eleven separate places hand-building JSON syntax, any one of which could
    // emit an unbalanced bracket or an unescaped quote and take down the whole
    // page rather than one chart. nlohmann/json makes that class of bug
    // unrepresentable: the value is either well-formed or it does not compile.
    nlohmann::json extra;
};

// One way a panel could be drawn, and whether this panel's data supports it.
//
// Celidae picks a chart per panel from the shape of the data, and that pick is
// usually right but not always the reading a person wants - the same counts
// are sometimes easier to compare as horizontal bars, and a distribution is
// sometimes clearer as a box than as bars. So each panel now carries its own
// menu of renderings.
//
// The menu is computed here rather than in the page for the same reason
// everything else is: whether a rendering is faithful is a question about the
// data, and the page does not have the data's shape - only its values. A line
// through unordered categories is the case that matters, and no amount of
// JavaScript can tell that "brass, polymer, steel" is unordered while
// "2024, 2025, 2026" is not.
//
// Unsupported options are still listed, with the reason, rather than omitted.
// A reader who wonders why they cannot draw a line is owed the answer.
struct ChartOption {
    std::string type;       // bar | hbar | line | area | boxplot | treemap | ...
    bool available = false;
    std::string reason;     // why not, when !available
};

// The alternatives for one panel, including its current type (always
// available). Order is stable so the menu does not reshuffle between renders.
std::vector<ChartOption> chartOptionsFor(const RenderPanel& panel);

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
    // The analysis steps that produced this view, in order, each recording
    // what was measured and what it caused. Emitted so a reader can audit the
    // chain rather than being handed a chart and asked to trust it.
    std::vector<ReasoningStep> reasoning;
    // Which groups of algorithms were tried for this view and what each
    // concluded, applicable or not. A bundle that declined is a result: it
    // tells a reader the question was asked and answered, rather than leaving
    // them to infer it from a missing chart.
    std::vector<AlgorithmBundle> bundles;
    // "network" or "panels": which representation carries this view's meaning
    // by default. A view offering both lets the reader switch.
    std::string defaultRender = "network";
    std::string json;
};

// Which view the data shape argues for, and why. Emitted with every payload so
// a reader landing on a nine-tab page is pointed at the one that will actually
// tell them something.
//
// `applicable` is the part that binds. Scoring alone was advisory: a program
// with no date field scored the timeline view at zero, which dropped it from
// the ranking - but the page still drew nine tabs, the timeline tab among
// them, and a reader who clicked it got a view that had reached for any
// numeric field it could find so as to have something to draw. Ordering 249
// countries by their ISO number and calling it a timeline is not a weak
// answer, it is a wrong one, and no ranking can fix it because the view
// should not have been offered.
//
// So every view now reports a verdict, and a view that measures itself
// inapplicable says so instead of degrading. The verdict is computed from
// DataShape - the same measurements everything else reads - so "when is a
// timeline useful" is answered by "when the facts carry time", once, rather
// than being encoded per view.
struct Recommendation {
    DiagramType view;
    double score = 0;
    std::string rationale;
    // False when this program lacks what the view is built to show. Such a
    // view is still buildable on request (--type accepts it, and it will
    // explain its own emptiness), but it is not offered as a choice.
    bool applicable = true;
};

// Every view, applicable or not, best first. Callers wanting only the usable
// ones filter on `applicable`; the UI needs the rest so it can say *why* a
// view is unavailable rather than silently omitting it.
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

} // namespace Felidae::Celidae
