#include "celidae/Visualization.h"

#include "BuiltinRegistry.h"
#include "celidae/GeneratedVisualizerAssets.h"
#include "celidae/Reasoning.h"
#include <inja/inja.hpp>

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

std::string joinWith(const std::vector<std::string>& parts, const char* separator) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) out += separator;
        out += parts[i];
    }
    return out;
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
    std::vector<ReasoningStep> reasoning;
    std::vector<AlgorithmBundle> bundles;
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

    // Records the analysis chain for one fact type, prefixing each step with
    // the type it belongs to so a multi-type program's trace stays readable.
    void trace(const std::string& factType, const std::vector<ReasoningStep>& steps) {
        for (const auto& step : steps) {
            reasoning.push_back(
                ReasoningStep{step.stage, factType + ": " + step.finding, step.decision});
        }
    }

    // What each bundle of algorithms concluded for one fact type. Carried
    // beside the step trace rather than folded into it, because the two
    // answer different questions: the trace is "what happened, in order", the
    // summary is "what was tried, and did it find anything".
    void summarise(const std::string& factType, const std::vector<AlgorithmBundle>& found) {
        for (const auto& entry : found) {
            AlgorithmBundle copy = entry;
            copy.finding = factType + ": " + copy.finding;
            bundles.push_back(std::move(copy));
        }
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

} // namespace

std::vector<ChartOption> chartOptionsFor(const RenderPanel& panel) {
    std::vector<ChartOption> options;
    auto offer = [&](const char* type, bool available, const std::string& reason = "") {
        options.push_back(ChartOption{type, available, reason});
    };

    // Charts built from a category axis and one or more numeric series. These
    // are the ones with genuine alternatives, because the data underneath
    // them is the same shape however it is drawn.
    const bool categorical = panel.type == "bar" || panel.type == "hbar" ||
        panel.type == "histogram" || panel.type == "line" || panel.type == "area";
    if (!categorical) {
        // A heatmap, treemap, scatter, box plot, parallel-coordinate or tree
        // panel carries a shape no bar chart can hold. Offering a swap would
        // mean silently dropping a dimension, so the panel reports its single
        // option and says why.
        offer(panel.type.c_str(), true);
        return options;
    }

    offer("bar", true);
    // Horizontal bars are the same chart with the axes swapped, and are
    // strictly easier to read once the labels are long or numerous - except
    // where the axis is ordered, since turning time on its side reads as a
    // ranking rather than a sequence.
    if (panel.orderedCategories) {
        offer("hbar", false,
              "the category axis is ordered, and rotating it would read as a ranking "
              "rather than a sequence");
    } else {
        offer("hbar", true);
    }

    // The distinction that matters. A line asserts the categories can be
    // traversed; where they are labels, that assertion is false and the chart
    // is persuasive anyway.
    if (panel.orderedCategories) {
        offer("line", true);
        offer("area", true);
    } else {
        const std::string reason =
            "these categories are labels with no order, so joining them would draw a "
            "trend that does not exist";
        offer("line", false, reason);
        offer("area", false, reason);
    }

    // A histogram's bars are contiguous bins, so it is a distinct rendering
    // rather than a styling of "bar" - offered only where the panel already
    // is one, since arbitrary categories have no bin width to be contiguous
    // about.
    if (panel.type == "histogram") offer("histogram", true);

    return options;
}

namespace {

std::string chartOptionsJson(const RenderPanel& panel) {
    const std::vector<ChartOption> options = chartOptionsFor(panel);
    nlohmann::json out = nlohmann::json::array();
    for (const auto& option : options) {
        nlohmann::json entry;
        entry["type"] = option.type;
        entry["available"] = option.available;
        if (!option.reason.empty()) entry["reason"] = option.reason;
        out.push_back(std::move(entry));
    }
    return out.dump();
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
    if (panel.orderedCategories) out << ",\"orderedCategories\":true";
    // The renderings this panel's data supports, and the reason for each it
    // does not. The page builds its per-chart selector from this rather than
    // deciding for itself, so the rule about lines through unordered
    // categories lives in one place.
    out << ",\"alternatives\":" << chartOptionsJson(panel);
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
    out << "}";
    std::string serialised = out.str();
    if (panel.extra.is_object() && !panel.extra.empty()) {
        // Parsing back is deliberate: it proves what was assembled above is
        // well-formed before it reaches the page, so a malformed panel fails
        // here rather than as a blank browser tab.
        nlohmann::json merged = nlohmann::json::parse(serialised);
        for (auto entry = panel.extra.begin(); entry != panel.extra.end(); ++entry) {
            merged[entry.key()] = entry.value();
        }
        serialised = merged.dump();
    }
    return serialised;
}

std::string scriptSafeJson(std::string json) {
    for (size_t pos = json.find("</"); pos != std::string::npos; pos = json.find("</", pos + 3)) {
        json.replace(pos, 2, "<\\/");
    }
    return json;
}

// toUpperAscii() and replaceToken() lived here to build "__DATA_<TYPE>__"
// tokens and substitute them by scanning the page. inja renders the template
// from a data object now, so both are gone rather than left behind: a
// find-and-replace helper sitting unused next to a templating engine is an
// invitation to reintroduce the bug it caused, which was replacing the first
// textual match anywhere in a megabyte of inlined third-party JavaScript.

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
            // Only genuine anomalies count towards the recommendation's
            // rationale. A field holding two populations contributes a large
            // "far from the median" count that would otherwise be advertised
            // as "26 outlying records" when nothing is actually anomalous.
            if (!field.secondPopulation) shape.outlierCount += field.outliers.size();
        }
        const auto profile = impl_->facts.find(entry.first);
        if (profile == impl_->facts.end()) continue;
        for (const auto& field : entry.second) {
            if (field.type != FieldType::Categorical && field.type != FieldType::Identifier) {
                continue;
            }
            if (field.distinct < kMinSemanticValues) continue;
            if (!textVocabulary(profile->second, field, 1).empty()) ++shape.textCorpusFields;
        }
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
        recommendations.push_back(Recommendation{view, score, rationale, true});
    };
    // Why a view is *not* offered. Stated in the same terms as the positive
    // case - a measurement of this program's facts - so the two halves cannot
    // drift into disagreeing about when a view applies.
    auto decline = [&](DiagramType view, const std::string& reason) {
        recommendations.push_back(Recommendation{view, 0.0, reason, false});
    };

    // Each score is a statement about the data, not a preference: a view only
    // scores when the program contains the thing that view is built to show.
    const std::size_t measurable = shape.numericFields + shape.dateFields;

    if (shape.clusterable) {
        add(DiagramType::Cluster,
            0.85 + std::min(0.1, static_cast<double>(shape.notableCorrelations) * 0.02),
            std::to_string(shape.sampledRecords) + " records carry enough varying fields to "
            "separate into segments");
    } else {
        decline(DiagramType::Cluster,
                "no fact type has enough records with enough varying fields to separate into "
                "segments; PCA and k-means need at least " +
                std::to_string(kMinClusterRows) + " records across two varying fields");
    }

    if (measurable >= 2) {
        add(DiagramType::Comparison, 0.7,
            std::to_string(measurable) + " measurable fields can be compared against each other");
    } else if (shape.factTypes >= 2) {
        add(DiagramType::Comparison, 0.3,
            std::to_string(shape.factTypes) +
            " fact types can be compared by volume and completeness, though there are too few "
            "measurable fields to correlate");
    } else {
        decline(DiagramType::Comparison,
                "one fact type and " + std::to_string(measurable) +
                " measurable fields: there is nothing to compare against anything else");
    }

    if (measurable >= 1 && shape.sampledRecords >= 8) {
        add(DiagramType::Distribution, 0.75,
            "value distributions are available for " + std::to_string(measurable) +
            " measurable fields" +
            (shape.outlierCount > 0
                ? ", including " + std::to_string(shape.outlierCount) + " outlying records"
                : ""));
    } else if (shape.textCorpusFields >= 1 && shape.sampledRecords >= 8) {
        // Text-only data still has a distribution - of the vocabulary its
        // values are built from, and of how those values group in the latent
        // space that vocabulary spans. Declining here left a 249-record list
        // of country names with no data view at all, on the grounds that
        // nothing in it was a number.
        add(DiagramType::Distribution, 0.55,
            std::to_string(shape.textCorpusFields) +
            " text fields have values sharing enough vocabulary to group, though no numeric "
            "field to plot");
    } else if (shape.categoricalFields >= 1 && shape.sampledRecords >= 8) {
        add(DiagramType::Distribution, 0.45,
            std::to_string(shape.categoricalFields) +
            " label fields have value frequencies to show, though no numeric field to plot");
    } else {
        decline(DiagramType::Distribution,
                shape.sampledRecords < 8
                    ? "only " + std::to_string(shape.sampledRecords) +
                          " records: too few for a distribution to have a shape"
                    : "no numeric, date or label field carries values to distribute");
    }

    // The gate the user's question is about. A timeline is exactly as useful
    // as the temporal information in the facts, and when there is none the
    // honest answer is that this view does not apply - not a substitute axis.
    if (shape.dateFields >= 1) {
        add(DiagramType::Timeline, 0.8,
            std::to_string(shape.dateFields) + " date field(s) place records in time");
    } else {
        decline(DiagramType::Timeline,
                "no fact field carries a date, so there is no time axis to place records on");
    }

    if (shape.inheritanceEdges >= 2) {
        add(DiagramType::Hierarchy, 0.55,
            std::to_string(shape.inheritanceEdges) + " inheritance relationships form a hierarchy");
    } else {
        decline(DiagramType::Hierarchy,
                shape.inheritanceEdges == 0
                    ? "no fact type uses `extend`, so there is no hierarchy"
                    : "a single `extend` relationship is a pair, not a hierarchy");
    }

    if (shape.records > 0) {
        add(DiagramType::Stats, 0.5,
            std::to_string(shape.records) + " records across " +
            std::to_string(shape.factTypes) + " fact types");
    } else {
        decline(DiagramType::Stats, "no fact records were declared, so there is nothing to count");
    }

    if (shape.factTypes > 0) {
        add(DiagramType::Schema, 0.35, "fact types and their fields are declared");
    } else {
        decline(DiagramType::Schema, "this program declares no fact types");
    }

    // The ER view needs relationships, and relationships need at least two
    // entities. One fact type has nothing to relate to, and offering the view
    // anyway is what made it a duplicate of the schema view.
    if (shape.factTypes >= 2) {
        add(DiagramType::Er, shape.inheritanceEdges > 0 ? 0.45 : 0.25,
            std::to_string(shape.factTypes) +
            " fact types may be joined by inheritance or by shared key values");
    } else {
        decline(DiagramType::Er,
                "a single fact type has nothing to be related to; the schema view describes it");
    }
    // The dependency graph is the one view about code rather than about facts,
    // and it is scored last on purpose. Facts are what Celidae exists to
    // visualise; an IDE already draws call graphs, and it draws them with a
    // real symbol index rather than from a parse. This view stays because it
    // is occasionally the right answer - a program that declares almost no
    // facts and is mostly methods has nothing else to show - but it should
    // never outrank a view of the data.
    if (shape.callEdges >= 3) {
        const double score = shape.records > 0 ? 0.15 : 0.4;
        add(DiagramType::Graph, score,
            shape.records > 0
                ? std::to_string(shape.callEdges) +
                      " call relationships between methods (code structure, not fact data)"
                : std::to_string(shape.callEdges) +
                      " call relationships, and no fact data to show instead");
    } else {
        decline(DiagramType::Graph,
                "this program has " + std::to_string(shape.callEdges) +
                " call relationships between methods, which is not a graph worth drawing");
    }

    std::sort(recommendations.begin(), recommendations.end(), [](const auto& a, const auto& b) {
        // Applicable views first, then by strength. An inapplicable view is
        // never merely a weak recommendation - it is not on offer.
        if (a.applicable != b.applicable) return a.applicable;
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

// Laplacian eigenmaps over the edge list, attached to every node so the page
// can offer a layout that reflects connectivity.
//
// This used to serve the static SVG export only. With SVG gone it moves to
// where it is more useful anyway: cytoscape's built-in layouts are a force
// simulation (which settles somewhere different on every run, so a node the
// reader located once has moved by the time they look back), a directed tree,
// and a circle. None of them places nodes by how the graph is actually
// connected, and none of them is reproducible. This one is both.
void attachSpectralPositions(GraphWriter& graph) {
    if (graph.nodes.size() < 4 || graph.edges.size() < 3) return;
    std::map<std::string, std::size_t> index;
    for (std::size_t i = 0; i < graph.nodes.size(); ++i) index[graph.nodes[i].id] = i;
    std::vector<std::pair<std::size_t, std::size_t>> edges;
    for (const auto& edge : graph.edges) {
        const auto from = index.find(edge.from);
        const auto to = index.find(edge.to);
        if (from == index.end() || to == index.end()) continue;
        if (from->second == to->second) continue;
        edges.emplace_back(from->second, to->second);
    }
    if (edges.size() < 3) return;

    const std::vector<SpectralPoint> points = spectralLayout(graph.nodes.size(), edges);
    if (points.size() != graph.nodes.size()) return;

    // A degenerate solve collapses every node onto one point. Reporting that
    // as a layout would hand the reader a single dot; leaving the nodes
    // unpositioned lets the page fall back to a force layout instead.
    double minX = points[0].x, maxX = points[0].x;
    double minY = points[0].y, maxY = points[0].y;
    for (const auto& point : points) {
        minX = std::min(minX, point.x);
        maxX = std::max(maxX, point.x);
        minY = std::min(minY, point.y);
        maxY = std::max(maxY, point.y);
    }
    if (maxX - minX < 1e-6 && maxY - minY < 1e-6) return;

    for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
        graph.nodes[i].layoutX = points[i].x;
        graph.nodes[i].layoutY = points[i].y;
        graph.nodes[i].positioned = true;
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

        // The ordering field must genuinely carry time. An earlier version
        // fell back to "any numeric field", on the reasoning that a version or
        // a sequence number orders records just as well - which is true, and
        // beside the point. This view is titled "records in date order", it
        // buckets by calendar year, it reports spikes "per period" and it
        // draws a moving-average trend; all four of those statements are
        // false about an axis that is not time.
        //
        // The concrete failure: converted_csv_country.fx has no date at all,
        // so the fallback picked `country_code` and produced a timeline of 249
        // countries ordered by their ISO number, with "Afghanistan (004)"
        // first. Nothing about that is informative, and a reader has no way to
        // tell it apart from a real one.
        //
        // A view that cannot answer its own question should say so. The
        // recommender declines the timeline for such a program (see
        // recommendViews), and this loop skips the fact type, so the two agree.
        const FieldStats* order = nullptr;
        for (const auto& field : found->second) {
            if (field.type != FieldType::Date) continue;
            if (!order || field.present > order->present) order = &field;
        }
        // A trend needs a sequence. One dated record produced a bar chart
        // titled "Audit over time" containing a single bar - a chart that
        // says nothing while looking exactly like one that does.
        if (!order || order->present < kMinTimelineRecords) continue;

        RenderNode& factNode = graph.node(nodeId("fact", item.first), item.first, "fact");
        factNode.attributes["orderField"] = order->name;
        factNode.attributes["orderScale"] = "date";
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
            if (!dateToDayNumber(value->second, numeric)) continue;
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

            // Bucket by calendar year, so a fact set spanning years produces
            // a readable category axis instead of one bucket per distinct day.
            ++perPeriod[orderText[source].substr(0, 4)];
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
        panel.orderedCategories = true;  // calendar periods
        panel.subtitle = "by year of " + order->name;
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
            "No fact field carries a date, so there is no time axis to place records on. "
            "This view does not apply to this program; ordering records by some other "
            "number would look like a timeline without being one.");
    }
}

// ---------------------------------------------------------------------------
// Entity relationships
// ---------------------------------------------------------------------------

// The ER view used to share a code path with the schema view, differing only
// in whether imports were drawn. Both emitted one node per fact type fanned
// out into one node per field, so on any program without `extend` they were
// the same picture twice - the schema view's picture, relabelled "entities,
// their fields and inheritance" and containing no relationships at all.
//
// They answer different questions and should look different. The schema view
// answers "what is declared": every field, its coverage, its type. This view
// answers "how do the entities connect": one box per entity, its keys named
// on the box, and an edge for every join - whether declared with `extend` or
// inferred from the values (see inferRelationships).
void buildErGraph(GraphWriter& graph,
                  const std::map<std::string, FactProfile>& facts,
                  const std::map<std::string, std::vector<FieldStats>>& stats,
                  const std::set<std::tuple<std::string, std::string, std::string>>& references) {
    if (facts.empty()) {
        graph.insight("info", "This program declares no fact types, so there are no entities.");
        return;
    }

    for (const auto& item : facts) {
        RenderNode& node = graph.node(nodeId("fact", item.first), item.first, "fact");
        node.metrics["records"] = static_cast<double>(item.second.records);
        node.metrics["fields"] = static_cast<double>(item.second.fields.size());

        // Naming the keys and measures on the entity box is what lets this
        // view drop the per-field nodes without losing information a reader
        // needs: the fields that matter for joining are the keys, and the
        // fields that matter for analysis are the measures.
        const auto found = stats.find(item.first);
        if (found == stats.end()) continue;
        std::vector<std::string> keys;
        std::vector<std::string> measures;
        std::vector<std::string> dates;
        std::vector<std::string> labels;
        for (const auto& field : found->second) {
            switch (field.type) {
                case FieldType::Identifier: keys.push_back(field.name); break;
                case FieldType::Numeric: measures.push_back(field.name); break;
                // Listed apart from the measures. A date is orderable and a
                // quantity is orderable, but only one of them can be summed,
                // and a reader deciding which view to open needs to know
                // whether this entity is placed in time at all.
                case FieldType::Date: dates.push_back(field.name); break;
                case FieldType::Categorical: labels.push_back(field.name); break;
                case FieldType::Empty: break;
            }
        }
        if (!keys.empty()) node.attributes["keys"] = joinWith(keys, ", ");
        if (!measures.empty()) node.attributes["measures"] = joinWith(measures, ", ");
        if (!dates.empty()) node.attributes["dates"] = joinWith(dates, ", ");
        if (!labels.empty()) node.attributes["labels"] = joinWith(labels, ", ");
        node.metrics["keyFields"] = static_cast<double>(keys.size());
    }

    // Declared inheritance first: `extend` is a stated relationship and
    // outranks anything inferred from values.
    std::size_t declared = 0;
    for (const auto& reference : references) {
        if (std::get<2>(reference) != "extends") continue;
        const std::string& target = std::get<1>(reference);
        graph.node(nodeId("fact", target), target, "fact");
        graph.edge(std::get<0>(reference), nodeId("fact", target), "extends");
        ++declared;
    }

    const std::vector<InferredRelationship> joins = inferRelationships(facts, stats);
    std::vector<std::pair<std::string, double>> strengths;
    for (const auto& join : joins) {
        std::ostringstream label;
        label << join.fromField << " -> " << join.toField << " (" << join.cardinality << ")";
        graph.edge(nodeId("fact", join.fromType), nodeId("fact", join.toType), label.str());
        strengths.emplace_back(join.fromType + "." + join.fromField + " -> " + join.toType,
                               std::round(join.containment * 1000.0) / 10.0);

        // A join that does not fully resolve is a data-quality finding, and
        // it is the kind a reader cannot get any other way: nothing in the
        // program declares the reference, so nothing else can notice it is
        // broken.
        if (join.orphans > 0) {
            std::ostringstream text;
            text << join.fromType << "." << join.fromField << " looks like a reference to "
                 << join.toType << "." << join.toField << ", but " << join.orphans
                 << (join.orphans == 1 ? " value has" : " values have")
                 << " no match on the other side.";
            graph.insight("warning", text.str());
        }
    }

    if (!strengths.empty()) {
        addBarPanel(graph, "hbar", "Inferred joins by referential integrity",
                    "share of referencing values found on the target side",
                    topEntries(strengths, 12), kindColor("fact"), "%");
    }

    if (declared == 0 && joins.empty()) {
        std::ostringstream text;
        text << "No relationships found: no fact type uses `extend`, and no field's values "
             << "are contained in another type's key. ";
        text << (facts.size() == 1
                     ? "With a single fact type there is nothing to relate - the schema view "
                       "describes this program's shape."
                     : "These fact types appear to be independent tables.");
        graph.insight("info", text.str());
    } else if (joins.empty()) {
        graph.insight("info",
                      "Relationships shown are declared with `extend`; no additional joins "
                      "were inferred from field values.");
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
        std::function<nlohmann::json(const std::string&, std::set<std::string>&)> treeJson =
            [&](const std::string& name, std::set<std::string>& seen) -> nlohmann::json {
                nlohmann::json node;
                const auto profile = facts.find(name);
                const std::size_t own = profile == facts.end() ? 0 : profile->second.records;
                node["name"] = name;
                // A zero-record parent still needs a positive area, or the
                // treemap drops the entire branch beneath it.
                node["value"] = own == 0 ? 1 : own;
                const auto children = childrenOf.find(name);
                if (children != childrenOf.end() && seen.insert(name).second) {
                    nlohmann::json list = nlohmann::json::array();
                    for (const auto& child : children->second) list.push_back(treeJson(child, seen));
                    node["children"] = std::move(list);
                }
                return node;
            };

        nlohmann::json tree = nlohmann::json::array();
        for (const auto& root : roots) {
            std::set<std::string> seen;
            tree.push_back(treeJson(root, seen));
        }

        RenderPanel& panel = graph.panel("treemap", "Records by type hierarchy");
        panel.subtitle = "branch area is the record volume beneath it";
        panel.extra["tree"] = std::move(tree);
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

// Defined below, next to the cluster view; both views want it.
void emitOutlierAndDriverFindings(GraphWriter& graph,
                                  const std::string& name,
                                  const AnalysisPipeline& pipeline);

// Numeric values of one field, paired with the sample index they came from.
std::vector<std::pair<std::size_t, double>> measureValues(const FactProfile& profile,
                                                          const std::string& field,
                                                          FieldType type) {
    std::vector<std::pair<std::size_t, double>> values;
    for (std::size_t i = 0; i < profile.samples.size(); ++i) {
        const auto found = profile.samples[i].find(field);
        if (found == profile.samples[i].end()) continue;
        double parsed = 0;
        if (type == FieldType::Date) {
            if (!dateToDayNumber(found->second, parsed)) continue;
        } else {
            if (!looksNumeric(found->second)) continue;
            parsed = std::strtod(found->second.c_str(), nullptr);
        }
        values.emplace_back(i, parsed);
    }
    return values;
}

const FieldStats* findField(const std::vector<FieldStats>& fields, const std::string& name) {
    for (const auto& field : fields) {
        if (field.name == name) return &field;
    }
    return nullptr;
}

// Turns one proposal into a panel carrying real data.
//
// This is the half of the dynamic pipeline that draws; proposeCharts() is the
// half that decides. Nothing here is specific to any subject matter - the
// arguments are "a measure", "a label", "a date field", and which fields play
// those parts was measured, not assumed. The same code produces a
// delivery-time histogram for one program and a species cross-tab for another.
bool renderProposal(GraphWriter& graph,
                    const std::string& factName,
                    const FactProfile& profile,
                    const std::vector<FieldStats>& fields,
                    const FeatureMatrix& matrix,
                    const ChartProposal& proposal) {
    const std::string prefix = factName + ": ";

    if (proposal.chart == "histogram" && proposal.fields.size() == 1) {
        const FieldStats* field = findField(fields, proposal.fields[0]);
        if (!field) return false;
        const Histogram histogram = buildHistogram(profile, *field);
        if (histogram.counts.empty()) return false;
        RenderPanel& panel = graph.panel("histogram", prefix + proposal.title);
        panel.subtitle = proposal.rationale;
        panel.color = kindColor("field");
        panel.xLabel = field->name;
        panel.yLabel = "records";
        // Bins run along a numeric scale, so a line through them is a
        // frequency polygon rather than an invented trend.
        panel.orderedCategories = true;
        RenderPanel::Series series;
        series.name = "records";
        for (std::size_t i = 0; i < histogram.counts.size(); ++i) {
            panel.categories.push_back(displayNumber(histogram.edges[i]));
            series.values.push_back(static_cast<double>(histogram.counts[i]));
        }
        panel.series.push_back(std::move(series));
        return true;
    }

    if (proposal.chart == "hbar" && proposal.fields.size() == 1) {
        const FieldStats* field = findField(fields, proposal.fields[0]);
        if (!field || field->topValues.empty()) return false;
        std::vector<std::pair<std::string, double>> entries;
        for (const auto& value : field->topValues) {
            entries.emplace_back(value.first, static_cast<double>(value.second));
        }
        addBarPanel(graph, "hbar", prefix + proposal.title, proposal.rationale,
                    entries, kindColor("segment"));
        return true;
    }

    if (proposal.chart == "scatter" && proposal.fields.size() == 2) {
        const FieldStats* x = findField(fields, proposal.fields[0]);
        const FieldStats* y = findField(fields, proposal.fields[1]);
        if (!x || !y) return false;
        const auto xs = measureValues(profile, x->name, x->type);
        const auto ys = measureValues(profile, y->name, y->type);
        std::map<std::size_t, double> yById;
        for (const auto& value : ys) yById[value.first] = value.second;
        RenderPanel& panel = graph.panel("scatter", prefix + proposal.title);
        panel.subtitle = proposal.rationale;
        panel.xLabel = x->name;
        panel.yLabel = y->name;
        for (const auto& value : xs) {
            const auto paired = yById.find(value.first);
            if (paired == yById.end()) continue;
            std::string label = "record " + std::to_string(value.first + 1);
            for (const char* candidate : {"name", "id", "title", "label"}) {
                const auto found = profile.samples[value.first].find(candidate);
                if (found != profile.samples[value.first].end()) { label = found->second; break; }
            }
            panel.points.push_back(
                RenderPanel::Point{value.second, paired->second, label, y->name});
        }
        if (panel.points.size() < 3) { graph.panels.pop_back(); return false; }
        panel.extra["groups"] = nlohmann::json::array({y->name});
        return true;
    }

    if (proposal.chart == "boxplot" && proposal.fields.size() == 2) {
        const FieldStats* measure = findField(fields, proposal.fields[0]);
        const FieldStats* label = findField(fields, proposal.fields[1]);
        if (!measure || !label) return false;
        // Five-number summary per level of the label, which is what makes the
        // separation the proposal measured actually visible.
        std::map<std::string, std::vector<double>> byLevel;
        for (std::size_t i = 0; i < profile.samples.size(); ++i) {
            const auto group = profile.samples[i].find(label->name);
            const auto value = profile.samples[i].find(measure->name);
            if (group == profile.samples[i].end() || value == profile.samples[i].end()) continue;
            double parsed = 0;
            if (measure->type == FieldType::Date) {
                if (!dateToDayNumber(value->second, parsed)) continue;
            } else {
                if (!looksNumeric(value->second)) continue;
                parsed = std::strtod(value->second.c_str(), nullptr);
            }
            byLevel[group->second].push_back(parsed);
        }
        std::vector<std::string> levels;
        nlohmann::json boxes = nlohmann::json::array();
        for (auto& level : byLevel) {
            if (level.second.size() < 2) continue;
            std::sort(level.second.begin(), level.second.end());
            const auto at = [&](double fraction) {
                const double position = fraction * static_cast<double>(level.second.size() - 1);
                const std::size_t low = static_cast<std::size_t>(std::floor(position));
                const std::size_t high = static_cast<std::size_t>(std::ceil(position));
                const double weight = position - static_cast<double>(low);
                return level.second[low] * (1.0 - weight) + level.second[high] * weight;
            };
            boxes.push_back(nlohmann::json::array({level.second.front(), at(0.25), at(0.5),
                                                   at(0.75), level.second.back()}));
            levels.push_back(level.first);
        }
        if (levels.size() < 2) return false;
        RenderPanel& panel = graph.panel("boxplot", prefix + proposal.title);
        panel.subtitle = proposal.rationale;
        panel.color = kindColor("measure");
        panel.categories = levels;
        panel.extra["boxes"] = std::move(boxes);
        return true;
    }

    if (proposal.chart == "heatmap" && proposal.fields.size() == 2) {
        // Contingency table between two labels.
        const std::string& a = proposal.fields[0];
        const std::string& b = proposal.fields[1];

        // Above a certain size, a correspondence map replaces the grid rather
        // than joining it.
        //
        // A contingency heatmap is the right chart for a small table: with
        // three regions against two tiers, six cells are read at a glance and
        // a map of five points would be an affectation. It stops working as
        // the table grows - eight products against six warehouses is 48 cells
        // and a reader has to scan rows and columns to find the pairing that
        // matters, which is precisely the work correspondence analysis does
        // for them by putting both fields in one space.
        //
        // Emitting both would be two pictures of one table competing for the
        // same conclusion, which is the overlap this view is meant to avoid.
        const FieldStats* rowField = findField(fields, a);
        const FieldStats* columnField = findField(fields, b);
        if (rowField && columnField &&
            rowField->distinct * columnField->distinct >= kMinCellsForCorrespondence) {
            const CorrespondenceMap map = correspondenceMap(profile, *rowField, *columnField);
            if (map.valid) {
                RenderPanel& panel =
                    graph.panel("scatter", prefix + a + " and " + b + ": which values go together");
                std::ostringstream subtitle;
                subtitle << "correspondence analysis; points close together co-occur more than "
                         << "chance would give. Cramer's V = "
                         << displayNumber(std::round(map.association * 100.0) / 100.0)
                         << ", axes carry "
                         << displayNumber(std::round(map.explained * 1000.0) / 10.0) << "%";
                panel.subtitle = subtitle.str();
                panel.xLabel = "dimension 1";
                panel.yLabel = "dimension 2";
                for (const auto& point : map.points) {
                    RenderPanel::Point rendered;
                    rendered.x = point.x;
                    rendered.y = point.y;
                    rendered.label = point.value;
                    rendered.group = point.isRow ? a : b;
                    panel.points.push_back(std::move(rendered));
                }
                panel.extra["groups"] = nlohmann::json::array({a, b});
                return true;
            }
        }
        std::map<std::string, std::map<std::string, std::size_t>> table;
        std::set<std::string> rows, columns;
        for (const auto& record : profile.samples) {
            const auto left = record.find(a);
            const auto right = record.find(b);
            if (left == record.end() || right == record.end()) continue;
            ++table[left->second][right->second];
            rows.insert(left->second);
            columns.insert(right->second);
        }
        if (rows.size() < 2 || columns.size() < 2) return false;
        const std::vector<std::string> rowNames(rows.begin(), rows.end());
        const std::vector<std::string> columnNames(columns.begin(), columns.end());
        nlohmann::json values = nlohmann::json::array();
        for (std::size_t r = 0; r < rowNames.size(); ++r) {
            nlohmann::json line = nlohmann::json::array();
            for (const auto& column : columnNames) {
                const auto row = table.find(rowNames[r]);
                double count = 0;
                if (row != table.end()) {
                    const auto cell = row->second.find(column);
                    if (cell != row->second.end()) count = static_cast<double>(cell->second);
                }
                line.push_back(count);
            }
            values.push_back(std::move(line));
        }
        RenderPanel& panel = graph.panel("heatmap", prefix + proposal.title);
        panel.subtitle = proposal.rationale;
        panel.extra["columns"] = columnNames;
        panel.extra["rowLabels"] = rowNames;
        panel.extra["values"] = std::move(values);
        // Counts, not correlations: the page picks a sequential colour scale
        // rather than one diverging around a midpoint that would mean nothing.
        panel.extra["scale"] = "count";
        return true;
    }

    if (proposal.chart == "heatmap" && proposal.fields.empty()) {
        const CorrelationMatrix correlation = correlate(matrix);
        if (correlation.columns.size() < 2) return false;
        // Same spectral reordering the comparison view uses, so related
        // fields adjoin and block structure is visible at a glance.
        const std::vector<std::size_t> seriated = seriate(correlation.values);
        std::vector<std::string> orderedColumns;
        for (const std::size_t index : seriated) {
            orderedColumns.push_back(correlation.columns[index]);
        }
        nlohmann::json values = nlohmann::json::array();
        for (const std::size_t i : seriated) {
            nlohmann::json row = nlohmann::json::array();
            for (const std::size_t j : seriated) row.push_back(correlation.values[i][j]);
            values.push_back(std::move(row));
        }
        RenderPanel& panel = graph.panel("heatmap", prefix + proposal.title);
        panel.subtitle = proposal.rationale + " (Pearson r, -1 to +1)";
        panel.extra["columns"] = orderedColumns;
        panel.extra["values"] = std::move(values);
        return true;
    }

    if (proposal.chart == "parallel") {
        if (matrix.rowCount() < 4 || matrix.columnCount() < 2) return false;
        const std::size_t limit = std::min<std::size_t>(matrix.rowCount(), 200);
        nlohmann::json rows = nlohmann::json::array();
        nlohmann::json rowLabels = nlohmann::json::array();
        for (std::size_t i = 0; i < limit; ++i) {
            rows.push_back(matrix.rows[i]);
            rowLabels.push_back(matrix.rowLabels[i]);
        }
        RenderPanel& panel = graph.panel("parallel", prefix + proposal.title);
        panel.subtitle = proposal.rationale;
        panel.color = kindColor("record");
        panel.extra["dimensions"] = matrix.columns;
        panel.extra["rows"] = std::move(rows);
        panel.extra["rowLabels"] = std::move(rowLabels);
        return true;
    }

    if (proposal.chart == "line" && !proposal.fields.empty()) {
        const FieldStats* time = findField(fields, proposal.fields[0]);
        if (!time || time->type != FieldType::Date) return false;
        const FieldStats* measure =
            proposal.fields.size() > 1 ? findField(fields, proposal.fields[1]) : nullptr;
        // Bucket by calendar year, which is the coarsest grain that always
        // exists in a date and never needs a calendar library.
        std::map<std::string, std::pair<double, std::size_t>> perPeriod;
        for (const auto& record : profile.samples) {
            const auto when = record.find(time->name);
            if (when == record.end() || !looksLikeDate(when->second)) continue;
            const std::string period = when->second.substr(0, 4);
            auto& bucket = perPeriod[period];
            ++bucket.second;
            if (!measure) continue;
            const auto value = record.find(measure->name);
            if (value == record.end() || !looksNumeric(value->second)) continue;
            bucket.first += std::strtod(value->second.c_str(), nullptr);
        }
        if (perPeriod.size() < 2) return false;
        RenderPanel& panel = graph.panel("line", prefix + proposal.title);
        panel.subtitle = proposal.rationale;
        panel.color = kindColor("event");
        panel.xLabel = time->name;
        panel.orderedCategories = true;  // calendar periods
        RenderPanel::Series series;
        series.name = measure ? ("total " + measure->name) : "records";
        for (const auto& entry : perPeriod) {
            panel.categories.push_back(entry.first);
            series.values.push_back(measure ? entry.second.first
                                            : static_cast<double>(entry.second.second));
        }
        panel.yLabel = series.name;
        panel.series.push_back(std::move(series));
        return true;
    }

    return false;
}

// Text structure of one fact type's string fields, from the corpus itself.
//
// This exists because a large class of real fact data has no numbers in it at
// all. examples/data/converted_csv_country.fx is 249 records of three text
// fields, every one of them near-unique; the numeric pipeline correctly
// declines it at every stage, and the view was left with nothing to say about
// a quarter of a thousand records that plainly do have structure - "Virgin
// Islands, British" and "Virgin Islands, U.S." are related, and no histogram
// was ever going to notice.
//
// What is drawn is derived entirely from the values: the vocabulary comes from
// tokenising them, the weighting from how the terms distribute across them,
// the axes from the SVD of that, and the groups from clustering the result.
// No term, category or family is named anywhere in this file.
//
// Returns the number of panels drawn.
std::size_t buildTextPanels(GraphWriter& graph,
                            const std::string& name,
                            const FactProfile& profile,
                            const std::vector<FieldStats>& fields,
                            std::size_t budget,
                            AnalysisPipeline* pipeline = nullptr) {
    std::size_t drawn = 0;
    // Richest first: the field whose values share the most vocabulary is the
    // one whose structure is worth a reader's attention.
    std::vector<std::pair<std::size_t, const FieldStats*>> candidates;
    for (const auto& field : fields) {
        if (field.type != FieldType::Categorical && field.type != FieldType::Identifier) continue;
        if (field.distinct < kMinSemanticValues) continue;
        const std::vector<VocabularyTerm> vocabulary = textVocabulary(profile, field);
        if (vocabulary.empty()) continue;
        candidates.emplace_back(vocabulary.size(), &field);
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first > b.first;
        return a.second->name < b.second->name;
    });

    // The text bundle's verdict. It is recorded here rather than in
    // analyseFactType because the algorithms behind it live in Reasoning.h,
    // which sits above Analytics and cannot be reached from inside it - the
    // layering is what keeps a bug in an explanation from being able to
    // corrupt a measurement.
    if (pipeline) {
        std::ostringstream finding;
        if (candidates.empty()) {
            finding << "no text field has values sharing vocabulary with each other";
        } else {
            finding << candidates.size() << " text field(s) have values built from "
                    << "shared vocabulary, led by " << candidates.front().second->name
                    << " with " << candidates.front().first << " recurring terms";
        }
        pipeline->bundles.push_back(AlgorithmBundle{
            BundleKind::Text,
            "do these text values share structure?",
            {"tokenisation", "TF-IDF", "truncated SVD", "k-means over latent space"},
            !candidates.empty(),
            finding.str()});
    }

    for (const auto& candidate : candidates) {
        if (drawn >= budget) break;
        const FieldStats& field = *candidate.second;
        const std::string qualified = name + "." + field.name;

        const std::vector<VocabularyTerm> vocabulary = textVocabulary(profile, field);
        {
            std::vector<std::pair<std::string, double>> entries;
            for (const auto& term : vocabulary) {
                entries.emplace_back(term.term, static_cast<double>(term.values));
            }
            addBarPanel(graph, "hbar", qualified + ": shared vocabulary",
                        "words appearing across more than one value, from tokenising the "
                        "values themselves",
                        entries, kindColor("field"));
            ++drawn;

            // Naming the leading terms is the finding. A reader looking at a
            // near-unique field has no other way to learn that a fifth of its
            // values share a word.
            const VocabularyTerm& top = vocabulary.front();
            std::ostringstream text;
            text << qualified << " is near-unique, but its values are not unrelated: \""
                 << top.term << "\" appears in " << top.values << " of " << field.distinct
                 << " distinct values";
            if (vocabulary.size() > 1) {
                text << ", followed by \"" << vocabulary[1].term << "\" in "
                     << vocabulary[1].values;
            }
            text << ".";
            graph.insight("notice", text.str());
        }

        if (drawn >= budget) break;
        const SemanticMap map = semanticMap(profile, field);
        if (!map.valid) {
            // Say why, rather than leaving a gap the reader has to guess at.
            graph.insight("info", qualified + ": no latent structure to map - " + map.reason + ".");
            continue;
        }

        RenderPanel& panel = graph.panel("scatter", qualified + ": values by shared vocabulary");
        std::ostringstream subtitle;
        subtitle << "latent semantic analysis (TF-IDF, truncated SVD); "
                 << displayNumber(std::round(map.explained * 1000.0) / 10.0)
                 << "% of the vocabulary's variation";
        if (map.groups > 1) subtitle << ", " << map.groups << " families";
        if (map.unrelated > 0) {
            subtitle << "; " << map.unrelated << " values share no vocabulary and are not plotted";
        }
        if (map.corpusValues > map.points.size() + map.unrelated) {
            subtitle << "; sampled " << (map.points.size() + map.unrelated) << " of "
                     << map.corpusValues << " distinct values";
        }
        panel.subtitle = subtitle.str();
        // The axes are latent, so they are labelled with the terms that
        // actually define them rather than with a made-up name.
        panel.xLabel = map.axisTermsX.empty() ? "latent 1" : joinWith(map.axisTermsX, " / ");
        panel.yLabel = map.axisTermsY.empty() ? "latent 2" : joinWith(map.axisTermsY, " / ");
        std::set<std::string> groupNames;
        for (const auto& point : map.points) {
            RenderPanel::Point rendered;
            rendered.x = point.x;
            rendered.y = point.y;
            rendered.label = point.value;
            rendered.group = map.groups > 1
                ? "family " + std::to_string(point.group + 1)
                : std::string("values");
            groupNames.insert(rendered.group);
            panel.points.push_back(std::move(rendered));
        }
        panel.extra["groups"] =
            std::vector<std::string>(groupNames.begin(), groupNames.end());
        ++drawn;
    }
    return drawn;
}

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
    // Per type, only the strongest proposals are drawn. The cut is on rank
    // rather than on score alone, so a data-rich fact type cannot crowd out
    // the others.
    constexpr std::size_t kMaxPanelsPerType = 6;
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

        // Which charts this fact type's data supports is measured, not
        // prescribed: the pipeline scores every candidate against the shape of
        // the values and the strongest ones get drawn. A fact type of dated
        // measures produces something entirely different from one of labels,
        // without either arrangement being written down here.
        AnalysisPipeline pipeline =
            analyseFactType(name, profile->second, found->second);
        std::size_t rendered = 0;
        for (const auto& proposal : pipeline.proposals) {
            if (rendered >= kMaxPanelsPerType) break;
            if (renderProposal(graph, name, profile->second, found->second,
                               pipeline.matrix, proposal)) {
                ++rendered;
                typeHadPanel = true;
            }
        }
        emitOutlierAndDriverFindings(graph, name, pipeline);

        // Text structure, with whatever panel budget the numeric proposals did
        // not use. On a fact type of measurements this is usually zero panels
        // and costs nothing; on a fact type of text it is the whole view.
        const std::size_t textPanels =
            buildTextPanels(graph, name, profile->second, found->second,
                            rendered < kMaxPanelsPerType ? kMaxPanelsPerType - rendered : 0,
                            &pipeline);
        rendered += textPanels;
        if (textPanels > 0) typeHadPanel = true;

        // Traced after every bundle has reported, so the reasoning panel
        // shows the whole account rather than the part that happened to run
        // before the text layer.
        graph.trace(name, pipeline.steps);
        graph.summarise(name, pipeline.bundles);

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

                // No chart is built here. Which charts this field earns is
                // decided by the proposal pass above, from measurements of the
                // data rather than from a rule that every numeric field gets a
                // histogram whether or not it has anything to show.
                fieldNode.metrics["farFromMedian"] =
                    static_cast<double>(field.outliers.size());
                if (field.secondPopulation) {
                    // Too many to be anomalies. Saying "12 outliers" here
                    // would be wrong twice over: it invites someone to go
                    // looking for twelve mistakes, and it hides the finding
                    // that actually matters - that this field describes two
                    // different kinds of record.
                    fieldNode.attributes["shape"] = "two populations";
                    std::ostringstream text;
                    text << qualified << ": " << field.outliers.size() << " of "
                         << field.present << " records sit far from the median, which is "
                         << "too many to be anomalies - this field holds two distinct "
                         << "populations rather than one with outliers. The segments view "
                         << "separates them.";
                    graph.insight("notice", text.str());
                } else if (!field.outliers.empty()) {
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

        if (typeHadPanel) {
            ++drawn;
            noteTruncation(graph, name, profile->second);
        }
    }

    if (graph.panels.empty()) {
        // Why there is nothing to draw matters, and the reasons are different.
        // Saying "no fact declares literal values" when every field was a
        // near-unique key is simply false, and sends a reader looking for a
        // problem in their data that is not there.
        std::size_t keyLike = 0;
        std::size_t usable = 0;
        for (const auto& entry : stats) {
            for (const auto& field : entry.second) {
                if (field.type == FieldType::Identifier) ++keyLike;
                else if (field.type != FieldType::Empty) ++usable;
            }
        }
        if (keyLike > 0 && usable == 0) {
            graph.insight("info",
                "Every field here holds a near-unique value, so all " +
                std::to_string(keyLike) + " of them are identifiers rather than "
                "measurements. Charting them would produce one bar per record. "
                "A distribution needs a field that repeats - a category, a "
                "quantity, or a date.");
        } else if (usable > 0) {
            graph.insight("info",
                "The fields here carry values, but none of them varies enough - or "
                "appears on enough records - to describe a distribution.");
        } else {
            graph.insight("info",
                "No fact declares literal values, so there is no distribution to plot. "
                "Facts whose fields are variables rather than literals carry no data "
                "Celidae can measure without running the program.");
        }
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
        if (matrix.columnCount() < 2) continue;
        // Columns from a single field are that field's own levels, and their
        // correlations are fixed by the encoding: every pair of indicators
        // from one categorical is negatively correlated whatever the data
        // says. A heatmap of them is a picture of one-hot encoding.
        if (distinctSourceFields(matrix) < 2) continue;
        // The same floor the notable-pair list uses, applied to the picture as
        // well as to the prose. Three records produce a full square of vivid
        // reds and blues, every cell of which is arithmetic rather than
        // evidence; drawing it while declining to state any of it in words
        // left the strongest-looking chart on the page as the one carrying the
        // least information. A lookup table of three regions is not a data set
        // whose fields can be said to move together.
        if (matrix.rowCount() < kMinCorrelationRows) continue;
        const CorrelationMatrix correlation = correlate(matrix);
        if (correlation.columns.size() < 2) continue;

        // Rows and columns reordered so related fields sit next to each other.
        // The same matrix in declaration order is a confetti of colour; block
        // structure only becomes visible once neighbours are actually related.
        // The ordering is the Fiedler vector of the similarity Laplacian.
        const std::vector<std::size_t> seriated = seriate(correlation.values);
        std::vector<std::string> orderedColumns;
        for (const std::size_t index : seriated) orderedColumns.push_back(correlation.columns[index]);
        nlohmann::json values = nlohmann::json::array();
        for (std::size_t i = 0; i < seriated.size(); ++i) {
            nlohmann::json row = nlohmann::json::array();
            for (const std::size_t j : seriated) row.push_back(correlation.values[seriated[i]][j]);
            values.push_back(std::move(row));
        }
        RenderPanel& panel = graph.panel("heatmap", name + ": how fields move together");
        panel.subtitle = "Pearson correlation, -1 to +1; fields ordered so related ones adjoin";
        panel.extra["columns"] = orderedColumns;
        panel.extra["values"] = std::move(values);

        // Parallel coordinates: the one chart that shows every record across
        // every measure at once, which is how a reader spots a record that is
        // ordinary on each field yet unusual in combination.
        if (matrix.rowCount() >= 4 && matrix.columnCount() <= 10) {
            const std::size_t limit = std::min<std::size_t>(matrix.rowCount(), 200);
            nlohmann::json rows = nlohmann::json::array();
            nlohmann::json rowLabels = nlohmann::json::array();
            for (std::size_t i = 0; i < limit; ++i) {
                rows.push_back(matrix.rows[i]);
                rowLabels.push_back(matrix.rowLabels[i]);
            }
            RenderPanel& parallel = graph.panel("parallel", name + ": records across all measures");
            parallel.subtitle = std::to_string(std::min<std::size_t>(matrix.rowCount(), 200)) +
                " records, " + std::to_string(matrix.columnCount()) + " measures";
            parallel.color = kindColor("record");
            parallel.extra["dimensions"] = matrix.columns;
            parallel.extra["rows"] = std::move(rows);
            parallel.extra["rowLabels"] = std::move(rowLabels);
        }

        // Correlation says two fields move together; a least-squares fit says
        // how much of one is accounted for by all the others at once, and
        // which of them carries the weight. That is the question a reader
        // actually has, and it is a different question.
        const std::vector<DriverModel> drivers = explainNumericTargets(matrix);
        for (const auto& model : drivers) {
            // A bar chart of non-identifiable coefficients is worse than no
            // chart: the bars are drawn to scale, invite comparison, and
            // compare quantities that are not determined. The finding
            // emitted alongside still reports the fit and says why the
            // per-field split is unavailable.
            if (!model.identifiable) continue;
            RenderPanel& driverPanel = graph.panel(
                "hbar", name + ": what moves " + model.target);
            std::ostringstream subtitle;
            subtitle << displayNumber(model.r2 * 100.0) << "% explained (adjusted "
                     << displayNumber(model.adjustedR2 * 100.0) << "%); bars are "
                     << "standardised coefficients, so they compare directly";
            driverPanel.subtitle = subtitle.str();
            driverPanel.color = kindColor("measure");
            RenderPanel::Series series;
            series.name = "influence on " + model.target;
            for (const auto& coefficient : model.coefficients) {
                driverPanel.categories.push_back(coefficient.column);
                series.values.push_back(std::round(coefficient.weight * 1000.0) / 1000.0);
            }
            driverPanel.series.push_back(std::move(series));

            RenderNode& target = graph.node(
                nodeId("measure", name + "." + model.target), model.target, "measure");
            target.attributes["factType"] = name;
            target.metrics["explained"] = std::round(model.r2 * 1000.0) / 10.0;
            target.units["explained"] = "%";
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

// The two analyses that describe individual records rather than groups of
// them. Shared by the cluster and distribution views, which both want them.
void emitOutlierAndDriverFindings(GraphWriter& graph,
                                  const std::string& name,
                                  const AnalysisPipeline& pipeline) {
    // Records that are unremarkable field by field yet unusual taken as a
    // whole - a cheap order that took an hour, a small basket worth a fortune.
    // A per-field z-score cannot see these at all, because no single value is
    // extreme; only the covariance structure makes them visible.
    for (std::size_t i = 0; i < pipeline.outliers.size() && i < 5; ++i) {
        const MultivariateOutlier& outlier = pipeline.outliers[i];
        std::ostringstream text;
        text << name << " " << outlier.label
             << " is unusual in combination rather than on any single field "
             << "(Mahalanobis distance " << displayNumber(outlier.distance);
        if (!outlier.drivers.empty()) {
            text << ", driven by ";
            for (std::size_t d = 0; d < outlier.drivers.size(); ++d) {
                if (d) text << ", ";
                text << outlier.drivers[d];
            }
        }
        text << ").";
        graph.insight("warn", text.str());
    }
    if (pipeline.outliers.size() > 5) {
        graph.insight("notice",
            name + ": " + std::to_string(pipeline.outliers.size() - 5) +
            " further record(s) are also unusual in combination.");
    }

    // What actually moves a number. This is the question a business reader
    // arrives with, and no structural diagram can express it.
    for (const auto& model : pipeline.drivers) {
        std::ostringstream text;
        text << name << ": " << displayNumber(model.r2 * 100.0) << "% of the variation in "
             << model.target << " is explained by ";
        if (!model.identifiable) {
            // The fit is real and the attribution is not. Naming coefficients
            // here would be stating one arbitrary member of an infinite
            // solution set as though it were the answer.
            text << "the other fields taken together, but they are not independent ("
                 << model.rank << " independent signal"
                 << (model.rank == 1 ? "" : "s")
                 << " between them), so the share belonging to any single field cannot be "
                    "determined from this data.";
            graph.insight("notice", text.str());
            continue;
        }
        for (std::size_t i = 0; i < model.coefficients.size() && i < 3; ++i) {
            if (i) text << ", ";
            text << model.coefficients[i].column << " ("
                 << (model.coefficients[i].weight > 0 ? "+" : "")
                 << displayNumber(model.coefficients[i].weight) << ")";
        }
        text << ".";
        graph.insight("info", text.str());
    }
}

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

        // The full multi-step pipeline, not clustering alone: each stage's
        // measurement decides whether the next one runs, and the trace of
        // those decisions travels with the payload.
        const AnalysisPipeline pipeline =
            analyseFactType(name, profile->second, found->second);
        graph.trace(name, pipeline.steps);
        const FeatureMatrix& matrix = pipeline.matrix;
        const ClusterResult& clusters = pipeline.clusters;

        // Structural findings hold whether or not segmentation went ahead.
        if (pipeline.structure.valid && pipeline.structure.collinear) {
            std::ostringstream text;
            text << name << ": " << pipeline.structure.effectiveRank << " of "
                 << pipeline.structure.columns
                 << " encoded fields carry independent information";
            if (!pipeline.structure.redundantPairs.empty()) {
                text << " - " << pipeline.structure.redundantPairs.front().first << " and "
                     << pipeline.structure.redundantPairs.front().second
                     << " say the same thing";
            }
            text << ".";
            graph.insight("notice", text.str());
        }
        if (pipeline.structure.valid && !pipeline.structure.worthClustering) {
            // The check that stops this view inventing structure. k-means
            // will cut evenly-spread data into tidy-looking segments and
            // score them well, because a silhouette measures whether a cut is
            // clean, never whether there was anything there to cut.
            std::ostringstream text;
            text << name << ": records are spread evenly rather than falling into "
                 << "pockets (cluster tendency "
                 << displayNumber(pipeline.structure.clusterTendency)
                 << ", where 0.5 is a uniform spread), so no segmentation is offered - "
                 << "any split would be an arbitrary cut through continuous data.";
            graph.insight("notice", text.str());
        }
        emitOutlierAndDriverFindings(graph, name, pipeline);

        if (!clusters.valid) {
            if (!clusters.reason.empty() && !matrix.columns.empty() &&
                pipeline.structure.worthClustering) {
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
        panel.extra["groups"] = groupNames;

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

        // k-means says which records group together; it does not say why. An
        // oblique decision tree answers that in a form a person can act on:
        // a short arithmetic test over the fields themselves. The splits are
        // weighted combinations rather than single-field thresholds, because
        // the boundary between two segments almost never runs parallel to an
        // axis - and approximating a diagonal boundary with axis-aligned cuts
        // produces a staircase of rules that explains nothing.
        const ObliqueTree tree = obliqueTree(matrix, clusters.assignment, groupNames);
        if (tree.valid) {
            std::ostringstream summary;
            summary << name << ": the segments are separated by "
                    << (tree.rules.size() == 1 ? "one rule" : std::to_string(tree.rules.size()) + " rules")
                    << ", which reproduce " << displayNumber(tree.accuracy * 100.0)
                    << "% of the grouping (measured on the same records the rules were "
                       "derived from, so treat it as an upper bound).";
            graph.insight("info", summary.str());
            for (const auto& rule : tree.rules) {
                graph.insight("info", name + " rule: " + rule);
            }

            // The tree as an actual tree, which is the shape of the thing.
            std::function<nlohmann::json(std::size_t)> asJson =
                [&](std::size_t index) -> nlohmann::json {
                    const ObliqueTreeNode& node = tree.nodes[index];
                    nlohmann::json out;
                    out["name"] = node.leaf
                        ? (static_cast<std::size_t>(node.majorityClass) < tree.classNames.size()
                               ? tree.classNames[static_cast<std::size_t>(node.majorityClass)]
                               : std::string("group"))
                        : node.split.description;
                    out["value"] = node.records;
                    if (!node.leaf) {
                        out["children"] =
                            nlohmann::json::array({asJson(node.left), asJson(node.right)});
                    }
                    return out;
                };
            RenderPanel& treePanel =
                graph.panel("tree", name + ": what separates the segments");
            std::ostringstream treeSubtitle;
            treeSubtitle << "each branch is a weighted test across fields; "
                         << displayNumber(tree.accuracy * 100.0) << "% of records follow it";
            treePanel.subtitle = treeSubtitle.str();
            treePanel.extra["tree"] = nlohmann::json::array({asJson(0)});
        } else if (!tree.reason.empty()) {
            graph.insight("notice", name + ": the segments cannot be reduced to a readable rule (" +
                          tree.reason + ").");
        }

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
        case DiagramType::Er:
            buildErGraph(graph, impl_->facts, stats, impl_->references);
            break;
        case DiagramType::Schema:
        case DiagramType::Graph: {
            const bool includeFields = type != DiagramType::Graph;
            const bool includeExecution = type == DiagramType::Graph;
            const bool includeImports = true;
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
            }
            break;
        }
    }

    // Every network-rendered view gets a connectivity-based layout, computed
    // once here rather than per-view, so a view added later gets it free.
    attachSpectralPositions(graph);

    RenderedGraph result;
    result.nodes.assign(graph.nodes.begin(), graph.nodes.end());
    result.edges = std::move(graph.edges);
    result.panels = std::move(graph.panels);
    result.insights = std::move(graph.insights);
    result.reasoning = std::move(graph.reasoning);
    result.bundles = std::move(graph.bundles);
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
        if (node.positioned) {
            out << ",\"position\":{\"x\":" << jsonNumber(node.layoutX)
                << ",\"y\":" << jsonNumber(node.layoutY) << "}";
        }
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
    out << "],\"reasoning\":[";
    for (std::size_t i = 0; i < result.reasoning.size(); ++i) {
        if (i) out << ",";
        out << "{\"stage\":" << quoted(result.reasoning[i].stage)
            << ",\"finding\":" << quoted(result.reasoning[i].finding)
            << ",\"decision\":" << quoted(result.reasoning[i].decision) << "}";
    }
    out << "],\"bundles\":[";
    for (std::size_t i = 0; i < result.bundles.size(); ++i) {
        if (i) out << ",";
        const AlgorithmBundle& entry = result.bundles[i];
        out << "{\"kind\":" << quoted(bundleKindName(entry.kind))
            << ",\"question\":" << quoted(entry.question)
            << ",\"applicable\":" << (entry.applicable ? "true" : "false")
            << ",\"algorithms\":" << jsonStringArray(entry.algorithms)
            << ",\"finding\":" << quoted(entry.finding) << "}";
    }
    out << "],\"recommendations\":[";
    const std::vector<Recommendation> recommendations = recommendViews(shape());
    for (std::size_t i = 0; i < recommendations.size(); ++i) {
        if (i) out << ",";
        out << "{\"view\":" << quoted(diagramTypeName(recommendations[i].view))
            << ",\"score\":" << jsonNumber(recommendations[i].score)
            << ",\"applicable\":" << (recommendations[i].applicable ? "true" : "false")
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
    // Rendered by inja rather than by scanning the page for magic strings.
    //
    // The previous approach called find() for each "__DATA_<TYPE>__" token and
    // replaced the first hit. That is fragile in a way that would have been
    // very hard to diagnose: the inlined cytoscape and ECharts sources sit
    // *above* the data elements in the document, so a token appearing anywhere
    // in a megabyte of minified third-party JavaScript would have been
    // substituted instead of the real one, silently emptying a view.
    //
    // Substitution is now driven by a data object, so a name that does not
    // exist raises an error naming it instead of leaving a token in the page,
    // and nothing outside the declared placeholders can be touched.
    //
    // The delimiters are deliberately not inja's defaults. The generated page
    // contains 8 occurrences of "{{" and over 2000 of "}}" inside minified CSS
    // and JavaScript; parsing that as template syntax would corrupt the
    // libraries. "<#" and "#>" occur nowhere in it.
    nlohmann::json context;
    context["data"] = nlohmann::json::object();
    for (DiagramType type : kAllDiagramTypes) {
        const auto found = payloads.find(type);
        if (found == payloads.end()) {
            context["data"][diagramTypeName(type)] = "null";
            continue;
        }
        // Two guarantees before a payload is embedded, both cheap and both
        // covering failures that are invisible until a browser gives up on
        // the whole page:
        //
        //   1. It parses. A payload assembled correctly but serialised wrong
        //      produces a blank tab and no error anywhere.
        //   2. It cannot terminate the surrounding <script> element. JSON
        //      escaping does not touch "</script>" - it is valid inside a JSON
        //      string - but HTML parsing ends the element at it regardless, so
        //      a fact label containing that text would break out of the data
        //      island and into the document.
        try {
            (void)nlohmann::json::parse(found->second);
        } catch (const std::exception& error) {
            throw std::runtime_error(
                std::string("Celidae built an invalid payload for the '") +
                diagramTypeName(type) + "' view: " + error.what());
        }
        context["data"][diagramTypeName(type)] = scriptSafeJson(found->second);
    }

    inja::Environment environment;
    environment.set_expression("<#", "#>");
    // The payloads are already JSON; inja must place them verbatim rather than
    // re-encoding them as JSON strings.
    environment.set_trim_blocks(false);
    environment.set_lstrip_blocks(false);
    try {
        return environment.render(kVisualizerTemplate, context);
    } catch (const std::exception& error) {
        throw std::runtime_error(
            std::string("Celidae could not render the visualizer template: ") + error.what());
    }
}

} // namespace Felidae::Celidae
