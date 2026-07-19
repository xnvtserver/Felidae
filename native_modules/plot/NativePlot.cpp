#include "NativePlot.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Json {
    enum Kind { Null, Number, String, Array, Object } kind = Null;
    double number = 0.0;
    std::string text;
    std::vector<Json> items;
    std::map<std::string, Json> fields;
};

void skipWs(const std::string& s, size_t& p) {
    while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) ++p;
}

bool parseValue(const std::string& s, size_t& p, Json& out);

bool parseString(const std::string& s, size_t& p, std::string& out) {
    if (p >= s.size() || s[p] != '"') return false;
    ++p;
    while (p < s.size()) {
        char c = s[p++];
        if (c == '"') return true;
        if (c == '\\' && p < s.size()) {
            char e = s[p++];
            if (e == 'n') out.push_back('\n');
            else if (e == 't') out.push_back('\t');
            else out.push_back(e);
        } else {
            out.push_back(c);
        }
    }
    return false;
}

bool parseArray(const std::string& s, size_t& p, Json& out) {
    if (p >= s.size() || s[p] != '[') return false;
    ++p;
    out.kind = Json::Array;
    skipWs(s, p);
    if (p < s.size() && s[p] == ']') { ++p; return true; }
    while (p < s.size()) {
        Json item;
        if (!parseValue(s, p, item)) return false;
        out.items.push_back(std::move(item));
        skipWs(s, p);
        if (p < s.size() && s[p] == ',') { ++p; continue; }
        if (p < s.size() && s[p] == ']') { ++p; return true; }
        return false;
    }
    return false;
}

bool parseObject(const std::string& s, size_t& p, Json& out) {
    if (p >= s.size() || s[p] != '{') return false;
    ++p;
    out.kind = Json::Object;
    skipWs(s, p);
    if (p < s.size() && s[p] == '}') { ++p; return true; }
    while (p < s.size()) {
        std::string key;
        if (!parseString(s, p, key)) return false;
        skipWs(s, p);
        if (p >= s.size() || s[p] != ':') return false;
        ++p;
        Json value;
        if (!parseValue(s, p, value)) return false;
        out.fields[key] = std::move(value);
        skipWs(s, p);
        if (p < s.size() && s[p] == ',') { ++p; continue; }
        if (p < s.size() && s[p] == '}') { ++p; return true; }
        return false;
    }
    return false;
}

bool parseNumber(const std::string& s, size_t& p, Json& out) {
    size_t start = p;
    if (p < s.size() && s[p] == '-') ++p;
    while (p < s.size() && std::isdigit(static_cast<unsigned char>(s[p]))) ++p;
    if (p < s.size() && s[p] == '.') {
        ++p;
        while (p < s.size() && std::isdigit(static_cast<unsigned char>(s[p]))) ++p;
    }
    if (start == p) return false;
    out.kind = Json::Number;
    out.number = std::strtod(s.substr(start, p - start).c_str(), nullptr);
    return true;
}

bool parseValue(const std::string& s, size_t& p, Json& out) {
    skipWs(s, p);
    if (p >= s.size()) return false;
    if (s[p] == '"') { out.kind = Json::String; return parseString(s, p, out.text); }
    if (s[p] == '[') return parseArray(s, p, out);
    if (s[p] == '{') return parseObject(s, p, out);
    if (s.compare(p, 4, "null") == 0) { p += 4; out.kind = Json::Null; return true; }
    if (s.compare(p, 4, "true") == 0) { p += 4; out.kind = Json::String; out.text = "true"; return true; }
    if (s.compare(p, 5, "false") == 0) { p += 5; out.kind = Json::String; out.text = "false"; return true; }
    return parseNumber(s, p, out);
}

Json parseJson(const char* raw) {
    std::string text = raw ? raw : "{}";
    size_t p = 0;
    Json out;
    if (!parseValue(text, p, out)) throw std::runtime_error("invalid plot JSON payload");
    return out;
}

const Json* field(const Json& object, const std::string& key) {
    if (object.kind != Json::Object) return nullptr;
    auto found = object.fields.find(key);
    return found == object.fields.end() ? nullptr : &found->second;
}

std::string esc(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else if (c == '\n') out += "\\n";
        else if (c == '\t') out += "\\t";
        else out.push_back(c);
    }
    return out;
}

std::string q(const std::string& s) { return "\"" + esc(s) + "\""; }

std::string scalarText(const Json& value) {
    if (value.kind == Json::String) return value.text;
    if (value.kind == Json::Number) {
        std::ostringstream out;
        out << value.number;
        return out.str();
    }
    return "";
}

std::string stringField(const Json& args, const std::string& name, const std::string& fallback = "") {
    const Json* value = field(args, name);
    if (!value) return fallback;
    return scalarText(*value);
}

const Json& requireFacts(const Json& args) {
    const Json* facts = field(args, "facts");
    if (!facts || facts->kind != Json::Array) throw std::runtime_error("plot expects facts array");
    return *facts;
}

double numberField(const Json& object, const std::string& name, bool& ok) {
    const Json* value = field(object, name);
    ok = value && value->kind == Json::Number && std::isfinite(value->number);
    return ok ? value->number : 0.0;
}

std::string plotEnvelope(const std::string& kind, const std::string& title, const std::string& svg, size_t count) {
    return "{\"__type\":\"Plot\",\"kind\":" + q(kind) + ",\"title\":" + q(title) +
           ",\"format\":\"svg\",\"count\":" + std::to_string(count) + ",\"svg\":" + q(svg) + "}";
}

std::string scatterResponse(const Json& args) {
    const Json& facts = requireFacts(args);
    const std::string xField = stringField(args, "x");
    const std::string yField = stringField(args, "y");
    const std::string labelField = stringField(args, "label", "__type");
    const std::string title = stringField(args, "title", "Felidae scatter plot");
    if (xField.empty() || yField.empty()) throw std::runtime_error("plot.scatter expects x and y fields");
    struct Point { double x; double y; std::string label; };
    std::vector<Point> points;
    for (const auto& fact : facts.items) {
        bool xOk = false, yOk = false;
        double x = numberField(fact, xField, xOk);
        double y = numberField(fact, yField, yOk);
        if (xOk && yOk) points.push_back(Point{x, y, scalarText(*field(fact, labelField))});
    }
    if (points.empty()) throw std::runtime_error("plot.scatter found no facts with numeric x and y fields");
    double minX = points[0].x, maxX = points[0].x, minY = points[0].y, maxY = points[0].y;
    for (const auto& p : points) { minX = std::min(minX, p.x); maxX = std::max(maxX, p.x); minY = std::min(minY, p.y); maxY = std::max(maxY, p.y); }
    const double w = 720, h = 420, left = 64, top = 36, pw = 600, ph = 320;
    auto sx = [&](double v) { return left + ((v - minX) / std::max(1e-9, maxX - minX)) * pw; };
    auto sy = [&](double v) { return top + ph - ((v - minY) / std::max(1e-9, maxY - minY)) * ph; };
    std::ostringstream svg;
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " << w << " " << h << "\" role=\"img\">"
        << "<rect width=\"100%\" height=\"100%\" fill=\"#fbfeff\"/><text x=\"64\" y=\"24\" font-size=\"18\" font-family=\"Segoe UI,Arial\">" << esc(title) << "</text>"
        << "<line x1=\"64\" y1=\"356\" x2=\"664\" y2=\"356\" stroke=\"#24303f\"/><line x1=\"64\" y1=\"36\" x2=\"64\" y2=\"356\" stroke=\"#24303f\"/>";
    for (const auto& p : points) {
        svg << "<circle cx=\"" << sx(p.x) << "\" cy=\"" << sy(p.y) << "\" r=\"6\" fill=\"#18f0d7\" stroke=\"#17313a\"/>"
            << "<text x=\"" << (sx(p.x) + 9) << "\" y=\"" << (sy(p.y) - 8) << "\" font-size=\"12\" font-family=\"Segoe UI,Arial\">" << esc(p.label) << "</text>";
    }
    svg << "<text x=\"360\" y=\"402\" font-size=\"13\" text-anchor=\"middle\" font-family=\"Segoe UI,Arial\">" << esc(xField) << "</text>"
        << "<text x=\"16\" y=\"196\" font-size=\"13\" transform=\"rotate(-90 16 196)\" text-anchor=\"middle\" font-family=\"Segoe UI,Arial\">" << esc(yField) << "</text></svg>";
    return plotEnvelope("scatter", title, svg.str(), points.size());
}

std::string barResponse(const Json& args) {
    const Json& facts = requireFacts(args);
    const std::string categoryField = stringField(args, "category");
    const std::string valueField = stringField(args, "value");
    const std::string title = stringField(args, "title", "Felidae bar plot");
    if (categoryField.empty() || valueField.empty()) throw std::runtime_error("plot.bar expects category and value fields");
    struct Bar { std::string label; double value; };
    std::vector<Bar> bars;
    for (const auto& fact : facts.items) {
        bool ok = false;
        double value = numberField(fact, valueField, ok);
        const Json* label = field(fact, categoryField);
        if (ok && label) bars.push_back(Bar{scalarText(*label), value});
    }
    if (bars.empty()) throw std::runtime_error("plot.bar found no facts with category and numeric value fields");
    double maxValue = 0.0;
    for (const auto& b : bars) maxValue = std::max(maxValue, b.value);
    const double w = 720, h = 420, left = 64, top = 42, pw = 600, ph = 300;
    const double gap = 12;
    const double bw = std::max(12.0, (pw - gap * (bars.size() + 1)) / static_cast<double>(bars.size()));
    std::ostringstream svg;
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " << w << " " << h << "\" role=\"img\">"
        << "<rect width=\"100%\" height=\"100%\" fill=\"#fbfeff\"/><text x=\"64\" y=\"24\" font-size=\"18\" font-family=\"Segoe UI,Arial\">" << esc(title) << "</text>"
        << "<line x1=\"64\" y1=\"342\" x2=\"664\" y2=\"342\" stroke=\"#24303f\"/>";
    for (size_t i = 0; i < bars.size(); ++i) {
        double height = (bars[i].value / std::max(1e-9, maxValue)) * ph;
        double x = left + gap + static_cast<double>(i) * (bw + gap);
        double y = top + ph - height;
        svg << "<rect x=\"" << x << "\" y=\"" << y << "\" width=\"" << bw << "\" height=\"" << height << "\" rx=\"3\" fill=\"#18f0d7\"/>"
            << "<text x=\"" << (x + bw / 2) << "\" y=\"366\" text-anchor=\"middle\" font-size=\"11\" font-family=\"Segoe UI,Arial\">" << esc(bars[i].label) << "</text>";
    }
    svg << "</svg>";
    return plotEnvelope("bar", title, svg.str(), bars.size());
}

std::string pieResponse(const Json& args) {
    const Json& facts = requireFacts(args);
    const std::string categoryField = stringField(args, "category");
    const std::string valueField = stringField(args, "value");
    const std::string title = stringField(args, "title", "Felidae pie chart");
    if (categoryField.empty() || valueField.empty()) throw std::runtime_error("plot.pie expects category and value fields");
    struct Slice { std::string label; double value; };
    std::vector<Slice> slices;
    double total = 0.0;
    for (const auto& fact : facts.items) {
        bool ok = false;
        double value = numberField(fact, valueField, ok);
        const Json* label = field(fact, categoryField);
        if (ok && label && value > 0.0) {
            slices.push_back(Slice{scalarText(*label), value});
            total += value;
        }
    }
    if (slices.empty() || total <= 0.0) throw std::runtime_error("plot.pie found no positive numeric values");
    const char* colors[] = {"#18f0d7", "#5eead4", "#2dd4bf", "#14b8a6", "#0f766e", "#99f6e4"};
    const double cx = 230, cy = 220, r = 140;
    double angle = -1.5707963267948966;
    std::ostringstream svg;
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 720 440\" role=\"img\"><rect width=\"100%\" height=\"100%\" fill=\"#fbfeff\"/>"
        << "<text x=\"42\" y=\"30\" font-size=\"18\" font-family=\"Segoe UI,Arial\">" << esc(title) << "</text>";
    for (size_t i = 0; i < slices.size(); ++i) {
        const double sweep = (slices[i].value / total) * 6.283185307179586;
        const double end = angle + sweep;
        const double x1 = cx + std::cos(angle) * r;
        const double y1 = cy + std::sin(angle) * r;
        const double x2 = cx + std::cos(end) * r;
        const double y2 = cy + std::sin(end) * r;
        const int large = sweep > 3.141592653589793 ? 1 : 0;
        svg << "<path d=\"M " << cx << " " << cy << " L " << x1 << " " << y1 << " A " << r << " " << r << " 0 " << large << " 1 " << x2 << " " << y2 << " Z\" fill=\"" << colors[i % 6] << "\" stroke=\"#fbfeff\" stroke-width=\"2\"/>";
        svg << "<rect x=\"430\" y=\"" << (80 + i * 28) << "\" width=\"14\" height=\"14\" fill=\"" << colors[i % 6] << "\"/><text x=\"452\" y=\"" << (92 + i * 28) << "\" font-size=\"13\" font-family=\"Segoe UI,Arial\">" << esc(slices[i].label) << " " << slices[i].value << "</text>";
        angle = end;
    }
    svg << "</svg>";
    return plotEnvelope("pie", title, svg.str(), slices.size());
}

std::string histogramResponse(const Json& args) {
    const Json& facts = requireFacts(args);
    const std::string valueField = stringField(args, "value");
    const std::string title = stringField(args, "title", "Felidae histogram");
    const size_t bins = static_cast<size_t>(std::max(1.0, std::min(24.0, field(args, "bins") && field(args, "bins")->kind == Json::Number ? field(args, "bins")->number : 6.0)));
    if (valueField.empty()) throw std::runtime_error("plot.histogram expects value field");
    std::vector<double> values;
    for (const auto& fact : facts.items) {
        bool ok = false;
        double value = numberField(fact, valueField, ok);
        if (ok) values.push_back(value);
    }
    if (values.empty()) throw std::runtime_error("plot.histogram found no numeric values");
    double minValue = *std::min_element(values.begin(), values.end());
    double maxValue = *std::max_element(values.begin(), values.end());
    std::vector<double> counts(bins, 0.0);
    for (double value : values) {
        size_t index = static_cast<size_t>(((value - minValue) / std::max(1e-9, maxValue - minValue)) * static_cast<double>(bins));
        if (index >= bins) index = bins - 1;
        counts[index] += 1.0;
    }
    Json barArgs;
    barArgs.kind = Json::Object;
    barArgs.fields["category"] = Json{Json::String, 0.0, "bucket", {}, {}};
    barArgs.fields["value"] = Json{Json::String, 0.0, "count", {}, {}};
    barArgs.fields["title"] = Json{Json::String, 0.0, title, {}, {}};
    Json factArray;
    factArray.kind = Json::Array;
    for (size_t i = 0; i < counts.size(); ++i) {
        Json row;
        row.kind = Json::Object;
        Json label;
        label.kind = Json::String;
        label.text = std::to_string(i + 1);
        Json count;
        count.kind = Json::Number;
        count.number = counts[i];
        row.fields["bucket"] = label;
        row.fields["count"] = count;
        factArray.items.push_back(row);
    }
    barArgs.fields["facts"] = factArray;
    std::string out = barResponse(barArgs);
    size_t kindPos = out.find("\"kind\":\"bar\"");
    if (kindPos != std::string::npos) out.replace(kindPos, 12, "\"kind\":\"histogram\"");
    return out;
}

std::string timeSeriesResponse(const Json& args) {
    const Json& facts = requireFacts(args);
    const std::string timeField = stringField(args, "time");
    const std::string valueField = stringField(args, "value");
    const std::string title = stringField(args, "title", "Felidae time series");
    if (timeField.empty() || valueField.empty()) throw std::runtime_error("plot.time_series expects time and value fields");
    struct Point { std::string label; double value; };
    std::vector<Point> points;
    for (const auto& fact : facts.items) {
        bool ok = false;
        double value = numberField(fact, valueField, ok);
        const Json* label = field(fact, timeField);
        if (ok && label) points.push_back(Point{scalarText(*label), value});
    }
    if (points.empty()) throw std::runtime_error("plot.time_series found no numeric values");
    double minY = points[0].value, maxY = points[0].value;
    for (const auto& p : points) { minY = std::min(minY, p.value); maxY = std::max(maxY, p.value); }
    const double left = 64, top = 42, pw = 600, ph = 300;
    auto sx = [&](size_t i) { return left + (points.size() == 1 ? pw / 2.0 : (static_cast<double>(i) / static_cast<double>(points.size() - 1)) * pw); };
    auto sy = [&](double v) { return top + ph - ((v - minY) / std::max(1e-9, maxY - minY)) * ph; };
    std::ostringstream svg;
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 720 420\" role=\"img\"><rect width=\"100%\" height=\"100%\" fill=\"#fbfeff\"/>"
        << "<text x=\"64\" y=\"24\" font-size=\"18\" font-family=\"Segoe UI,Arial\">" << esc(title) << "</text>"
        << "<line x1=\"64\" y1=\"342\" x2=\"664\" y2=\"342\" stroke=\"#24303f\"/><line x1=\"64\" y1=\"42\" x2=\"64\" y2=\"342\" stroke=\"#24303f\"/><polyline points=\"";
    for (size_t i = 0; i < points.size(); ++i) svg << sx(i) << "," << sy(points[i].value) << " ";
    svg << "\" fill=\"none\" stroke=\"#18f0d7\" stroke-width=\"3\"/>";
    for (size_t i = 0; i < points.size(); ++i) {
        svg << "<circle cx=\"" << sx(i) << "\" cy=\"" << sy(points[i].value) << "\" r=\"5\" fill=\"#08363a\"/>"
            << "<text x=\"" << sx(i) << "\" y=\"366\" text-anchor=\"middle\" font-size=\"11\" font-family=\"Segoe UI,Arial\">" << esc(points[i].label) << "</text>";
    }
    svg << "</svg>";
    return plotEnvelope("time_series", title, svg.str(), points.size());
}

std::string relationshipResponse(const Json& args) {
    const Json& facts = requireFacts(args);
    const std::string title = stringField(args, "title", "Felidae fact relationship graph");
    struct Node { std::string id; double x; double y; };
    std::vector<Node> nodes;
    std::vector<std::pair<std::string, std::string>> edges;
    std::set<std::string> seen;
    for (const auto& fact : facts.items) {
        const std::string type = stringField(fact, "__type");
        const std::string parent = stringField(fact, "__parent");
        if (!type.empty() && seen.insert(type).second) nodes.push_back(Node{type, 0, 0});
        if (!parent.empty() && seen.insert(parent).second) nodes.push_back(Node{parent, 0, 0});
        if (!type.empty() && !parent.empty()) edges.push_back({type, parent});
    }
    if (nodes.empty()) throw std::runtime_error("plot.relationships found no typed facts");
    const double w = 760, h = 520, cx = 380, cy = 270;
    for (size_t i = 0; i < nodes.size(); ++i) {
        double a = (static_cast<double>(i) / static_cast<double>(nodes.size())) * 6.283185307179586;
        double r = 150 + static_cast<double>(i % 3) * 38;
        nodes[i].x = cx + std::cos(a) * r;
        nodes[i].y = cy + std::sin(a) * r;
    }
    auto findNode = [&](const std::string& id) -> const Node* {
        for (const auto& node : nodes) if (node.id == id) return &node;
        return nullptr;
    };
    std::ostringstream svg;
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " << w << " " << h << "\" role=\"img\">"
        << "<rect width=\"100%\" height=\"100%\" fill=\"#081114\"/><text x=\"32\" y=\"32\" font-size=\"19\" fill=\"#eafffb\" font-family=\"Segoe UI,Arial\">" << esc(title) << "</text>";
    for (const auto& e : edges) {
        const Node* a = findNode(e.first);
        const Node* b = findNode(e.second);
        if (!a || !b) continue;
        svg << "<line x1=\"" << a->x << "\" y1=\"" << a->y << "\" x2=\"" << b->x << "\" y2=\"" << b->y << "\" stroke=\"#507079\" stroke-width=\"1.4\"/>";
    }
    for (const auto& n : nodes) {
        svg << "<circle cx=\"" << n.x << "\" cy=\"" << n.y << "\" r=\"17\" fill=\"#18f0d7\" stroke=\"#eafffb\"/>"
            << "<text x=\"" << (n.x + 22) << "\" y=\"" << (n.y + 4) << "\" font-size=\"12\" fill=\"#eafffb\" font-family=\"Segoe UI,Arial\">" << esc(n.id) << "</text>";
    }
    svg << "</svg>";
    return plotEnvelope("relationships", title, svg.str(), nodes.size());
}

std::string htmlResponse(const Json& args) {
    const Json* plot = field(args, "plot");
    if (!plot || plot->kind != Json::Object) throw std::runtime_error("plot.html expects plot object");
    const std::string title = stringField(args, "title", stringField(*plot, "title", "Felidae plot"));
    const std::string svg = stringField(*plot, "svg");
    std::string html = "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>" +
                       esc(title) + "</title><style>body{margin:0;font-family:Segoe UI,Arial,sans-serif;background:#f7fffe;color:#101820}main{max-width:980px;margin:0 auto;padding:24px}section{margin:18px 0}svg{width:100%;height:auto;border:1px solid #c7f7f2;border-radius:8px}</style></head><body><main><h1>" +
                       esc(title) + "</h1><section>" + svg + "</section></main></body></html>";
    return "{\"__type\":\"PlotHtml\",\"title\":" + q(title) + ",\"html\":" + q(html) + "}";
}

std::string controlResponse(const Json& args, const std::string& kind) {
    const std::string id = stringField(args, "id", kind);
    const std::string label = stringField(args, "label", id);
    const std::string value = stringField(args, "value", label);
    return "{\"__type\":\"PlotControl\",\"kind\":" + q(kind) + ",\"id\":" + q(id) + ",\"label\":" + q(label) + ",\"value\":" + q(value) + "}";
}

std::string dashboardResponse(const Json& args) {
    const Json* plots = field(args, "plots");
    const Json* controls = field(args, "controls");
    if (!plots || plots->kind != Json::Array) throw std::runtime_error("plot.dashboard expects plots array");
    const std::string title = stringField(args, "title", "Felidae visualization dashboard");
    std::ostringstream html;
    html << "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>" << esc(title)
         << "</title><style>body{margin:0;font-family:Segoe UI,Arial,sans-serif;background:#071114;color:#eafffb}main{max-width:1080px;margin:0 auto;padding:22px}.toolbar{display:flex;gap:8px;flex-wrap:wrap;margin:14px 0 18px}button,label{border:1px solid #18f0d7;background:#0e1f23;color:#eafffb;border-radius:999px;padding:8px 12px;cursor:pointer}.plot{display:none;background:#f7fffe;border-radius:8px;padding:10px}.plot.active{display:block}svg{width:100%;height:auto}</style></head><body><main><h1>" << esc(title) << "</h1><div class=\"toolbar\">";
    if (controls && controls->kind == Json::Array) {
        for (size_t i = 0; i < controls->items.size(); ++i) {
            const std::string kind = stringField(controls->items[i], "kind", "button");
            const std::string label = esc(stringField(controls->items[i], "label", "Plot"));
            if (kind == "radio") html << "<label data-plot=\"" << i << "\"><input type=\"radio\" name=\"plotMode\"" << (i == 0 ? " checked" : "") << "> " << label << "</label>";
            else if (kind == "checkbox") html << "<label data-plot=\"" << i << "\"><input type=\"checkbox\"> " << label << "</label>";
            else html << "<button type=\"button\" data-plot=\"" << i << "\">" << label << "</button>";
        }
    }
    html << "</div>";
    for (size_t i = 0; i < plots->items.size(); ++i) {
        html << "<section class=\"plot" << (i == 0 ? " active" : "") << "\" data-index=\"" << i << "\">" << stringField(plots->items[i], "svg") << "</section>";
    }
    html << "<script>const plots=[...document.querySelectorAll('.plot')];document.querySelectorAll('[data-plot]').forEach((b,i)=>b.onclick=()=>{plots.forEach(p=>p.classList.remove('active'));(plots[i]||plots[0]).classList.add('active')});</script></main></body></html>";
    return "{\"__type\":\"PlotDashboard\",\"title\":" + q(title) + ",\"html\":" + q(html.str()) + ",\"plot_count\":" + std::to_string(plots->items.size()) + "}";
}

std::string dispatch(const std::string& function, const Json& args) {
    std::string call = function;
    const std::string prefix = "system:flibrary:plot:";
    if (call.rfind(prefix, 0) == 0) call = "plot:" + call.substr(prefix.size());
    if (call == "plot:scatter") return scatterResponse(args);
    if (call == "plot:bar") return barResponse(args);
    if (call == "plot:pie") return pieResponse(args);
    if (call == "plot:histogram") return histogramResponse(args);
    if (call == "plot:time_series") return timeSeriesResponse(args);
    if (call == "plot:relationships") return relationshipResponse(args);
    if (call == "plot:html") return htmlResponse(args);
    if (call == "plot:button") return controlResponse(args, "button");
    if (call == "plot:radio") return controlResponse(args, "radio");
    if (call == "plot:checkbox") return controlResponse(args, "checkbox");
    if (call == "plot:dashboard") return dashboardResponse(args);
    return "{\"error\":\"Unsupported plot native function\"}";
}

char* copyResponse(const std::string& value) {
    char* out = static_cast<char*>(std::malloc(value.size() + 1));
    if (!out) return nullptr;
    std::memcpy(out, value.c_str(), value.size() + 1);
    return out;
}

} // namespace

extern "C" FELIDAE_PLOT_EXPORT char* felidae_native_call(const char* functionName, const char* argsJson) {
    try {
        Json args = parseJson(argsJson ? argsJson : "{}");
        return copyResponse(dispatch(functionName ? functionName : "", args));
    } catch (const std::exception& ex) {
        return copyResponse(std::string("{\"error\":\"") + esc(ex.what()) + "\"}");
    } catch (...) {
        return copyResponse("{\"error\":\"Unknown native plot module failure\"}");
    }
}

extern "C" FELIDAE_PLOT_EXPORT void felidae_native_free(char* value) {
    std::free(value);
}
