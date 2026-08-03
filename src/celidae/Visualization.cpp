#include "celidae/Visualization.h"

#include "BuiltinRegistry.h"
#include "celidae/GeneratedVisualizerAssets.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <functional>
#include <iomanip>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_map>

namespace Felidae::Celidae {

namespace {

// ---------------------------------------------------------------------------
// JSON serialization primitives
// ---------------------------------------------------------------------------

std::string jsonEscape(const std::string& value) {
    std::ostringstream out;
    for (const unsigned char ch : value) {
        switch (ch) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (ch < 0x20) {
                    out << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
                        << static_cast<int>(ch) << std::dec;
                } else {
                    out << static_cast<char>(ch);
                }
        }
    }
    return out.str();
}

std::string quoted(const std::string& value) {
    return "\"" + jsonEscape(value) + "\"";
}

// JSON has no representation for NaN or infinity, and emitting either
// produces a document that JSON.parse rejects outright - the whole page would
// fail to load because one statistic divided by zero. `null` is the defined
// way to say "no value here".
std::string jsonNumber(double value) {
    if (!std::isfinite(value)) return "null";
    std::ostringstream out;
    out << std::setprecision(10) << value;
    return out.str();
}

std::string jsonNumberArray(const std::vector<double>& values) {
    std::ostringstream out;
    out << "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) out << ",";
        out << jsonNumber(values[i]);
    }
    out << "]";
    return out.str();
}

std::string jsonStringArray(const std::vector<std::string>& values) {
    std::ostringstream out;
    out << "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) out << ",";
        out << quoted(values[i]);
    }
    out << "]";
    return out.str();
}

// Compact rendering for prose: 3 significant decimals at most, and no
// trailing ".0" on whole numbers, so a detail line reads "records=42" rather
// than "records=42.000000".
std::string displayNumber(double value) {
    if (!std::isfinite(value)) return "n/a";
    if (value == std::floor(value) && std::fabs(value) < 1e15) {
        std::ostringstream out;
        out << static_cast<long long>(value);
        return out.str();
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << value;
    return out.str();
}

std::string nodeId(const std::string& kind, const std::string& name) {
    return kind + ":" + name;
}

std::string stableEdgeId(const std::string& identity) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const unsigned char ch : identity) {
        hash ^= ch;
        hash *= 1099511628211ull;
    }
    std::ostringstream out;
    out << "edge:" << std::hex << hash;
    return out.str();
}

bool hasSuffix(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
        value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

void visitReferenceAttachments(const std::shared_ptr<Goal>& goal,
                               const std::function<void(const Call&)>& visitAttachment) {
    if (!goal) return;
    if (auto call = std::dynamic_pointer_cast<CallGoal>(goal)) {
        if (hasSuffix(call->call.name, ":references")) visitAttachment(call->call);
    } else if (auto conditional = std::dynamic_pointer_cast<IfGoal>(goal)) {
        visitReferenceAttachments(conditional->condition, visitAttachment);
        for (const auto& child : conditional->thenBranch) visitReferenceAttachments(child, visitAttachment);
        for (const auto& child : conditional->elseBranch) visitReferenceAttachments(child, visitAttachment);
    } else if (auto group = std::dynamic_pointer_cast<GroupGoal>(goal)) {
        for (const auto& child : group->goals) visitReferenceAttachments(child, visitAttachment);
    } else if (auto alternatives = std::dynamic_pointer_cast<OrGoal>(goal)) {
        for (const auto& branch : alternatives->branches) {
            for (const auto& child : branch) visitReferenceAttachments(child, visitAttachment);
        }
    } else if (auto where = std::dynamic_pointer_cast<WhereGoal>(goal)) {
        visitReferenceAttachments(where->condition, visitAttachment);
    }
}

// Literal text of an argument value, or empty when it is not a literal.
// Only literals are meaningful here: an expression's value is not known
// without executing the program, which Celidae deliberately does not do.
std::string literalText(const std::shared_ptr<Expr>& value) {
    if (auto text = std::dynamic_pointer_cast<StringExpr>(value)) return text->value;
    if (auto number = std::dynamic_pointer_cast<NumberExpr>(value)) {
        std::ostringstream out;
        out << std::setprecision(10) << number->value;
        return out.str();
    }
    if (auto boolean = std::dynamic_pointer_cast<BoolExpr>(value)) {
        return boolean->value ? "true" : "false";
    }
    return {};
}

// ---------------------------------------------------------------------------
// Graph assembly
// ---------------------------------------------------------------------------

// Accumulates a view as structured node/edge/panel records (shared by every
// output format) plus a JSON serialization on demand, so the JSON, HTML data
// payload, and SVG exporter all read from one analysis pass instead of three
// parallel ones.
struct GraphWriter {
    // A deque, not a vector, and deliberately: node() hands out a reference
    // that callers hold while adding further nodes (a fact node updated after
    // its fields are emitted, for instance). A vector would reallocate and
    // leave every outstanding reference dangling - a crash that would surface
    // only on inputs large enough to trigger a regrow.
    std::deque<RenderNode> nodes;
    std::vector<RenderEdge> edges;
    std::vector<RenderPanel> panels;
    std::vector<RenderInsight> insights;
    // id -> position, so re-referencing an existing node stays O(log n)
    // instead of rescanning every node emitted so far.
    std::map<std::string, std::size_t> nodeIndex;
    std::set<std::string> edgeIds;
    std::string defaultRender = "network";

    RenderNode& node(const std::string& id,
                     const std::string& label,
                     const std::string& kind) {
        const auto existing = nodeIndex.find(id);
        if (existing != nodeIndex.end()) return nodes[existing->second];
        nodeIndex.emplace(id, nodes.size());
        RenderNode created;
        created.id = id;
        created.label = label;
        created.kind = kind;
        nodes.push_back(std::move(created));
        return nodes.back();
    }

    void edge(const std::string& from, const std::string& to, const std::string& label) {
        const std::string identity = from + "\n" + to + "\n" + label;
        if (!edgeIds.insert(identity).second) return;
        edges.push_back(RenderEdge{from, to, label, {}});
    }

    void insight(const std::string& level, const std::string& text) {
        insights.push_back(RenderInsight{level, text});
    }

    RenderPanel& panel(const std::string& type, const std::string& title) {
        RenderPanel created;
        created.type = type;
        created.title = title;
        panels.push_back(std::move(created));
        return panels.back();
    }
};

// Plain-language rendering of a feature column's pull on a segment. A numeric
// column reads as "high price"; a one-hot column reads as "mostly Engineer
// roles", because "high role=Engineer" describes the encoding rather than the
// records.
std::string describeDriver(const std::string& column, double weight) {
    const std::size_t separator = column.find('=');
    if (separator == std::string::npos) {
        return std::string(weight > 0 ? "high " : "low ") + column;
    }
    const std::string field = column.substr(0, separator);
    const std::string level = column.substr(separator + 1);
    return std::string(weight > 0 ? "mostly " : "rarely ") + level + " " + field;
}

std::string panelJson(const RenderPanel& panel) {
    std::ostringstream out;
    out << "{\"type\":" << quoted(panel.type)
        << ",\"title\":" << quoted(panel.title);
    if (!panel.subtitle.empty()) out << ",\"subtitle\":" << quoted(panel.subtitle);
    if (!panel.color.empty()) out << ",\"color\":" << quoted(panel.color);
    if (!panel.valueSuffix.empty()) out << ",\"valueSuffix\":" << quoted(panel.valueSuffix);
    if (!panel.xLabel.empty()) out << ",\"xLabel\":" << quoted(panel.xLabel);
    if (!panel.yLabel.empty()) out << ",\"yLabel\":" << quoted(panel.yLabel);
    if (!panel.categories.empty()) out << ",\"categories\":" << jsonStringArray(panel.categories);
    if (!panel.series.empty()) {
        out << ",\"series\":[";
        for (std::size_t i = 0; i < panel.series.size(); ++i) {
            if (i) out << ",";
            out << "{\"name\":" << quoted(panel.series[i].name)
                << ",\"values\":" << jsonNumberArray(panel.series[i].values) << "}";
        }
        out << "]";
    }
    if (!panel.points.empty()) {
        out << ",\"points\":[";
        for (std::size_t i = 0; i < panel.points.size(); ++i) {
            if (i) out << ",";
            out << "{\"x\":" << jsonNumber(panel.points[i].x)
                << ",\"y\":" << jsonNumber(panel.points[i].y)
                << ",\"label\":" << quoted(panel.points[i].label)
                << ",\"group\":" << quoted(panel.points[i].group) << "}";
        }
        out << "]";
    }
    if (!panel.extraJson.empty()) out << "," << panel.extraJson;
    out << "}";
    return out.str();
}

std::string scriptSafeJson(std::string json) {
    for (size_t pos = json.find("</"); pos != std::string::npos; pos = json.find("</", pos + 3)) {
        json.replace(pos, 2, "<\\/");
    }
    return json;
}

std::string toUpperAscii(const std::string& value) {
    std::string out = value;
    for (char& ch : out) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return out;
}

void replaceToken(std::string& text, const std::string& token, const std::string& value) {
    const size_t pos = text.find(token);
    if (pos == std::string::npos) {
        throw std::runtime_error("Celidae visualizer template is missing token: " + token);
    }
    text.replace(pos, token.size(), value);
}

void visitExpr(const std::shared_ptr<Expr>& expr,
               const std::function<void(const std::string&)>& visitCall) {
    if (!expr) return;
    if (auto term = std::dynamic_pointer_cast<TermExpr>(expr)) {
        visitCall(term->name);
        for (const auto& arg : term->args) visitExpr(arg.value, visitCall);
    } else if (auto lambda = std::dynamic_pointer_cast<LambdaExpr>(expr)) {
        visitExpr(lambda->source, visitCall);
        visitExpr(lambda->body, visitCall);
        visitExpr(lambda->right, visitCall);
    } else if (auto array = std::dynamic_pointer_cast<ArrayExpr>(expr)) {
        for (const auto& item : array->items) visitExpr(item, visitCall);
    } else if (auto map = std::dynamic_pointer_cast<MapExpr>(expr)) {
        for (const auto& entry : map->entries) visitExpr(entry.value, visitCall);
    } else if (auto access = std::dynamic_pointer_cast<AccessExpr>(expr)) {
        visitExpr(access->target, visitCall);
    } else if (auto op = std::dynamic_pointer_cast<OperatorExpression>(expr)) {
        for (size_t i = 0; i < op->captureCount(); ++i) visitExpr(op->capture(i), visitCall);
    }
}

void visitGoal(const std::shared_ptr<Goal>& goal,
               const std::function<void(const std::string&)>& visitCall) {
    if (!goal) return;
    if (auto call = std::dynamic_pointer_cast<CallGoal>(goal)) {
        visitCall(call->call.name);
        for (const auto& arg : call->call.args) visitExpr(arg.value, visitCall);
    } else if (auto notGoal = std::dynamic_pointer_cast<NotGoal>(goal)) {
        // Static graph analysis records negative dependencies without
        // evaluating them.  The graph's edge label remains generic today,
        // but the referenced predicate is retained for schema/linking.
        visitCall(notGoal->call.name);
        for (const auto& arg : notGoal->call.args) visitExpr(arg.value, visitCall);
    } else if (auto assign = std::dynamic_pointer_cast<AssignGoal>(goal)) {
        visitExpr(assign->expr, visitCall);
    } else if (auto multi = std::dynamic_pointer_cast<MultiAssignGoal>(goal)) {
        visitExpr(multi->expr, visitCall);
    } else if (auto binary = std::dynamic_pointer_cast<BinaryGoal>(goal)) {
        visitExpr(binary->left, visitCall);
        visitExpr(binary->right, visitCall);
    } else if (auto where = std::dynamic_pointer_cast<WhereGoal>(goal)) {
        visitGoal(where->condition, visitCall);
    } else if (auto conditional = std::dynamic_pointer_cast<IfGoal>(goal)) {
        visitGoal(conditional->condition, visitCall);
        for (const auto& child : conditional->thenBranch) visitGoal(child, visitCall);
        for (const auto& child : conditional->elseBranch) visitGoal(child, visitCall);
    } else if (auto returned = std::dynamic_pointer_cast<ReturnGoal>(goal)) {
        for (const auto& field : returned->fields) visitExpr(field.value, visitCall);
    } else if (auto group = std::dynamic_pointer_cast<GroupGoal>(goal)) {
        for (const auto& child : group->goals) visitGoal(child, visitCall);
    } else if (auto alternatives = std::dynamic_pointer_cast<OrGoal>(goal)) {
        for (const auto& branch : alternatives->branches) {
            for (const auto& child : branch) visitGoal(child, visitCall);
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Palette
// ---------------------------------------------------------------------------

const std::vector<std::string>& allNodeKinds() {
    static const std::vector<std::string> kinds = {
        "fact", "field", "method", "global", "library",
        "external", "event", "record", "segment", "measure"
    };
    return kinds;
}

const char* kindColor(const std::string& kind) {
    if (kind == "fact") return "#18f0d7";
    if (kind == "field") return "#f7c948";
    if (kind == "method") return "#6ca8ff";
    if (kind == "global") return "#f27d9d";
    if (kind == "library") return "#9ca3af";
    if (kind == "event") return "#c4a2ff";
    if (kind == "record") return "#7bdcb5";
    if (kind == "segment") return "#ff9f6b";
    if (kind == "measure") return "#8ed1fc";
    return "#9ca3af";
}

namespace {

// A stable, readable colour per cluster/series index, drawn from the same
// palette family as the node kinds so a page never mixes two colour systems.
const char* seriesColor(std::size_t index) {
    static const char* colors[] = {
        "#18f0d7", "#ff9f6b", "#6ca8ff", "#f7c948",
        "#c4a2ff", "#f27d9d", "#7bdcb5", "#8ed1fc"
    };
    return colors[index % (sizeof(colors) / sizeof(colors[0]))];
}

std::string paletteJson() {
    std::ostringstream out;
    out << "{";
    bool first = true;
    for (const auto& kind : allNodeKinds()) {
        if (!first) out << ",";
        first = false;
        out << quoted(kind) << ":" << quoted(kindColor(kind));
    }
    out << "}";
    return out.str();
}

} // namespace

std::string RenderNode::detail() const {
    std::ostringstream out;
    bool first = true;
    auto separate = [&]() {
        if (!first) out << " ";
        first = false;
    };
    for (const auto& attribute : attributes) {
        separate();
        out << attribute.first << "=" << attribute.second;
    }
    for (const auto& metric : metrics) {
        separate();
        out << metric.first << "=" << displayNumber(metric.second);
        const auto unit = units.find(metric.first);
        if (unit != units.end()) out << unit->second;
    }
    for (const auto& note : notes) {
        separate();
        out << note;
    }
    return out.str();
}

// ---------------------------------------------------------------------------
// Accumulator
// ---------------------------------------------------------------------------

struct SchemaGraphAccumulator::Impl {
    std::map<std::string, FactProfile> facts;
    std::set<std::string> methods;
    std::set<std::string> globals;
    std::set<std::string> imports;
    std::set<std::tuple<std::string, std::string, std::string>> references;

    // Field profiling walks every sampled record of every fact type, and up to
    // nine views ask for it. Computing it once per accumulator keeps
    // `celidae --html` (which builds all views) from repeating the same scan
    // nine times.
    mutable std::map<std::string, std::vector<FieldStats>> statsCache;
    mutable bool statsComputed = false;
    // Likewise for the data shape: every view embeds the recommendation list,
    // and computing it means encoding and correlating every fact type.
    mutable DataShape shapeCache;
    mutable bool shapeComputed = false;

    const std::map<std::string, std::vector<FieldStats>>& stats() const {
        if (!statsComputed) {
            for (const auto& item : facts) statsCache[item.first] = profileFields(item.second);
            statsComputed = true;
        }
        return statsCache;
    }

    // Fact types with sampled values, largest first: the data views work
    // through them in the order a reader cares about.
    std::vector<std::string> factsByVolume() const {
        std::vector<std::string> names;
        for (const auto& item : facts) {
            if (!item.second.samples.empty()) names.push_back(item.first);
        }
        std::sort(names.begin(), names.end(), [&](const std::string& a, const std::string& b) {
            const std::size_t left = facts.at(a).records;
            const std::size_t right = facts.at(b).records;
            if (left != right) return left > right;
            return a < b;  // stable, diffable ordering on ties
        });
        return names;
    }
};

SchemaGraphAccumulator::SchemaGraphAccumulator() : impl_(std::make_unique<Impl>()) {}
SchemaGraphAccumulator::~SchemaGraphAccumulator() = default;
SchemaGraphAccumulator::SchemaGraphAccumulator(SchemaGraphAccumulator&&) noexcept = default;
SchemaGraphAccumulator& SchemaGraphAccumulator::operator=(SchemaGraphAccumulator&&) noexcept = default;

void SchemaGraphAccumulator::consume(const std::shared_ptr<Statement>& statement) {
    if (!statement) return;
    // Any structural change invalidates the derived statistics.
    impl_->statsComputed = false;
    impl_->statsCache.clear();
    impl_->shapeComputed = false;

    if (auto import = std::dynamic_pointer_cast<ImportStmt>(statement)) {
        impl_->imports.insert(import->paths.begin(), import->paths.end());
        return;
    }
    if (auto global = std::dynamic_pointer_cast<GlobalBindingStmt>(statement)) {
        impl_->globals.insert(global->name);
        visitExpr(global->expr, [&](const std::string& called) {
            impl_->references.emplace("global:" + global->name, called, "references");
        });
        return;
    }
    auto clause = std::dynamic_pointer_cast<ClauseStmt>(statement);
    if (!clause) return;
    if (clause->isFact()) {
        auto& profile = impl_->facts[clause->head.name];
        ++profile.records;
        std::set<std::string> fieldsInRecord;
        FactRecordValues values;
        for (const auto& argument : clause->head.args) {
            if (argument.name.empty()) continue;
            fieldsInRecord.insert(argument.name);
            std::string literal = literalText(argument.value);
            if (!literal.empty()) values.emplace(argument.name, std::move(literal));
        }
        for (const auto& field : fieldsInRecord) ++profile.fields[field];
        if (!values.empty() && profile.samples.size() < kMaxFactSamples) {
            profile.samples.push_back(std::move(values));
        }
        // A fact may extend more than one direct parent.  parentName is only
        // the compatibility view for older callers; using it here would make
        // ER and graph diagrams silently omit every secondary inheritance
        // edge.
        if (!clause->parentNames.empty()) {
            for (const auto& parentName : clause->parentNames) {
                impl_->facts.try_emplace(parentName);
                impl_->references.emplace(
                    "fact:" + clause->head.name,
                    parentName,
                    "extends");
            }
        } else if (!clause->parentName.empty()) {
            impl_->facts.try_emplace(clause->parentName);
            impl_->references.emplace(
                "fact:" + clause->head.name,
                clause->parentName,
                "extends");
        }
        return;
    }

    impl_->methods.insert(clause->head.name);
    auto recordCall = [&](const std::string& called) {
        // Fact attachments are parser-lowered as calls on the source value
        // (for example, "ava:depends").  The source variable is not an
        // external callable, so preserve the operation's actual semantics in
        // dependency graphs instead of emitting misleading external nodes.
        if (hasSuffix(called, ":depends")) {
            impl_->references.emplace(
                "method:" + clause->head.name, "Fact:depends", "attaches dependency");
            return;
        }
        if (hasSuffix(called, ":relate")) {
            impl_->references.emplace(
                "method:" + clause->head.name, "Fact:relate", "attaches relationship");
            return;
        }
        if (hasSuffix(called, ":references")) {
            impl_->references.emplace(
                "method:" + clause->head.name, "Fact:references", "attaches reference");
            return;
        }
        impl_->references.emplace("method:" + clause->head.name, called, "calls");
    };
    for (const auto& goal : clause->body) visitGoal(goal, recordCall);
    for (const auto& branch : clause->fallbackBranches) {
        for (const auto& goal : branch) visitGoal(goal, recordCall);
    }
    auto recordReferenceTarget = [&](const Call& call) {
        for (const auto& arg : call.args) {
            if (arg.name != "by") continue;
            const auto callable = std::dynamic_pointer_cast<VarExpr>(arg.value);
            if (!callable) continue;
            std::string target = callable->name;
            const size_t separator = target.find("::");
            if (separator != std::string::npos) target.replace(separator, 2, ":");
            impl_->references.emplace("method:" + clause->head.name, target, "references");
        }
    };
    for (const auto& goal : clause->body) visitReferenceAttachments(goal, recordReferenceTarget);
    for (const auto& branch : clause->fallbackBranches) {
        for (const auto& goal : branch) visitReferenceAttachments(goal, recordReferenceTarget);
    }
}

DataShape SchemaGraphAccumulator::shape() const {
    if (impl_->shapeComputed) return impl_->shapeCache;
    DataShape shape;
    shape.factTypes = impl_->facts.size();
    shape.methods = impl_->methods.size();
    shape.globals = impl_->globals.size();
    shape.imports = impl_->imports.size();
    for (const auto& reference : impl_->references) {
        if (std::get<2>(reference) == "extends") ++shape.inheritanceEdges;
        else ++shape.callEdges;
    }
    for (const auto& item : impl_->facts) {
        shape.records += item.second.records;
        shape.sampledRecords += item.second.samples.size();
        if (item.second.records > shape.largestFactRecords) {
            shape.largestFactRecords = item.second.records;
            shape.largestFactName = item.first;
        }
    }
    for (const auto& entry : impl_->stats()) {
        for (const auto& field : entry.second) {
            switch (field.type) {
                case FieldType::Numeric: ++shape.numericFields; break;
                case FieldType::Date: ++shape.dateFields; break;
                case FieldType::Categorical: ++shape.categoricalFields; break;
                case FieldType::Identifier: ++shape.identifierFields; break;
                case FieldType::Empty: break;
            }
            shape.outlierCount += field.outliers.size();
        }
        const auto profile = impl_->facts.find(entry.first);
        if (profile == impl_->facts.end()) continue;
        const FeatureMatrix matrix = buildFeatureMatrix(profile->second, entry.second);
        if (matrix.rowCount() >= kMinClusterRows && matrix.columnCount() >= 2) {
            shape.clusterable = true;
            shape.notableCorrelations += correlate(matrix).notable.size();
        }
    }
    impl_->shapeCache = shape;
    impl_->shapeComputed = true;
    return shape;
}

// ---------------------------------------------------------------------------
// View recommender ("which of these nine actually tells me something?")
// ---------------------------------------------------------------------------

std::vector<Recommendation> recommendViews(const DataShape& shape) {
    std::vector<Recommendation> recommendations;
    auto add = [&](DiagramType view, double score, const std::string& rationale) {
        if (score <= 0) return;
        recommendations.push_back(Recommendation{view, score, rationale});
    };

    // Each score is a statement about the data, not a preference: a view only
    // scores when the program contains the thing that view is built to show.
    if (shape.clusterable) {
        add(DiagramType::Cluster,
            0.85 + std::min(0.1, static_cast<double>(shape.notableCorrelations) * 0.02),
            std::to_string(shape.sampledRecords) + " records carry enough varying fields to "
            "separate into segments");
    }
    if (shape.numericFields + shape.dateFields >= 2) {
        add(DiagramType::Comparison, 0.7,
            std::to_string(shape.numericFields + shape.dateFields) +
            " measurable fields can be compared against each other");
    }
    if (shape.numericFields + shape.dateFields >= 1 && shape.sampledRecords >= 8) {
        add(DiagramType::Distribution, 0.75,
            "value distributions are available for " +
            std::to_string(shape.numericFields + shape.dateFields) + " measurable fields" +
            (shape.outlierCount > 0
                ? ", including " + std::to_string(shape.outlierCount) + " outlying records"
                : ""));
    }
    if (shape.dateFields >= 1) {
        add(DiagramType::Timeline, 0.8,
            std::to_string(shape.dateFields) + " date field(s) place records in time");
    }
    if (shape.inheritanceEdges >= 2) {
        add(DiagramType::Hierarchy, 0.55,
            std::to_string(shape.inheritanceEdges) + " inheritance relationships form a hierarchy");
    }
    if (shape.records > 0) {
        add(DiagramType::Stats, 0.5,
            std::to_string(shape.records) + " records across " +
            std::to_string(shape.factTypes) + " fact types");
    }
    if (shape.factTypes > 0) {
        add(DiagramType::Schema, 0.35, "fact types and their fields are declared");
    }
    if (shape.callEdges >= 3) {
        add(DiagramType::Graph, 0.3,
            std::to_string(shape.callEdges) + " call/reference relationships between methods");
    }
    if (shape.factTypes >= 2) {
        add(DiagramType::Er, 0.25, "multiple fact types with fields and inheritance");
    }

    std::sort(recommendations.begin(), recommendations.end(), [](const auto& a, const auto& b) {
        if (a.score != b.score) return a.score > b.score;
        return diagramTypeName(a.view) < diagramTypeName(b.view);
    });
    return recommendations;
}

namespace {

// ---------------------------------------------------------------------------
// Shared building blocks for the data views
// ---------------------------------------------------------------------------

// A fact type's sampled records are a partial picture once the cap bites.
// Every view built on samples says so, on the node and as an insight: a
// silently truncated chart is a wrong answer, not a smaller one.
void noteTruncation(GraphWriter& graph, const std::string& name, const FactProfile& profile) {
    if (profile.records <= profile.samples.size()) return;
    std::ostringstream text;
    text << name << ": charted from the first " << profile.samples.size() << " of "
         << profile.records << " records (Celidae samples at most " << kMaxFactSamples
         << " per fact type).";
    graph.insight("notice", text.str());
}

void addBarPanel(GraphWriter& graph,
                 const std::string& type,
                 const std::string& title,
                 const std::string& subtitle,
                 const std::vector<std::pair<std::string, double>>& entries,
                 const char* color,
                 const std::string& suffix = {}) {
    if (entries.empty()) return;
    RenderPanel& panel = graph.panel(type, title);
    panel.subtitle = subtitle;
    panel.color = color;
    panel.valueSuffix = suffix;
    RenderPanel::Series series;
    series.name = title;
    for (const auto& entry : entries) {
        panel.categories.push_back(entry.first);
        series.values.push_back(entry.second);
    }
    panel.series.push_back(std::move(series));
}

// Ranked entries, largest first, capped for readability.
std::vector<std::pair<std::string, double>> topEntries(
    std::vector<std::pair<std::string, double>> entries,
    std::size_t limit,
    bool ascending = false) {
    std::sort(entries.begin(), entries.end(), [ascending](const auto& a, const auto& b) {
        if (a.second != b.second) return ascending ? a.second < b.second : a.second > b.second;
        return a.first < b.first;
    });
    if (entries.size() > limit) entries.resize(limit);
    return entries;
}

// ---------------------------------------------------------------------------
// Structural views: schema, graph, er
// ---------------------------------------------------------------------------

// Ranks nodes by PageRank over the view's own edges, so "important" means
// "much depended upon" rather than "declared first". This is the non-linear
// part of the structural views: a node inherits importance from the
// importance of what points at it, not merely from how many edges it has.
void attachCentrality(GraphWriter& graph) {
    std::map<std::string, std::size_t> index;
    for (std::size_t i = 0; i < graph.nodes.size(); ++i) index[graph.nodes[i].id] = i;
    std::vector<std::pair<std::size_t, std::size_t>> edges;
    for (const auto& edge : graph.edges) {
        const auto from = index.find(edge.from);
        const auto to = index.find(edge.to);
        if (from == index.end() || to == index.end()) continue;
        edges.emplace_back(from->second, to->second);
    }
    if (graph.nodes.empty()) return;
    const std::vector<double> rank = pageRank(graph.nodes.size(), edges);
    for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
        // Scaled to a 0-100 reading so the number means something to a person
        // and does not shrink as the graph grows.
        graph.nodes[i].metrics["importance"] =
            std::round(rank[i] * static_cast<double>(graph.nodes.size()) * 1000.0) / 1000.0;
    }
}

// ---------------------------------------------------------------------------
// Timeline
// ---------------------------------------------------------------------------

// Fact records placed in order of a date-like field, with per-period volumes
// and a spike test over them.
void buildTimelineGraph(GraphWriter& graph,
                        const std::map<std::string, FactProfile>& facts,
                        const std::map<std::string, std::vector<FieldStats>>& stats) {
    graph.defaultRender = "panels";

    for (const auto& item : facts) {
        const auto found = stats.find(item.first);
        if (found == stats.end()) continue;

        // The ordering field is the one carrying an orderable literal in the
        // most records. Dates are preferred, but restricting to them made this
        // view work only on fact sets that happen to carry an ISO date - most
        // programs do not. Any numeric field (a version, a duration, a score, a
        // sequence number) orders records just as well.
        const FieldStats* order = nullptr;
        for (const auto& field : found->second) {
            if (field.type != FieldType::Date) continue;
            if (!order || field.present > order->present) order = &field;
        }
        if (!order) {
            for (const auto& field : found->second) {
                if (field.type != FieldType::Numeric) continue;
                if (!order || field.present > order->present) order = &field;
            }
        }
        if (!order || order->present == 0) continue;
        const bool byDate = order->type == FieldType::Date;

        RenderNode& factNode = graph.node(nodeId("fact", item.first), item.first, "fact");
        factNode.attributes["orderField"] = order->name;
        factNode.attributes["orderScale"] = byDate ? "date" : "numeric";
        factNode.metrics["records"] = static_cast<double>(item.second.records);
        factNode.metrics["ordered"] = static_cast<double>(order->present);
        if (item.second.records > item.second.samples.size()) {
            factNode.notes.push_back(
                "TRUNCATED: showing first " + std::to_string(item.second.samples.size()) +
                " of " + std::to_string(item.second.records) + " records");
        }
        noteTruncation(graph, item.first, item.second);

        // Sort records by the ordering value so the emitted order is the
        // timeline order; no renderer has to re-sort.
        std::vector<std::pair<double, const FactRecordValues*>> ordered;
        std::vector<std::string> orderText;
        for (const auto& record : item.second.samples) {
            const auto value = record.find(order->name);
            if (value == record.end()) continue;
            double numeric = 0;
            if (byDate) {
                if (!dateToDayNumber(value->second, numeric)) continue;
            } else {
                if (!looksNumeric(value->second)) continue;
                numeric = std::strtod(value->second.c_str(), nullptr);
            }
            ordered.emplace_back(numeric, &record);
            orderText.push_back(value->second);
        }
        std::vector<std::size_t> permutation(ordered.size());
        std::iota(permutation.begin(), permutation.end(), 0);
        std::sort(permutation.begin(), permutation.end(), [&](std::size_t a, std::size_t b) {
            if (ordered[a].first != ordered[b].first) return ordered[a].first < ordered[b].first;
            return orderText[a] < orderText[b];
        });

        std::string previousId;
        std::map<std::string, std::size_t> perPeriod;
        for (std::size_t rank = 0; rank < permutation.size(); ++rank) {
            const std::size_t source = permutation[rank];
            const FactRecordValues& record = *ordered[source].second;

            // A label the reader recognises: prefer a name-like field.
            std::string label = orderText[source];
            for (const char* candidate : {"name", "id", "title", "label"}) {
                const auto value = record.find(candidate);
                if (value != record.end()) {
                    label = value->second + " (" + orderText[source] + ")";
                    break;
                }
            }
            const std::string id = nodeId("event", item.first + "#" + std::to_string(rank));
            RenderNode& event = graph.node(id, label, "event");
            event.attributes[order->name] = orderText[source];
            event.attributes["factType"] = item.first;
            event.metrics["sequence"] = static_cast<double>(rank + 1);
            event.metrics["orderValue"] = ordered[source].first;
            for (const auto& value : record) {
                if (value.first == order->name) continue;
                event.attributes[value.first] = value.second;
            }
            graph.edge(nodeId("fact", item.first), id, "at");
            // Chain consecutive events so a network renderer without a time
            // axis still shows the sequence.
            if (!previousId.empty()) graph.edge(previousId, id, "then");
            previousId = id;

            // Period bucket: the calendar year for dates, and a rounded
            // magnitude for plain numbers, so both scales produce a readable
            // category axis instead of one bucket per distinct value.
            std::string period;
            if (byDate) {
                period = orderText[source].substr(0, 4);
            } else {
                period = displayNumber(std::floor(ordered[source].first));
            }
            ++perPeriod[period];
        }

        if (perPeriod.empty()) continue;
        std::vector<std::string> periods;
        std::vector<double> volumes;
        for (const auto& entry : perPeriod) {
            periods.push_back(entry.first);
            volumes.push_back(static_cast<double>(entry.second));
        }

        // A line through a single point draws no line; below two periods the
        // volume reads as a bar.
        RenderPanel& panel = graph.panel(
            periods.size() >= 2 ? "line" : "bar", item.first + " over time");
        panel.subtitle = std::string(byDate ? "by year of " : "bucketed by ") + order->name;
        panel.color = kindColor("event");
        panel.xLabel = order->name;
        panel.yLabel = "records";
        panel.categories = periods;
        panel.series.push_back(RenderPanel::Series{"records", volumes});

        // A three-point centred moving average: the trend line a reader needs
        // to tell a genuine rise from a single busy period.
        if (volumes.size() >= 3) {
            std::vector<double> smoothed(volumes.size());
            for (std::size_t i = 0; i < volumes.size(); ++i) {
                const std::size_t from = i == 0 ? 0 : i - 1;
                const std::size_t to = std::min(volumes.size() - 1, i + 1);
                double sum = 0;
                for (std::size_t j = from; j <= to; ++j) sum += volumes[j];
                smoothed[i] = sum / static_cast<double>(to - from + 1);
            }
            panel.series.push_back(RenderPanel::Series{"3-period average", smoothed});
        }

        // Spike test on the per-period volumes. Reported rather than drawn
        // because a spike is the point of a timeline, and a reader scanning a
        // line chart will miss a two-sigma jump that a sentence states.
        const double mean = std::accumulate(volumes.begin(), volumes.end(), 0.0) /
            static_cast<double>(volumes.size());
        double variance = 0;
        for (const double volume : volumes) variance += (volume - mean) * (volume - mean);
        const double stddev = volumes.size() > 1
            ? std::sqrt(variance / static_cast<double>(volumes.size() - 1))
            : 0.0;
        if (stddev > 0) {
            for (std::size_t i = 0; i < volumes.size(); ++i) {
                if (volumes[i] < mean + 2.0 * stddev) continue;
                std::ostringstream text;
                text << item.first << ": " << displayNumber(volumes[i]) << " records at "
                     << periods[i] << ", against an average of " << displayNumber(mean)
                     << " per period.";
                graph.insight("notice", text.str());
            }
        }
    }

    if (graph.panels.empty()) {
        graph.insight("info",
            "No fact type carries a date-like or numeric field, so there is nothing to "
            "place in order.");
    }
}

// ---------------------------------------------------------------------------
// Hierarchy
// ---------------------------------------------------------------------------

void buildHierarchyGraph(
    GraphWriter& graph,
    const std::map<std::string, FactProfile>& facts,
    const std::set<std::tuple<std::string, std::string, std::string>>& references) {
    // consume() stores a reference's source already qualified ("fact:Station")
    // while its target is a bare name ("Reading"). Both sides are reduced to
    // bare names here, so the maps below are keyed consistently and nodeId()
    // is applied exactly once.
    auto bareName = [](const std::string& value) {
        const std::size_t separator = value.find(':');
        return separator == std::string::npos ? value : value.substr(separator + 1);
    };

    std::map<std::string, std::vector<std::string>> childrenOf;
    std::map<std::string, std::string> parentOf;
    for (const auto& reference : references) {
        if (std::get<2>(reference) != "extends") continue;
        const std::string child = bareName(std::get<0>(reference));
        const std::string parent = std::get<1>(reference);
        childrenOf[parent].push_back(child);
        parentOf[child] = parent;
    }

    // Depth by walking to the root, guarded against a cyclic `extend` chain
    // (which a malformed program can declare and which would otherwise spin
    // here forever).
    auto depthOf = [&](const std::string& name) {
        int level = 0;
        std::string current = name;
        std::set<std::string> seen;
        while (seen.insert(current).second) {
            const auto parent = parentOf.find(current);
            if (parent == parentOf.end()) break;
            current = parent->second;
            ++level;
        }
        return level;
    };

    // Subtree record totals: what a treemap needs to size a branch by the data
    // underneath it rather than by the number of type names in it.
    std::function<std::size_t(const std::string&, std::set<std::string>&)> subtreeRecords =
        [&](const std::string& name, std::set<std::string>& seen) -> std::size_t {
            if (!seen.insert(name).second) return 0;
            std::size_t total = 0;
            const auto profile = facts.find(name);
            if (profile != facts.end()) total += profile->second.records;
            const auto children = childrenOf.find(name);
            if (children != childrenOf.end()) {
                for (const auto& child : children->second) total += subtreeRecords(child, seen);
            }
            return total;
        };

    std::vector<std::string> roots;
    for (const auto& item : facts) {
        const std::string id = nodeId("fact", item.first);
        RenderNode& node = graph.node(id, item.first, "fact");
        node.metrics["records"] = static_cast<double>(item.second.records);
        node.metrics["fields"] = static_cast<double>(item.second.fields.size());
        node.metrics["depth"] = depthOf(item.first);
        const auto children = childrenOf.find(item.first);
        node.metrics["directSubtypes"] =
            children == childrenOf.end() ? 0.0 : static_cast<double>(children->second.size());
        std::set<std::string> seen;
        node.metrics["subtreeRecords"] = static_cast<double>(subtreeRecords(item.first, seen));
        if (!parentOf.count(item.first)) {
            node.attributes["position"] = "root";
            roots.push_back(item.first);
        }
    }
    for (const auto& entry : childrenOf) {
        graph.node(nodeId("fact", entry.first), entry.first, "fact");
        for (const auto& child : entry.second) {
            graph.node(nodeId("fact", child), child, "fact");
            // Parent -> child, so tree layouts root at the base type.
            graph.edge(nodeId("fact", entry.first), nodeId("fact", child), "extends");
        }
    }

    // Treemap of the inheritance tree, weighted by records. This is the
    // business reading of a hierarchy: which branch of the taxonomy the data
    // actually lives in, not merely how the types nest.
    if (!roots.empty()) {
        std::function<std::string(const std::string&, std::set<std::string>&)> treeJson =
            [&](const std::string& name, std::set<std::string>& seen) -> std::string {
                std::ostringstream out;
                const auto profile = facts.find(name);
                const std::size_t own = profile == facts.end() ? 0 : profile->second.records;
                out << "{\"name\":" << quoted(name) << ",\"value\":" << (own == 0 ? 1 : own);
                const auto children = childrenOf.find(name);
                if (children != childrenOf.end() && seen.insert(name).second) {
                    out << ",\"children\":[";
                    bool first = true;
                    for (const auto& child : children->second) {
                        if (!first) out << ",";
                        first = false;
                        out << treeJson(child, seen);
                    }
                    out << "]";
                }
                out << "}";
                return out.str();
            };

        std::ostringstream tree;
        tree << "\"tree\":[";
        bool first = true;
        for (const auto& root : roots) {
            std::set<std::string> seen;
            if (!first) tree << ",";
            first = false;
            tree << treeJson(root, seen);
        }
        tree << "]";

        RenderPanel& panel = graph.panel("treemap", "Records by type hierarchy");
        panel.subtitle = "branch area is the record volume beneath it";
        panel.extraJson = tree.str();
    }

    std::vector<std::pair<std::string, double>> subtypes;
    for (const auto& entry : childrenOf) {
        subtypes.emplace_back(entry.first, static_cast<double>(entry.second.size()));
    }
    addBarPanel(graph, "hbar", "Direct subtypes", "how broadly each type is specialised",
                topEntries(subtypes, 12), kindColor("fact"));

    if (childrenOf.empty()) {
        graph.insight("info", "No fact uses `extend`, so there is no hierarchy to show.");
    }
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------

void buildStatsGraph(GraphWriter& graph,
                     const std::map<std::string, FactProfile>& facts,
                     const std::map<std::string, std::vector<FieldStats>>& stats) {
    graph.defaultRender = "panels";

    std::vector<std::pair<std::string, double>> recordCounts;
    std::vector<std::pair<std::string, double>> coverage;

    for (const auto& item : facts) {
        RenderNode& node = graph.node(nodeId("fact", item.first), item.first, "fact");
        node.metrics["records"] = static_cast<double>(item.second.records);
        node.metrics["fields"] = static_cast<double>(item.second.fields.size());
        node.metrics["sampled"] = static_cast<double>(item.second.samples.size());
        recordCounts.emplace_back(item.first, static_cast<double>(item.second.records));
        noteTruncation(graph, item.first, item.second);

        const auto found = stats.find(item.first);
        if (found == stats.end()) continue;

        double completeness = 0;
        for (const auto& field : found->second) {
            const double present = item.second.records == 0
                ? 0.0
                : static_cast<double>(item.second.fields.at(field.name)) * 100.0 /
                    static_cast<double>(item.second.records);
            completeness += present;

            const std::string qualified = item.first + "." + field.name;
            RenderNode& fieldNode =
                graph.node(nodeId("field", qualified), field.name, "field");
            fieldNode.attributes["factType"] = item.first;
            fieldNode.attributes["type"] = fieldTypeName(field.type);
            fieldNode.metrics["present"] = static_cast<double>(item.second.fields.at(field.name));
            fieldNode.metrics["of"] = static_cast<double>(item.second.records);
            fieldNode.metrics["coverage"] = std::round(present * 10.0) / 10.0;
            fieldNode.units["coverage"] = "%";
            fieldNode.metrics["distinct"] = static_cast<double>(field.distinct);
            if (field.type == FieldType::Numeric || field.type == FieldType::Date) {
                fieldNode.metrics["min"] = field.min;
                fieldNode.metrics["max"] = field.max;
                fieldNode.metrics["mean"] = field.mean;
                fieldNode.metrics["median"] = field.median;
                fieldNode.metrics["stddev"] = field.stddev;
                fieldNode.metrics["outliers"] = static_cast<double>(field.outliers.size());
            } else if (field.type == FieldType::Categorical) {
                fieldNode.metrics["spread"] = std::round(field.entropy * 100.0) / 100.0;
            }
            graph.edge(nodeId("fact", item.first), nodeId("field", qualified), "field");
            coverage.emplace_back(qualified, std::round(present * 10.0) / 10.0);

            // Data-quality findings, which are the reason to look at a
            // statistics view in the first place.
            if (present > 0 && present < 60.0) {
                std::ostringstream text;
                text << qualified << " is present in only " << displayNumber(present)
                     << "% of records - any chart grouped by it covers a minority of the data.";
                graph.insight("warn", text.str());
            }
        }
        if (!found->second.empty()) {
            node.metrics["completeness"] =
                std::round(completeness / static_cast<double>(found->second.size()) * 10.0) / 10.0;
            node.units["completeness"] = "%";
        }
    }

    addBarPanel(graph, "bar", "Records per fact type", "declared volume by type",
                topEntries(recordCounts, 14), kindColor("fact"));
    addBarPanel(graph, "hbar", "Least complete fields",
                "share of records that actually carry a value",
                topEntries(coverage, 12, /*ascending=*/true), kindColor("field"), "%");

    if (facts.empty()) {
        graph.insight("info", "No fact declarations were found in this program.");
    }
}

// ---------------------------------------------------------------------------
// Distribution
// ---------------------------------------------------------------------------

// Per-field value distributions. Histograms for measurable fields (binned by
// Freedman-Diaconis, so the bar width follows the spread of the data rather
// than a fixed count), category bars for groupable ones, and a box plot
// across all measurable fields so their scales can be compared at a glance.
void buildDistributionGraph(GraphWriter& graph,
                            const std::map<std::string, FactProfile>& facts,
                            const std::map<std::string, std::vector<FieldStats>>& stats,
                            const std::vector<std::string>& order) {
    graph.defaultRender = "panels";

    // Enough types to be representative without producing a page of charts
    // nobody scrolls through.
    constexpr std::size_t kMaxTypes = 4;
    std::size_t drawn = 0;

    for (const auto& name : order) {
        if (drawn >= kMaxTypes) break;
        const auto profile = facts.find(name);
        const auto found = stats.find(name);
        if (profile == facts.end() || found == stats.end()) continue;

        bool typeHadPanel = false;
        RenderNode& factNode = graph.node(nodeId("fact", name), name, "fact");
        factNode.metrics["records"] = static_cast<double>(profile->second.records);
        factNode.metrics["sampled"] = static_cast<double>(profile->second.samples.size());

        std::vector<std::string> boxFields;
        std::vector<std::vector<double>> boxes;

        for (const auto& field : found->second) {
            const std::string qualified = name + "." + field.name;
            RenderNode& fieldNode = graph.node(nodeId("field", qualified), field.name, "field");
            fieldNode.attributes["factType"] = name;
            fieldNode.attributes["type"] = fieldTypeName(field.type);
            fieldNode.metrics["present"] = static_cast<double>(field.present);
            fieldNode.metrics["distinct"] = static_cast<double>(field.distinct);
            graph.edge(nodeId("fact", name), nodeId("field", qualified), "field");

            if (field.type == FieldType::Numeric || field.type == FieldType::Date) {
                fieldNode.metrics["min"] = field.min;
                fieldNode.metrics["max"] = field.max;
                fieldNode.metrics["median"] = field.median;
                fieldNode.metrics["stddev"] = field.stddev;
                fieldNode.metrics["skew"] = std::round(field.skewness * 100.0) / 100.0;
                fieldNode.metrics["outliers"] = static_cast<double>(field.outliers.size());

                const Histogram histogram = buildHistogram(profile->second, field);
                if (histogram.counts.empty()) continue;
                RenderPanel& panel =
                    graph.panel("histogram", name + "." + field.name + " distribution");
                std::ostringstream subtitle;
                subtitle << histogram.counts.size() << " bins, median "
                         << displayNumber(field.median);
                if (field.type == FieldType::Date) subtitle << " (day number)";
                panel.subtitle = subtitle.str();
                panel.color = kindColor("field");
                panel.xLabel = field.name;
                panel.yLabel = "records";
                for (std::size_t i = 0; i < histogram.counts.size(); ++i) {
                    panel.categories.push_back(displayNumber(histogram.edges[i]));
                }
                RenderPanel::Series series;
                series.name = "records";
                for (const std::size_t count : histogram.counts) {
                    series.values.push_back(static_cast<double>(count));
                }
                panel.series.push_back(std::move(series));
                typeHadPanel = true;

                // Box plot input: the five-number summary on the same scale
                // the histogram used.
                boxFields.push_back(field.name);
                boxes.push_back({field.min,
                                 field.median - field.mad,
                                 field.median,
                                 field.median + field.mad,
                                 field.max});

                if (!field.outliers.empty()) {
                    std::ostringstream text;
                    text << qualified << ": " << field.outliers.size()
                         << " record(s) sit more than " << displayNumber(kOutlierZ)
                         << " robust deviations from the median";
                    // Name the records where it is short enough to be useful.
                    if (field.outliers.size() <= 5) {
                        text << " (";
                        for (std::size_t i = 0; i < field.outliers.size(); ++i) {
                            const FactRecordValues& record =
                                profile->second.samples[field.outliers[i]];
                            const auto value = record.find(field.name);
                            if (i) text << ", ";
                            text << (value == record.end() ? "?" : value->second);
                        }
                        text << ")";
                    }
                    text << ".";
                    graph.insight("warn", text.str());
                }
                if (std::fabs(field.skewness) > 1.5) {
                    std::ostringstream text;
                    text << qualified << " is strongly "
                         << (field.skewness > 0 ? "right" : "left")
                         << "-skewed, so its mean (" << displayNumber(field.mean)
                         << ") sits away from its median (" << displayNumber(field.median) << ").";
                    graph.insight("notice", text.str());
                }
            } else if (field.type == FieldType::Categorical && field.distinct >= 2) {
                // A single-category bar chart states that every record shares
                // one value, which the field node already says in one line.
                fieldNode.metrics["spread"] = std::round(field.entropy * 100.0) / 100.0;
                std::vector<std::pair<std::string, double>> entries;
                for (const auto& value : field.topValues) {
                    entries.emplace_back(value.first, static_cast<double>(value.second));
                }
                addBarPanel(graph, "hbar", name + "." + field.name + " by value",
                            std::to_string(field.distinct) + " distinct values",
                            entries, kindColor("segment"));
                typeHadPanel = true;

                if (field.distinct > 1 && field.entropy < 0.25) {
                    std::ostringstream text;
                    text << qualified << " is dominated by one value ("
                         << field.topValues.front().first << ", "
                         << field.topValues.front().second << " of " << field.present
                         << " records), so grouping by it separates almost nothing.";
                    graph.insight("notice", text.str());
                }
            } else if (field.type == FieldType::Identifier) {
                fieldNode.notes.push_back("near-unique: used as a key, not a category");
            }
        }

        if (!boxes.empty()) {
            std::ostringstream extra;
            extra << "\"boxes\":[";
            for (std::size_t i = 0; i < boxes.size(); ++i) {
                if (i) extra << ",";
                extra << jsonNumberArray(boxes[i]);
            }
            extra << "]";
            RenderPanel& panel = graph.panel("boxplot", name + ": spread of measurable fields");
            panel.subtitle = "median with robust deviation, min to max";
            panel.color = kindColor("measure");
            panel.categories = boxFields;
            panel.extraJson = extra.str();
        }

        if (typeHadPanel) {
            ++drawn;
            noteTruncation(graph, name, profile->second);
        }
    }

    if (graph.panels.empty()) {
        graph.insight("info",
            "No fact declares literal values, so there is no distribution to plot. "
            "Facts whose fields are variables rather than literals carry no data "
            "Celidae can measure without running the program.");
    }
}

// ---------------------------------------------------------------------------
// Comparison
// ---------------------------------------------------------------------------

// How fact types and fields stand against each other: volume, completeness,
// and - the non-linear part - which measurable fields move together, from a
// Pearson correlation over the encoded feature matrix.
void buildComparisonGraph(GraphWriter& graph,
                          const std::map<std::string, FactProfile>& facts,
                          const std::map<std::string, std::vector<FieldStats>>& stats,
                          const std::vector<std::string>& order) {
    graph.defaultRender = "panels";

    std::vector<std::string> types;
    std::vector<double> records;
    std::vector<double> fieldCounts;
    std::vector<double> completeness;
    for (const auto& item : facts) {
        RenderNode& node = graph.node(nodeId("fact", item.first), item.first, "fact");
        node.metrics["records"] = static_cast<double>(item.second.records);
        node.metrics["fields"] = static_cast<double>(item.second.fields.size());

        double filled = 0;
        for (const auto& field : item.second.fields) {
            filled += item.second.records == 0
                ? 0.0
                : static_cast<double>(field.second) * 100.0 /
                    static_cast<double>(item.second.records);
        }
        const double average = item.second.fields.empty()
            ? 0.0
            : filled / static_cast<double>(item.second.fields.size());
        node.metrics["completeness"] = std::round(average * 10.0) / 10.0;
        node.units["completeness"] = "%";

        types.push_back(item.first);
        records.push_back(static_cast<double>(item.second.records));
        fieldCounts.push_back(static_cast<double>(item.second.fields.size()));
        completeness.push_back(std::round(average * 10.0) / 10.0);
    }

    // One grouped chart rather than three separate ones: the comparison is
    // between the types, and separate charts make the reader do the join.
    if (!types.empty()) {
        RenderPanel& panel = graph.panel("bar", "Fact types compared");
        panel.subtitle = "volume, breadth and completeness side by side";
        panel.categories = types;
        panel.series.push_back(RenderPanel::Series{"records", records});
        panel.series.push_back(RenderPanel::Series{"fields", fieldCounts});
        panel.series.push_back(RenderPanel::Series{"completeness %", completeness});
    }

    for (const auto& name : order) {
        const auto profile = facts.find(name);
        const auto found = stats.find(name);
        if (profile == facts.end() || found == stats.end()) continue;

        const FeatureMatrix matrix = buildFeatureMatrix(profile->second, found->second);
        if (matrix.columnCount() < 2 || matrix.rowCount() < 3) continue;
        const CorrelationMatrix correlation = correlate(matrix);
        if (correlation.columns.size() < 2) continue;

        std::ostringstream extra;
        extra << "\"columns\":" << jsonStringArray(correlation.columns) << ",\"values\":[";
        for (std::size_t i = 0; i < correlation.values.size(); ++i) {
            if (i) extra << ",";
            extra << jsonNumberArray(correlation.values[i]);
        }
        extra << "]";
        RenderPanel& panel = graph.panel("heatmap", name + ": how fields move together");
        panel.subtitle = "Pearson correlation, -1 to +1";
        panel.extraJson = extra.str();

        // Parallel coordinates: the one chart that shows every record across
        // every measure at once, which is how a reader spots a record that is
        // ordinary on each field yet unusual in combination.
        if (matrix.rowCount() >= 4 && matrix.columnCount() <= 10) {
            std::ostringstream rows;
            rows << "\"dimensions\":" << jsonStringArray(matrix.columns) << ",\"rows\":[";
            const std::size_t limit = std::min<std::size_t>(matrix.rowCount(), 200);
            for (std::size_t i = 0; i < limit; ++i) {
                if (i) rows << ",";
                rows << jsonNumberArray(matrix.rows[i]);
            }
            rows << "],\"rowLabels\":[";
            for (std::size_t i = 0; i < limit; ++i) {
                if (i) rows << ",";
                rows << quoted(matrix.rowLabels[i]);
            }
            rows << "]";
            RenderPanel& parallel = graph.panel("parallel", name + ": records across all measures");
            parallel.subtitle = std::to_string(std::min<std::size_t>(matrix.rowCount(), 200)) +
                " records, " + std::to_string(matrix.columnCount()) + " measures";
            parallel.color = kindColor("record");
            parallel.extraJson = rows.str();
        }

        for (const auto& pair : correlation.notable) {
            std::ostringstream text;
            text << name << ": " << pair.a << " and " << pair.b << " move "
                 << (pair.r > 0 ? "together" : "opposite each other")
                 << " (r = " << displayNumber(pair.r) << ").";
            graph.insight("info", text.str());
            RenderNode& measure =
                graph.node(nodeId("measure", name + "." + pair.a), pair.a, "measure");
            measure.attributes["factType"] = name;
            RenderNode& other =
                graph.node(nodeId("measure", name + "." + pair.b), pair.b, "measure");
            other.attributes["factType"] = name;
            graph.edge(nodeId("measure", name + "." + pair.a),
                       nodeId("measure", name + "." + pair.b),
                       (pair.r > 0 ? "+" : "") + displayNumber(pair.r));
        }
        noteTruncation(graph, name, profile->second);
    }

    if (graph.panels.empty()) {
        graph.insight("info", "No fact declarations were found to compare.");
    }
}

// ---------------------------------------------------------------------------
// Cluster
// ---------------------------------------------------------------------------

// Records projected onto their two principal components and grouped by
// k-means, with k chosen by silhouette. This is the view that answers "are
// there natural segments in this data" - a question no structural diagram can
// even express.
void buildClusterGraph(GraphWriter& graph,
                       const std::map<std::string, FactProfile>& facts,
                       const std::map<std::string, std::vector<FieldStats>>& stats,
                       const std::vector<std::string>& order) {
    graph.defaultRender = "panels";
    constexpr std::size_t kMaxTypes = 3;
    std::size_t drawn = 0;

    for (const auto& name : order) {
        if (drawn >= kMaxTypes) break;
        const auto profile = facts.find(name);
        const auto found = stats.find(name);
        if (profile == facts.end() || found == stats.end()) continue;

        const FeatureMatrix matrix = buildFeatureMatrix(profile->second, found->second);
        const ClusterResult clusters = clusterRecords(matrix);
        if (!clusters.valid) {
            if (!clusters.reason.empty() && !matrix.columns.empty()) {
                graph.insight("info", name + ": " + clusters.reason + ".");
            }
            continue;
        }
        ++drawn;
        noteTruncation(graph, name, profile->second);

        RenderNode& factNode = graph.node(nodeId("fact", name), name, "fact");
        factNode.metrics["records"] = static_cast<double>(profile->second.records);
        factNode.metrics["segments"] = clusters.k;
        factNode.metrics["separation"] = std::round(clusters.silhouette * 100.0) / 100.0;

        RenderPanel& panel = graph.panel("scatter", name + ": record segments");
        std::ostringstream subtitle;
        subtitle << clusters.k << " segments over " << matrix.rowCount() << " records; the two axes "
                 << "capture " << displayNumber((clusters.explainedX + clusters.explainedY) * 100.0)
                 << "% of the variation";
        panel.subtitle = subtitle.str();
        panel.xLabel = "PC1 (" + displayNumber(clusters.explainedX * 100.0) + "%)";
        panel.yLabel = "PC2 (" + displayNumber(clusters.explainedY * 100.0) + "%)";

        std::vector<std::string> groupNames;
        for (int c = 0; c < clusters.k; ++c) groupNames.push_back("segment " + std::to_string(c + 1));
        std::ostringstream extra;
        extra << "\"groups\":" << jsonStringArray(groupNames);
        panel.extraJson = extra.str();

        for (std::size_t i = 0; i < clusters.assignment.size(); ++i) {
            const std::string group =
                groupNames[static_cast<std::size_t>(clusters.assignment[i])];
            panel.points.push_back(RenderPanel::Point{
                clusters.x[i], clusters.y[i], matrix.rowLabels[i], group});

            RenderNode& record = graph.node(
                nodeId("record", name + "#" + std::to_string(i)), matrix.rowLabels[i], "record");
            record.attributes["factType"] = name;
            record.attributes["segment"] = group;
            record.metrics["pc1"] = std::round(clusters.x[i] * 1000.0) / 1000.0;
            record.metrics["pc2"] = std::round(clusters.y[i] * 1000.0) / 1000.0;
            graph.edge(nodeId("segment", name + "/" + group),
                       nodeId("record", name + "#" + std::to_string(i)), "in");
        }

        std::vector<std::pair<std::string, double>> sizes;
        for (int c = 0; c < clusters.k; ++c) {
            const std::string group = groupNames[static_cast<std::size_t>(c)];
            RenderNode& segment =
                graph.node(nodeId("segment", name + "/" + group), group, "segment");
            segment.attributes["factType"] = name;
            segment.metrics["records"] =
                static_cast<double>(clusters.clusterSizes[static_cast<std::size_t>(c)]);
            sizes.emplace_back(group,
                static_cast<double>(clusters.clusterSizes[static_cast<std::size_t>(c)]));
            graph.edge(nodeId("fact", name), nodeId("segment", name + "/" + group), "segment");

            // What actually distinguishes a segment. Without this the view
            // says "there are three groups" and stops, which is not usable.
            const auto& drivers = clusters.clusterDrivers[static_cast<std::size_t>(c)];
            if (drivers.empty()) continue;
            std::ostringstream text;
            text << name << " " << group << " ("
                 << clusters.clusterSizes[static_cast<std::size_t>(c)] << " records): ";
            for (std::size_t i = 0; i < drivers.size(); ++i) {
                if (i) text << ", ";
                text << describeDriver(drivers[i].column, drivers[i].weight);
                segment.attributes[drivers[i].column] = drivers[i].weight > 0 ? "high" : "low";
            }
            text << ".";
            graph.insight("info", text.str());
        }
        addBarPanel(graph, "bar", name + ": segment sizes", "records per segment",
                    sizes, kindColor("segment"));

        // Silhouette is the honest caveat on the whole view: below about 0.25
        // the segments overlap enough that reading meaning into them would be
        // reading meaning into noise.
        if (clusters.silhouette < 0.25) {
            std::ostringstream text;
            text << name << ": the segments overlap heavily (separation "
                 << displayNumber(clusters.silhouette)
                 << " on a -1 to 1 scale), so treat them as a weak grouping rather than "
                    "distinct populations.";
            graph.insight("warn", text.str());
        }

        std::ostringstream axes;
        axes << name << ": the horizontal axis is driven by ";
        for (std::size_t i = 0; i < clusters.driversX.size() && i < 3; ++i) {
            if (i) axes << ", ";
            axes << clusters.driversX[i].column;
        }
        axes << "; the vertical by ";
        for (std::size_t i = 0; i < clusters.driversY.size() && i < 3; ++i) {
            if (i) axes << ", ";
            axes << clusters.driversY[i].column;
        }
        axes << ".";
        graph.insight("info", axes.str());
    }

    if (graph.panels.empty()) {
        graph.insight("info",
            "No fact type has enough varying literal fields to separate records into "
            "segments. Clustering needs at least " + std::to_string(kMinClusterRows) +
            " records and two fields that differ between them.");
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Diagram type metadata
// ---------------------------------------------------------------------------

const char* diagramTypeName(DiagramType type) {
    switch (type) {
        case DiagramType::Schema: return "schema";
        case DiagramType::Graph: return "graph";
        case DiagramType::Er: return "er";
        case DiagramType::Hierarchy: return "hierarchy";
        case DiagramType::Timeline: return "timeline";
        case DiagramType::Stats: return "stats";
        case DiagramType::Distribution: return "distribution";
        case DiagramType::Comparison: return "comparison";
        case DiagramType::Cluster: return "cluster";
    }
    return "schema";
}

const char* diagramTypeSummary(DiagramType type) {
    switch (type) {
        case DiagramType::Schema: return "fact types and the fields they declare";
        case DiagramType::Graph: return "methods, globals, imports and the calls between them";
        case DiagramType::Er: return "entities, their fields and inheritance";
        case DiagramType::Hierarchy: return "inheritance as a tree, sized by record volume";
        case DiagramType::Timeline: return "records in date order, with per-period volume and trend";
        case DiagramType::Stats: return "record counts, field coverage and data-quality findings";
        case DiagramType::Distribution: return "value distributions per field, with outliers";
        case DiagramType::Comparison: return "fact types side by side, and which fields move together";
        case DiagramType::Cluster: return "records grouped into segments by principal components";
    }
    return "";
}

bool parseDiagramType(const std::string& name, DiagramType& out) {
    for (DiagramType candidate : kAllDiagramTypes) {
        if (name == diagramTypeName(candidate)) {
            out = candidate;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// buildGraph
// ---------------------------------------------------------------------------

RenderedGraph SchemaGraphAccumulator::buildGraph(
    DiagramType type,
    const std::vector<std::string>& unresolvedImports) const {
    GraphWriter graph;
    const auto& stats = impl_->stats();
    const std::vector<std::string> byVolume = impl_->factsByVolume();

    switch (type) {
        case DiagramType::Timeline:
            buildTimelineGraph(graph, impl_->facts, stats);
            break;
        case DiagramType::Hierarchy:
            buildHierarchyGraph(graph, impl_->facts, impl_->references);
            break;
        case DiagramType::Stats:
            buildStatsGraph(graph, impl_->facts, stats);
            break;
        case DiagramType::Distribution:
            buildDistributionGraph(graph, impl_->facts, stats, byVolume);
            break;
        case DiagramType::Comparison:
            buildComparisonGraph(graph, impl_->facts, stats, byVolume);
            break;
        case DiagramType::Cluster:
            buildClusterGraph(graph, impl_->facts, stats, byVolume);
            break;
        case DiagramType::Schema:
        case DiagramType::Graph:
        case DiagramType::Er: {
            const bool includeFields = type != DiagramType::Graph;
            const bool includeExecution = type == DiagramType::Graph;
            const bool includeImports = type != DiagramType::Er;
            auto classify = [&](const std::string& name) {
                if (impl_->methods.count(name)) return std::string("method");
                if (impl_->facts.count(name)) return std::string("fact");
                if (isBuiltinFunctionName(name)) return std::string("library");
                if (name == "Fact:depends" || name == "Fact:relate") return std::string("library");
                const auto separator = name.find(':');
                if (separator != std::string::npos &&
                    impl_->imports.count(name.substr(0, separator))) {
                    return std::string("library");
                }
                return std::string("external");
            };

            std::vector<std::pair<std::string, double>> coverage;
            std::vector<std::pair<std::string, double>> fieldsPerEntity;
            for (const auto& item : impl_->facts) {
                RenderNode& node = graph.node(nodeId("fact", item.first), item.first, "fact");
                node.metrics["records"] = static_cast<double>(item.second.records);
                node.metrics["fields"] = static_cast<double>(item.second.fields.size());
                fieldsPerEntity.emplace_back(
                    item.first, static_cast<double>(item.second.fields.size()));
                if (!includeFields) continue;

                const auto found = stats.find(item.first);
                for (const auto& field : item.second.fields) {
                    const std::string qualified = item.first + "." + field.first;
                    const std::size_t missing = item.second.records - field.second;
                    const double percentage = item.second.records == 0
                        ? 0.0
                        : static_cast<double>(field.second) * 100.0 /
                            static_cast<double>(item.second.records);
                    RenderNode& fieldNode =
                        graph.node(nodeId("field", qualified), field.first, "field");
                    fieldNode.attributes["factType"] = item.first;
                    fieldNode.metrics["present"] = static_cast<double>(field.second);
                    fieldNode.metrics["missing"] = static_cast<double>(missing);
                    fieldNode.metrics["coverage"] = std::round(percentage * 10.0) / 10.0;
                    fieldNode.units["coverage"] = "%";
                    if (found != stats.end()) {
                        for (const auto& detected : found->second) {
                            if (detected.name != field.first) continue;
                            fieldNode.attributes["type"] = fieldTypeName(detected.type);
                            fieldNode.metrics["distinct"] = static_cast<double>(detected.distinct);
                            break;
                        }
                    }
                    graph.edge(nodeId("fact", item.first), nodeId("field", qualified), "field");
                    coverage.emplace_back(qualified, std::round(percentage * 10.0) / 10.0);
                }
            }
            if (includeExecution) {
                for (const auto& method : impl_->methods) {
                    graph.node(nodeId("method", method), method, "method");
                }
                for (const auto& global : impl_->globals) {
                    graph.node(nodeId("global", global), global, "global");
                }
            }
            if (includeImports) {
                for (const auto& import : impl_->imports) {
                    graph.node(nodeId("library", import), import, "library");
                }
                for (const auto& unresolved : unresolvedImports) {
                    graph.node(nodeId("library", unresolved), unresolved, "library")
                        .notes.push_back("source not resolved; may be native");
                }
            }

            std::map<std::string, double> relationshipCounts;
            for (const auto& reference : impl_->references) {
                const std::string& from = std::get<0>(reference);
                const std::string& targetName = std::get<1>(reference);
                const std::string& label = std::get<2>(reference);
                if (!includeExecution && label != "extends") continue;
                const std::string kind = label == "extends" ? "fact" : classify(targetName);
                graph.node(nodeId(kind, targetName), targetName, kind);
                graph.edge(from, nodeId(kind, targetName), label);
                relationshipCounts[label] += 1.0;
            }

            attachCentrality(graph);

            if (type == DiagramType::Schema) {
                addBarPanel(graph, "hbar", "Least complete fields",
                            "share of records that carry a value",
                            topEntries(coverage, 12, /*ascending=*/true),
                            kindColor("field"), "%");
            } else if (type == DiagramType::Graph) {
                std::vector<std::pair<std::string, double>> relationships(
                    relationshipCounts.begin(), relationshipCounts.end());
                addBarPanel(graph, "bar", "Relationships by type",
                            "how this program is wired together",
                            topEntries(relationships, 12), kindColor("method"));
                std::vector<std::pair<std::string, double>> central;
                for (const auto& node : graph.nodes) {
                    const auto importance = node.metrics.find("importance");
                    if (importance == node.metrics.end()) continue;
                    central.emplace_back(node.label, importance->second);
                }
                addBarPanel(graph, "hbar", "Most depended upon",
                            "PageRank over the call graph, not raw edge count",
                            topEntries(central, 12), kindColor("global"));
            } else {
                addBarPanel(graph, "hbar", "Fields per entity", "entity shape complexity",
                            topEntries(fieldsPerEntity, 12), kindColor("fact"));
            }
            break;
        }
    }

    RenderedGraph result;
    result.nodes.assign(graph.nodes.begin(), graph.nodes.end());
    result.edges = std::move(graph.edges);
    result.panels = std::move(graph.panels);
    result.insights = std::move(graph.insights);
    result.defaultRender = graph.defaultRender;

    std::ostringstream out;
    out << "{\"schemaVersion\":3,\"mode\":" << quoted(diagramTypeName(type))
        << ",\"title\":" << quoted(diagramTypeSummary(type))
        << ",\"render\":" << quoted(result.defaultRender)
        << ",\"palette\":" << paletteJson()
        << ",\"summary\":{\"factTypes\":" << impl_->facts.size()
        << ",\"methods\":" << impl_->methods.size()
        << ",\"globals\":" << impl_->globals.size() << "},"
        << "\"truncation\":{\"truncated\":false,\"omittedNodes\":0,\"omittedEdges\":0},"
        << "\"nodes\":[";
    for (std::size_t i = 0; i < result.nodes.size(); ++i) {
        const RenderNode& node = result.nodes[i];
        if (i) out << ",";
        out << "{\"id\":" << quoted(node.id)
            << ",\"label\":" << quoted(node.label)
            << ",\"kind\":" << quoted(node.kind);
        const std::string detail = node.detail();
        if (!detail.empty()) out << ",\"detail\":" << quoted(detail);
        if (!node.metrics.empty()) {
            out << ",\"metrics\":{";
            bool first = true;
            for (const auto& metric : node.metrics) {
                if (!first) out << ",";
                first = false;
                out << quoted(metric.first) << ":" << jsonNumber(metric.second);
            }
            out << "}";
        }
        if (!node.attributes.empty()) {
            out << ",\"attributes\":{";
            bool first = true;
            for (const auto& attribute : node.attributes) {
                if (!first) out << ",";
                first = false;
                out << quoted(attribute.first) << ":" << quoted(attribute.second);
            }
            out << "}";
        }
        out << "}";
    }
    out << "],\"edges\":[";
    for (std::size_t i = 0; i < result.edges.size(); ++i) {
        if (i) out << ",";
        const std::string identity =
            result.edges[i].from + "\n" + result.edges[i].to + "\n" + result.edges[i].label;
        out << "{\"id\":\"" << stableEdgeId(identity)
            << "\",\"from\":" << quoted(result.edges[i].from)
            << ",\"to\":" << quoted(result.edges[i].to)
            << ",\"label\":" << quoted(result.edges[i].label) << "}";
    }
    out << "],\"panels\":[";
    for (std::size_t i = 0; i < result.panels.size(); ++i) {
        if (i) out << ",";
        out << panelJson(result.panels[i]);
    }
    out << "],\"insights\":[";
    for (std::size_t i = 0; i < result.insights.size(); ++i) {
        if (i) out << ",";
        out << "{\"level\":" << quoted(result.insights[i].level)
            << ",\"text\":" << quoted(result.insights[i].text) << "}";
    }
    out << "],\"recommendations\":[";
    const std::vector<Recommendation> recommendations = recommendViews(shape());
    for (std::size_t i = 0; i < recommendations.size(); ++i) {
        if (i) out << ",";
        out << "{\"view\":" << quoted(diagramTypeName(recommendations[i].view))
            << ",\"score\":" << jsonNumber(recommendations[i].score)
            << ",\"rationale\":" << quoted(recommendations[i].rationale) << "}";
    }
    out << "]}";
    result.json = out.str();
    return result;
}

std::string SchemaGraphAccumulator::json(
    DiagramType type,
    const std::vector<std::string>& unresolvedImports) const {
    return buildGraph(type, unresolvedImports).json;
}

std::string graphJson(const Program& program,
                      const std::vector<std::string>& unresolvedImports) {
    SchemaGraphAccumulator accumulator;
    for (const auto& import : program.imports) accumulator.consume(import);
    for (const auto& global : program.globals) accumulator.consume(global);
    for (const auto& clause : program.clauses) accumulator.consume(clause);
    return accumulator.json(DiagramType::Schema, unresolvedImports);
}

std::string graphJsonEnvelope(const std::string& json) {
    return "FELIDAE_GRAPH_BEGIN\n" + json + "\nFELIDAE_GRAPH_END\n";
}

// ---------------------------------------------------------------------------
// SVG export
// ---------------------------------------------------------------------------

namespace {

// Server-computed layout shared by standaloneSvg. The interactive HTML runs
// equivalent algorithms client-side (so a user can re-layout/drag live); this
// C++ copy is what makes a static SVG possible without a JS runtime.
struct Point { double x = 0, y = 0; };

// Timeline layout: one row per fact type, its events running left to right in
// the order buildTimelineGraph emitted them (already sorted).
//
// The lane layout below cannot express this - it groups by node *kind*, so
// every event in the program lands in a single undifferentiated column and the
// exported SVG shows none of the ordering the view exists to convey.
std::map<std::string, Point> computeTimelineLayout(const RenderedGraph& graph) {
    std::map<std::string, std::string> ownerOfEvent;
    for (const auto& edge : graph.edges) {
        if (edge.label == "at") ownerOfEvent[edge.to] = edge.from;
    }

    std::vector<std::string> factOrder;
    std::map<std::string, std::vector<std::string>> eventsByFact;
    for (const auto& node : graph.nodes) {
        if (node.kind == "fact") {
            if (!eventsByFact.count(node.id)) factOrder.push_back(node.id);
            eventsByFact.try_emplace(node.id);
        }
    }
    for (const auto& node : graph.nodes) {
        if (node.kind != "event") continue;
        const auto owner = ownerOfEvent.find(node.id);
        const std::string key =
            owner == ownerOfEvent.end() ? std::string("(ungrouped)") : owner->second;
        if (!eventsByFact.count(key)) factOrder.push_back(key);
        eventsByFact[key].push_back(node.id);
    }

    std::map<std::string, Point> positions;
    double y = 60;
    for (const auto& fact : factOrder) {
        positions[fact] = Point{70.0, y};
        const auto& events = eventsByFact[fact];
        for (std::size_t i = 0; i < events.size(); ++i) {
            positions[events[i]] = Point{300.0 + static_cast<double>(i) * 210.0, y};
        }
        y += 120;
    }
    return positions;
}

// Hierarchy layout: parents above their children, so inheritance depth reads
// top to bottom instead of every fact stacking into one column.
std::map<std::string, Point> computeHierarchyLayout(const RenderedGraph& graph) {
    std::map<std::string, int> depth;
    std::map<std::string, std::string> parentOf;
    for (const auto& edge : graph.edges) {
        if (edge.label == "extends") parentOf[edge.to] = edge.from;
    }
    for (const auto& node : graph.nodes) {
        int level = 0;
        std::string current = node.id;
        std::set<std::string> seen;
        // seen guards against a cyclic `extend` chain, which would otherwise
        // spin here forever.
        while (seen.insert(current).second) {
            const auto parent = parentOf.find(current);
            if (parent == parentOf.end()) break;
            current = parent->second;
            ++level;
        }
        depth[node.id] = level;
    }

    std::map<int, int> countAtDepth;
    std::map<std::string, Point> positions;
    for (const auto& node : graph.nodes) {
        const int level = depth[node.id];
        const int column = countAtDepth[level]++;
        positions[node.id] = Point{70.0 + static_cast<double>(column) * 240.0,
                                   55.0 + static_cast<double>(level) * 110.0};
    }
    return positions;
}

// One column per node kind. The kind order used to be a fixed list, so any
// kind introduced later - `record`, `segment`, `measure` - fell into a single
// shared trailing column and overlapped. The order now comes from the palette,
// which is the same list the colours come from.
std::map<std::string, Point> computeLaneLayout(const RenderedGraph& graph) {
    std::map<std::string, int> columnByKind;
    for (std::size_t i = 0; i < allNodeKinds().size(); ++i) {
        columnByKind[allNodeKinds()[i]] = static_cast<int>(i);
    }
    std::map<std::string, std::vector<const RenderNode*>> lanes;
    for (const auto& node : graph.nodes) lanes[node.kind].push_back(&node);

    // A kind the palette does not know still gets its own column rather than
    // sharing one with every other unknown kind.
    int nextColumn = static_cast<int>(allNodeKinds().size());
    for (const auto& lane : lanes) {
        if (!columnByKind.count(lane.first)) columnByKind[lane.first] = nextColumn++;
    }

    std::map<std::string, Point> positions;
    for (const auto& lane : lanes) {
        const int column = columnByKind[lane.first];
        for (std::size_t row = 0; row < lane.second.size(); ++row) {
            positions[lane.second[row]->id] =
                Point{70.0 + static_cast<double>(column) * 230.0,
                      55.0 + static_cast<double>(row) * 76.0};
        }
    }
    return positions;
}

// jsonEscape() is for embedding text inside a JSON string literal; it
// deliberately leaves '<'/'>'/'&' untouched, which are exactly the
// characters that corrupt or break out of SVG/XML text content (a fact
// label containing "&" would otherwise produce invalid, unparsable XML).
std::string xmlEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += ch; break;
        }
    }
    return out;
}

std::string truncateLabel(const std::string& label, std::size_t limit) {
    if (label.size() <= limit) return label;
    return label.substr(0, limit - 1) + "\xE2\x80\xA6";
}

const char* diagramTitle(DiagramType type) {
    switch (type) {
        case DiagramType::Schema: return "Celidae Fact Schema";
        case DiagramType::Graph: return "Celidae Dependency Graph";
        case DiagramType::Er: return "Celidae ER Diagram";
        case DiagramType::Hierarchy: return "Celidae Fact Hierarchy";
        case DiagramType::Timeline: return "Celidae Fact Timeline";
        case DiagramType::Stats: return "Celidae Fact Statistics";
        case DiagramType::Distribution: return "Celidae Value Distributions";
        case DiagramType::Comparison: return "Celidae Fact Comparison";
        case DiagramType::Cluster: return "Celidae Record Segments";
    }
    return "Celidae Fact Schema";
}

// A panel drawn directly into the SVG. Without this the data views would
// export a node-link picture of a chart-shaped answer - technically an image,
// but not the one the view exists to produce.
struct SvgChart {
    std::string body;
    double height = 0;
};

SvgChart renderPanelSvg(const RenderPanel& panel, double width, double top) {
    SvgChart chart;
    std::ostringstream out;
    const double left = 70;
    const double plotWidth = width - left - 60;
    const double titleHeight = 34;

    auto header = [&](double blockHeight) {
        out << "<text x=\"" << left << "\" y=\"" << (top + 18)
            << "\" fill=\"#e6f7f4\" font-size=\"13\" font-weight=\"600\">"
            << xmlEscape(panel.title) << "</text>\n";
        if (!panel.subtitle.empty()) {
            out << "<text x=\"" << left << "\" y=\"" << (top + 31)
                << "\" fill=\"#7fa39e\" font-size=\"10\">" << xmlEscape(panel.subtitle)
                << "</text>\n";
        }
        chart.height = blockHeight;
    };

    const std::string color = panel.color.empty() ? std::string("#18f0d7") : panel.color;

    if ((panel.type == "bar" || panel.type == "hbar" || panel.type == "histogram") &&
        !panel.series.empty() && !panel.categories.empty()) {
        const std::size_t rows = panel.categories.size();
        const double rowHeight = 22;
        header(titleHeight + static_cast<double>(rows) * rowHeight + 18);

        double maximum = 0;
        for (const auto& series : panel.series) {
            for (const double value : series.values) maximum = std::max(maximum, std::fabs(value));
        }
        if (!(maximum > 0)) maximum = 1;

        const double labelWidth = 150;
        for (std::size_t i = 0; i < rows; ++i) {
            const double y = top + titleHeight + static_cast<double>(i) * rowHeight;
            out << "<text x=\"" << left << "\" y=\"" << (y + 13)
                << "\" fill=\"#8aa8a3\" font-size=\"10\">"
                << xmlEscape(truncateLabel(panel.categories[i], 22)) << "</text>\n";
            // Grouped series stack as thin sub-bars inside the row, so a
            // multi-measure comparison stays one readable block per category.
            const double barHeight = std::max(3.0, 14.0 / static_cast<double>(panel.series.size()));
            for (std::size_t s = 0; s < panel.series.size(); ++s) {
                if (i >= panel.series[s].values.size()) continue;
                const double value = panel.series[s].values[i];
                const double barWidth =
                    std::max(0.0, std::fabs(value) / maximum * (plotWidth - labelWidth));
                out << "<rect x=\"" << (left + labelWidth) << "\" y=\""
                    << (y + 2 + static_cast<double>(s) * barHeight) << "\" width=\"" << barWidth
                    << "\" height=\"" << (barHeight - 1) << "\" rx=\"2\" fill=\""
                    << (panel.series.size() > 1 ? seriesColor(s) : color) << "\"/>\n";
            }
            const double first = panel.series.front().values.size() > i
                ? panel.series.front().values[i] : 0.0;
            out << "<text x=\"" << (left + labelWidth + plotWidth - labelWidth + 6) << "\" y=\""
                << (y + 13) << "\" fill=\"#7fa39e\" font-size=\"9\">"
                << xmlEscape(displayNumber(first) + panel.valueSuffix) << "</text>\n";
        }
        chart.body = out.str();
        return chart;
    }

    if (panel.type == "line" && !panel.series.empty() && !panel.categories.empty()) {
        const double plotHeight = 150;
        header(titleHeight + plotHeight + 26);
        double maximum = 0;
        for (const auto& series : panel.series) {
            for (const double value : series.values) maximum = std::max(maximum, value);
        }
        if (!(maximum > 0)) maximum = 1;
        const double baseline = top + titleHeight + plotHeight;
        out << "<line x1=\"" << left << "\" y1=\"" << baseline << "\" x2=\"" << (left + plotWidth)
            << "\" y2=\"" << baseline << "\" stroke=\"#1f3d38\" stroke-width=\"1\"/>\n";
        for (std::size_t s = 0; s < panel.series.size(); ++s) {
            const auto& values = panel.series[s].values;
            if (values.empty()) continue;
            out << "<polyline fill=\"none\" stroke=\"" << seriesColor(s)
                << "\" stroke-width=\"1.8\" points=\"";
            for (std::size_t i = 0; i < values.size(); ++i) {
                const double x = left + (values.size() == 1
                    ? plotWidth / 2
                    : static_cast<double>(i) / static_cast<double>(values.size() - 1) * plotWidth);
                const double y = baseline - values[i] / maximum * plotHeight;
                if (i) out << " ";
                out << x << "," << y;
            }
            out << "\"/>\n";
        }
        // Only the ends and middle get a tick label; a dense axis would
        // overlap itself at any realistic period count.
        for (const std::size_t index : {std::size_t(0), panel.categories.size() / 2,
                                        panel.categories.size() - 1}) {
            if (index >= panel.categories.size()) continue;
            const double x = left + (panel.categories.size() == 1
                ? plotWidth / 2
                : static_cast<double>(index) /
                    static_cast<double>(panel.categories.size() - 1) * plotWidth);
            out << "<text x=\"" << x << "\" y=\"" << (baseline + 14)
                << "\" fill=\"#7fa39e\" font-size=\"9\" text-anchor=\"middle\">"
                << xmlEscape(panel.categories[index]) << "</text>\n";
        }
        chart.body = out.str();
        return chart;
    }

    if (panel.type == "scatter" && !panel.points.empty()) {
        const double plotHeight = 240;
        header(titleHeight + plotHeight + 26);
        double minX = panel.points.front().x, maxX = minX;
        double minY = panel.points.front().y, maxY = minY;
        for (const auto& point : panel.points) {
            minX = std::min(minX, point.x); maxX = std::max(maxX, point.x);
            minY = std::min(minY, point.y); maxY = std::max(maxY, point.y);
        }
        const double spanX = maxX - minX > 0 ? maxX - minX : 1;
        const double spanY = maxY - minY > 0 ? maxY - minY : 1;

        std::map<std::string, std::size_t> groupIndex;
        for (const auto& point : panel.points) {
            groupIndex.emplace(point.group, groupIndex.size());
        }
        for (const auto& point : panel.points) {
            const double x = left + (point.x - minX) / spanX * plotWidth;
            const double y = top + titleHeight + plotHeight -
                (point.y - minY) / spanY * plotHeight;
            out << "<circle cx=\"" << x << "\" cy=\"" << y << "\" r=\"4\" fill=\""
                << seriesColor(groupIndex[point.group]) << "\" fill-opacity=\"0.75\">"
                << "<title>" << xmlEscape(point.label + " - " + point.group) << "</title>"
                << "</circle>\n";
        }
        double legendX = left;
        for (const auto& group : groupIndex) {
            out << "<rect x=\"" << legendX << "\" y=\"" << (top + titleHeight + plotHeight + 8)
                << "\" width=\"9\" height=\"9\" rx=\"2\" fill=\"" << seriesColor(group.second)
                << "\"/>\n"
                << "<text x=\"" << (legendX + 13) << "\" y=\""
                << (top + titleHeight + plotHeight + 16)
                << "\" fill=\"#7fa39e\" font-size=\"9\">" << xmlEscape(group.first) << "</text>\n";
            legendX += 110;
        }
        chart.body = out.str();
        return chart;
    }

    if (panel.type == "boxplot" && !panel.categories.empty()) {
        // The five-number summaries live in extraJson, which the SVG path
        // does not parse. Naming the fields it covers is still more useful
        // than silently dropping the panel.
        header(titleHeight + 20);
        out << "<text x=\"" << left << "\" y=\"" << (top + titleHeight + 10)
            << "\" fill=\"#7fa39e\" font-size=\"10\">"
            << xmlEscape("fields: " + [&] {
                   std::string names;
                   for (const auto& category : panel.categories) {
                       if (!names.empty()) names += ", ";
                       names += category;
                   }
                   return names;
               }())
            << " (open the --html export for the plotted version)</text>\n";
        chart.body = out.str();
        return chart;
    }

    return chart;  // treemap/heatmap/parallel: HTML-only, height stays 0
}

} // namespace

std::string standaloneSvg(const RenderedGraph& graph, DiagramType type) {
    const double width = 1180;
    std::ostringstream charts;
    double cursor = 70;

    // Panel-first views export their charts, then the node graph beneath, so
    // an --svg of a data view is the chart a reader expected rather than a
    // node-link rendering of one.
    if (graph.defaultRender == "panels") {
        for (const auto& panel : graph.panels) {
            const SvgChart rendered = renderPanelSvg(panel, width, cursor);
            if (rendered.height <= 0) continue;
            charts << rendered.body;
            cursor += rendered.height + 18;
        }
    }
    const double graphTop = cursor > 70 ? cursor + 10 : 0;

    // Each view gets the layout that actually carries its meaning; the lane
    // layout only suits the kind-partitioned views.
    auto positions =
        type == DiagramType::Timeline ? computeTimelineLayout(graph)
        : type == DiagramType::Hierarchy ? computeHierarchyLayout(graph)
        : computeLaneLayout(graph);
    for (auto& position : positions) position.second.y += graphTop;

    double maxX = width, maxY = cursor + 60;
    for (const auto& entry : positions) {
        maxX = std::max(maxX, entry.second.x + 220);
        maxY = std::max(maxY, entry.second.y + 60);
    }

    std::ostringstream out;
    const char* title = diagramTitle(type);
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " << maxX << " " << maxY
        << "\" width=\"" << maxX << "\" height=\"" << maxY
        << "\" font-family=\"Segoe UI, Inter, sans-serif\">\n"
        << "<title>" << xmlEscape(title) << "</title>\n"
        << "<defs><marker id=\"arrow\" viewBox=\"0 0 10 10\" refX=\"9\" refY=\"5\" "
           "markerWidth=\"6\" markerHeight=\"6\" orient=\"auto-start-reverse\">"
        << "<path d=\"M 0 0 L 10 5 L 0 10 z\" fill=\"#376760\"/></marker></defs>\n"
        << "<rect x=\"0\" y=\"0\" width=\"" << maxX << "\" height=\"" << maxY
        << "\" fill=\"#0b1210\"/>\n"
        << "<text x=\"70\" y=\"38\" fill=\"#2dd9c0\" font-size=\"16\" font-weight=\"600\">"
        << xmlEscape(title) << "</text>\n"
        << "<text x=\"70\" y=\"54\" fill=\"#7fa39e\" font-size=\"11\">"
        << xmlEscape(diagramTypeSummary(type)) << "</text>\n";

    out << charts.str();

    for (const auto& edge : graph.edges) {
        const auto fromIt = positions.find(edge.from);
        const auto toIt = positions.find(edge.to);
        if (fromIt == positions.end() || toIt == positions.end()) continue;
        const double x1 = fromIt->second.x + 150, y1 = fromIt->second.y + 20;
        const double x2 = toIt->second.x, y2 = toIt->second.y + 20;
        out << "<line x1=\"" << x1 << "\" y1=\"" << y1 << "\" x2=\"" << x2 << "\" y2=\"" << y2
            << "\" stroke=\"#376760\" stroke-width=\"1.4\" marker-end=\"url(#arrow)\"/>\n";
        if (!edge.label.empty()) {
            out << "<text x=\"" << (x1 + x2) / 2 << "\" y=\"" << (y1 + y2) / 2 - 4
                << "\" fill=\"#7fa39e\" font-size=\"10\">" << xmlEscape(edge.label) << "</text>\n";
        }
    }

    for (const auto& node : graph.nodes) {
        const auto it = positions.find(node.id);
        if (it == positions.end()) continue;
        const double x = it->second.x, y = it->second.y;
        const std::string color = kindColor(node.kind);
        const std::string detail = node.detail();
        out << "<g>\n"
            << "  <rect x=\"" << x << "\" y=\"" << y << "\" width=\"150\" height=\"40\" rx=\"7\""
            << " fill=\"" << color << "\" fill-opacity=\"0.22\" stroke=\"" << color
            << "\" stroke-width=\"1.4\"/>\n"
            << "  <text x=\"" << (x + 10) << "\" y=\"" << (y + 24)
            << "\" fill=\"#e6f7f4\" font-size=\"12\">" << xmlEscape(truncateLabel(node.label, 19))
            << "</text>\n"
            << "  <title>"
            << xmlEscape(node.kind + ": " + node.label +
                         (detail.empty() ? "" : " \xE2\x80\x94 " + detail))
            << "</title>\n"
            << "</g>\n";
    }

    out << "</svg>\n";
    return out.str();
}

// ---------------------------------------------------------------------------
// HTML export
// ---------------------------------------------------------------------------

std::string standaloneHtml(const std::map<DiagramType, std::string>& payloads) {
    // kVisualizerTemplate is generated from src/celidae/webui/template.html
    // (cytoscape + ECharts + heroicons, all npm-installed there - see
    // GeneratedVisualizerAssets.h). This function only substitutes the
    // DiagramType payloads into the pre-built, self-contained page.
    //
    // Every type gets a token so the template's shape is fixed; a type the
    // caller did not supply is substituted with `null`, and the page renders
    // only the views whose payload is non-null. That is what lets
    // `--template=<name>` emit a focused single-view file from the same
    // template as the all-views default.
    std::string html = kVisualizerTemplate;
    for (DiagramType type : kAllDiagramTypes) {
        const std::string token =
            std::string("__DATA_") + toUpperAscii(diagramTypeName(type)) + "__";
        const auto found = payloads.find(type);
        replaceToken(
            html, token,
            found == payloads.end() ? "null" : scriptSafeJson(found->second));
    }
    return html;
}

} // namespace Felidae::Celidae
