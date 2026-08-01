#include "celidae/Visualization.h"

#include "BuiltinRegistry.h"
#include "celidae/GeneratedVisualizerAssets.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_map>

namespace Felidae::Celidae {

namespace {

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

struct FactProfile {
    std::size_t records = 0;
    std::map<std::string, std::size_t> fields;
};

// Accumulates a graph as structured node/edge records (shared by every
// output format) plus a JSON serialization on demand, so the JSON, HTML data
// payload, and SVG layout engine all read from one classification pass in
// SchemaGraphAccumulator::buildGraph instead of three parallel ones.
struct GraphWriter {
    std::vector<RenderNode> nodes;
    std::vector<RenderEdge> edges;
    std::set<std::string> nodeIds;
    std::set<std::string> edgeIds;

    void node(const std::string& id,
              const std::string& label,
              const std::string& kind,
              const std::string& detail = {}) {
        if (!nodeIds.insert(id).second) return;
        nodes.push_back(RenderNode{id, label, kind, detail});
    }

    void edge(const std::string& from, const std::string& to, const std::string& label) {
        const std::string identity = from + "\n" + to + "\n" + label;
        if (!edgeIds.insert(identity).second) return;
        edges.push_back(RenderEdge{from, to, label});
    }

    std::string toJson(std::size_t factCount,
                       std::size_t methodCount,
                       std::size_t globalCount,
                       const char* mode) const {
        std::ostringstream out;
        out << "{\"schemaVersion\":2,\"mode\":\"" << mode << "\",\"summary\":{"
            << "\"factTypes\":" << factCount << ","
            << "\"methods\":" << methodCount << ","
            << "\"globals\":" << globalCount << "},"
            << "\"truncation\":{\"truncated\":false,\"omittedNodes\":0,"
               "\"omittedEdges\":0},"
            << "\"nodes\":[";
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (i) out << ",";
            out << "{\"id\":\"" << jsonEscape(nodes[i].id)
                << "\",\"label\":\"" << jsonEscape(nodes[i].label)
                << "\",\"kind\":\"" << jsonEscape(nodes[i].kind) << "\"";
            if (!nodes[i].detail.empty()) out << ",\"detail\":\"" << jsonEscape(nodes[i].detail) << "\"";
            out << "}";
        }
        out << "],\"edges\":[";
        for (size_t i = 0; i < edges.size(); ++i) {
            if (i) out << ",";
            const std::string identity = edges[i].from + "\n" + edges[i].to + "\n" + edges[i].label;
            out << "{\"id\":\"" << stableEdgeId(identity)
                << "\",\"from\":\"" << jsonEscape(edges[i].from)
                << "\",\"to\":\"" << jsonEscape(edges[i].to)
                << "\",\"label\":\"" << jsonEscape(edges[i].label) << "\"}";
        }
        out << "]}";
        return out.str();
    }
};

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
    } else if (auto binary = std::dynamic_pointer_cast<BinaryExpr>(expr)) {
        visitExpr(binary->left, visitCall);
        visitExpr(binary->right, visitCall);
    } else if (auto pipeline = std::dynamic_pointer_cast<PipelineExpr>(expr)) {
        visitExpr(pipeline->left, visitCall);
        visitExpr(pipeline->right, visitCall);
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

std::string scriptSafeJson(std::string json) {
    for (size_t pos = json.find("</"); pos != std::string::npos; pos = json.find("</", pos + 3)) {
        json.replace(pos, 2, "<\\/");
    }
    return json;
}

void replaceToken(std::string& text, const std::string& token, const std::string& value) {
    const size_t pos = text.find(token);
    if (pos == std::string::npos) {
        throw std::runtime_error("Celidae visualizer template is missing token: " + token);
    }
    text.replace(pos, token.size(), value);
}

} // namespace

struct SchemaGraphAccumulator::Impl {
    std::map<std::string, FactProfile> facts;
    std::set<std::string> methods;
    std::set<std::string> globals;
    std::set<std::string> imports;
    std::set<std::tuple<std::string, std::string, std::string>> references;
};

SchemaGraphAccumulator::SchemaGraphAccumulator() : impl_(std::make_unique<Impl>()) {}
SchemaGraphAccumulator::~SchemaGraphAccumulator() = default;
SchemaGraphAccumulator::SchemaGraphAccumulator(SchemaGraphAccumulator&&) noexcept = default;
SchemaGraphAccumulator& SchemaGraphAccumulator::operator=(SchemaGraphAccumulator&&) noexcept = default;

void SchemaGraphAccumulator::consume(const std::shared_ptr<Statement>& statement) {
    if (!statement) return;
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
        for (const auto& argument : clause->head.args) {
            if (!argument.name.empty()) fieldsInRecord.insert(argument.name);
        }
        for (const auto& field : fieldsInRecord) ++profile.fields[field];
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
                "method:" + clause->head.name,
                "Fact:depends",
                "attaches dependency");
            return;
        }
        if (hasSuffix(called, ":relate")) {
            impl_->references.emplace(
                "method:" + clause->head.name,
                "Fact:relate",
                "attaches relationship");
            return;
        }
        if (hasSuffix(called, ":references")) {
            impl_->references.emplace(
                "method:" + clause->head.name,
                "Fact:references",
                "attaches reference");
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

RenderedGraph SchemaGraphAccumulator::buildGraph(
    DiagramType type,
    const std::vector<std::string>& unresolvedImports) const {
    GraphWriter graph;
    const bool includeFields = type != DiagramType::Graph;
    const bool includeExecution = type == DiagramType::Graph;
    const bool includeImports = type != DiagramType::Er;
    auto classify = [&](const std::string& name) {
        if (impl_->methods.count(name)) return std::string("method");
        if (impl_->facts.count(name)) return std::string("fact");
        if (isBuiltinFunctionName(name)) return std::string("library");
        if (name == "Fact:depends" || name == "Fact:relate") {
            return std::string("library");
        }
        const auto separator = name.find(':');
        if (separator != std::string::npos &&
            impl_->imports.count(name.substr(0, separator))) {
            return std::string("library");
        }
        return std::string("external");
    };

    for (const auto& item : impl_->facts) {
        std::ostringstream detail;
        detail << "records=" << item.second.records << " fields=" << item.second.fields.size();
        graph.node(nodeId("fact", item.first), item.first, "fact", detail.str());
        if (!includeFields) continue;
        for (const auto& field : item.second.fields) {
            const std::string fieldName = item.first + "." + field.first;
            const std::size_t missing = item.second.records - field.second;
            const double coverage = item.second.records == 0
                ? 0.0
                : static_cast<double>(field.second) * 100.0 /
                    static_cast<double>(item.second.records);
            std::ostringstream fieldDetail;
            fieldDetail << "present=" << field.second << " missing=" << missing
                        << " coverage=" << std::fixed << std::setprecision(1)
                        << coverage << "%";
            graph.node(nodeId("field", fieldName), field.first, "field", fieldDetail.str());
            graph.edge(nodeId("fact", item.first), nodeId("field", fieldName), "field");
        }
    }
    if (includeExecution) for (const auto& method : impl_->methods) {
        graph.node(nodeId("method", method), method, "method");
    }
    if (includeExecution) for (const auto& global : impl_->globals) {
        graph.node(nodeId("global", global), global, "global");
    }
    if (includeImports) for (const auto& import : impl_->imports) {
        graph.node(nodeId("library", import), import, "library");
    }
    if (includeImports) for (const auto& unresolved : unresolvedImports) {
        graph.node(
            nodeId("library", unresolved),
            unresolved,
            "library",
            "source not resolved; may be native");
    }

    for (const auto& reference : impl_->references) {
        const std::string& from = std::get<0>(reference);
        const std::string& targetName = std::get<1>(reference);
        const std::string& label = std::get<2>(reference);
        if (!includeExecution && label != "extends") continue;
        const std::string kind = label == "extends" ? "fact" : classify(targetName);
        graph.node(nodeId(kind, targetName), targetName, kind);
        graph.edge(from, nodeId(kind, targetName), label);
    }

    const char* mode = type == DiagramType::Er ? "er" :
        (type == DiagramType::Graph ? "graph" : "schema");
    RenderedGraph result;
    result.nodes = graph.nodes;
    result.edges = graph.edges;
    result.json = graph.toJson(impl_->facts.size(), impl_->methods.size(), impl_->globals.size(), mode);
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

namespace {

// Server-computed layout shared by standaloneSvg. The interactive HTML runs
// the same three algorithms client-side (so a user can re-layout/drag
// live); this C++ copy is what makes a static SVG possible without a JS
// runtime.
struct Point { double x = 0, y = 0; };

std::map<std::string, Point> computeLaneLayout(const RenderedGraph& graph) {
    static const std::vector<std::string> kinds = {"fact", "field", "method", "global", "library"};
    std::map<std::string, int> columnByKind;
    for (size_t i = 0; i < kinds.size(); ++i) columnByKind[kinds[i]] = static_cast<int>(i);
    std::map<std::string, std::vector<const RenderNode*>> lanes;
    for (const auto& node : graph.nodes) lanes[node.kind].push_back(&node);

    std::map<std::string, Point> positions;
    const double colWidth = 230;
    const double rowHeight = 76;
    for (auto& [kind, nodes] : lanes) {
        const int column = columnByKind.count(kind) ? columnByKind[kind] : static_cast<int>(kinds.size());
        for (size_t row = 0; row < nodes.size(); ++row) {
            positions[nodes[row]->id] = Point{70.0 + column * colWidth, 55.0 + static_cast<double>(row) * rowHeight};
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
            default: out += ch; break;
        }
    }
    return out;
}

const char* kindColor(const std::string& kind) {
    if (kind == "fact") return "#18f0d7";
    if (kind == "field") return "#f7c948";
    if (kind == "method") return "#6ca8ff";
    if (kind == "global") return "#f27d9d";
    if (kind == "library") return "#9ca3af";
    return "#9ca3af";
}

} // namespace

std::string standaloneSvg(const RenderedGraph& graph, DiagramType type) {
    const auto positions = computeLaneLayout(graph);
    double maxX = 400, maxY = 400;
    for (const auto& [id, point] : positions) {
        maxX = std::max(maxX, point.x + 220);
        maxY = std::max(maxY, point.y + 60);
    }

    std::ostringstream out;
    const char* title = type == DiagramType::Er ? "Celidae ER Diagram" :
        (type == DiagramType::Graph ? "Celidae Dependency Graph" : "Celidae Fact Schema");
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " << maxX << " " << maxY
        << "\" width=\"" << maxX << "\" height=\"" << maxY << "\" font-family=\"Segoe UI, Inter, sans-serif\">\n"
        << "<title>" << xmlEscape(title) << "</title>\n"
        << "<defs><marker id=\"arrow\" viewBox=\"0 0 10 10\" refX=\"9\" refY=\"5\" markerWidth=\"6\" markerHeight=\"6\" orient=\"auto-start-reverse\">"
        << "<path d=\"M 0 0 L 10 5 L 0 10 z\" fill=\"#376760\"/></marker></defs>\n"
        << "<rect x=\"0\" y=\"0\" width=\"" << maxX << "\" height=\"" << maxY << "\" fill=\"#0b1210\"/>\n";

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
        std::string label = node.label.size() > 19 ? node.label.substr(0, 18) + "\xE2\x80\xA6" : node.label;
        out << "<g>\n"
            << "  <rect x=\"" << x << "\" y=\"" << y << "\" width=\"150\" height=\"40\" rx=\"7\""
            << " fill=\"" << color << "\" fill-opacity=\"0.22\" stroke=\"" << color << "\" stroke-width=\"1.4\"/>\n"
            << "  <text x=\"" << (x + 10) << "\" y=\"" << (y + 24) << "\" fill=\"#e6f7f4\" font-size=\"12\">"
            << xmlEscape(label) << "</text>\n"
            << "  <title>" << xmlEscape(node.kind + ": " + node.label + (node.detail.empty() ? "" : " \xE2\x80\x94 " + node.detail)) << "</title>\n"
            << "</g>\n";
    }

    out << "</svg>\n";
    return out.str();
}

std::string standaloneHtml(const std::string& schemaJson, const std::string& graphJson, const std::string& erJson) {
    // kVisualizerTemplate is generated from src/celidae/webui/template.html
    // (cytoscape + chart.js + heroicons, all npm-installed there - see
    // GeneratedVisualizerAssets.h). This function only substitutes the three
    // DiagramType payloads into the pre-built, self-contained page.
    std::string html = kVisualizerTemplate;
    replaceToken(html, "__DATA_SCHEMA__", scriptSafeJson(schemaJson));
    replaceToken(html, "__DATA_GRAPH__", scriptSafeJson(graphJson));
    replaceToken(html, "__DATA_ER__", scriptSafeJson(erJson));
    return html;
}

} // namespace Felidae::Celidae
