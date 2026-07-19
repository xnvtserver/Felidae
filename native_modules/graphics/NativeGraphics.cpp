#include "NativeGraphics.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
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
    if (!parseValue(text, p, out)) throw std::runtime_error("invalid graphics JSON payload");
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

std::string xmlEsc(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '"') out += "&quot;";
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

double numberField(const Json& args, const std::string& name, double fallback = 0.0) {
    const Json* value = field(args, name);
    return value && value->kind == Json::Number && std::isfinite(value->number) ? value->number : fallback;
}

std::string moduleName(const std::string& functionName) {
    if (functionName.find(":qt:") != std::string::npos) return "qt";
    return "gtk";
}

std::string operationName(const std::string& functionName) {
    size_t pos = functionName.rfind(':');
    return pos == std::string::npos ? functionName : functionName.substr(pos + 1);
}

std::string element(const std::string& module, const std::string& kind, const std::string& props) {
    return "{\"__type\":\"" + module + "." + kind + "\",\"module\":" + q(module) + ",\"kind\":" + q(kind) + props + "}";
}

std::string primitiveResponse(const std::string& module, const std::string& kind, const Json& args) {
    std::ostringstream props;
    if (kind == "canvas") {
        props << ",\"width\":" << numberField(args, "width", 720)
              << ",\"height\":" << numberField(args, "height", 420)
              << ",\"title\":" << q(stringField(args, "title", "Felidae graphics canvas"));
    } else if (kind == "circle") {
        props << ",\"cx\":" << numberField(args, "cx", 100)
              << ",\"cy\":" << numberField(args, "cy", 100)
              << ",\"radius\":" << numberField(args, "radius", 40)
              << ",\"name\":" << q(stringField(args, "name", "circle"))
              << ",\"fill\":" << q(stringField(args, "fill", "#18f0d7"));
    } else if (kind == "rect") {
        props << ",\"x\":" << numberField(args, "x", 20)
              << ",\"y\":" << numberField(args, "y", 20)
              << ",\"width\":" << numberField(args, "width", 120)
              << ",\"height\":" << numberField(args, "height", 72)
              << ",\"name\":" << q(stringField(args, "name", "rect"))
              << ",\"fill\":" << q(stringField(args, "fill", "#18f0d7"));
    } else if (kind == "line") {
        props << ",\"x1\":" << numberField(args, "x1", 0)
              << ",\"y1\":" << numberField(args, "y1", 0)
              << ",\"x2\":" << numberField(args, "x2", 100)
              << ",\"y2\":" << numberField(args, "y2", 100)
              << ",\"name\":" << q(stringField(args, "name", "line"))
              << ",\"stroke\":" << q(stringField(args, "stroke", "#10292d"))
              << ",\"width\":" << numberField(args, "width", 2);
    } else if (kind == "sector") {
        props << ",\"cx\":" << numberField(args, "cx", 220)
              << ",\"cy\":" << numberField(args, "cy", 220)
              << ",\"radius\":" << numberField(args, "radius", 140)
              << ",\"start\":" << numberField(args, "start", 0)
              << ",\"end\":" << numberField(args, "end", 45)
              << ",\"name\":" << q(stringField(args, "name", "sector"))
              << ",\"fill\":" << q(stringField(args, "fill", "#18f0d7"))
              << ",\"weight\":" << numberField(args, "weight", 1);
    } else if (kind == "p" || kind == "h1" || kind == "text") {
        props << ",\"content\":" << q(stringField(args, "content", ""))
              << ",\"x\":" << numberField(args, "x", kind == "h1" ? 36 : 48)
              << ",\"y\":" << numberField(args, "y", kind == "h1" ? 44 : 82)
              << ",\"size\":" << numberField(args, "size", kind == "h1" ? 26 : 15);
    } else if (kind == "plot") {
        const Json* plot = field(args, "plot");
        props << ",\"title\":" << q(plot && plot->kind == Json::Object ? stringField(*plot, "title", "Felidae plot") : stringField(args, "title", "Felidae plot"))
              << ",\"svg\":" << q(plot && plot->kind == Json::Object ? stringField(*plot, "svg", "") : stringField(args, "svg", ""))
              << ",\"x\":" << numberField(args, "x", 48)
              << ",\"y\":" << numberField(args, "y", 110)
              << ",\"width\":" << numberField(args, "width", 640)
              << ",\"height\":" << numberField(args, "height", 360);
    } else {
        props << ",\"id\":" << q(stringField(args, "id", kind))
              << ",\"label\":" << q(stringField(args, "label", kind))
              << ",\"value\":" << q(stringField(args, "value", kind))
              << ",\"x\":" << numberField(args, "x", 48)
              << ",\"y\":" << numberField(args, "y", 110)
              << ",\"width\":" << numberField(args, "width", 120)
              << ",\"height\":" << numberField(args, "height", 34);
    }
    return element(module, kind, props.str());
}

std::string pieFromFactsResponse(const std::string& module, const Json& args) {
    const Json* facts = field(args, "facts");
    if (!facts || facts->kind != Json::Array) throw std::runtime_error(module + ".pieFromFacts expects facts array");
    const std::string labelField = stringField(args, "label");
    const std::string valueField = stringField(args, "value");
    if (labelField.empty() || valueField.empty()) throw std::runtime_error(module + ".pieFromFacts expects label and value fields");
    const double cx = numberField(args, "cx", 240);
    const double cy = numberField(args, "cy", 240);
    const double radius = numberField(args, "radius", 150);
    double total = 0.0;
    for (const auto& fact : facts->items) {
        const Json* value = field(fact, valueField);
        if (value && value->kind == Json::Number && value->number > 0) total += value->number;
    }
    if (total <= 0.0) throw std::runtime_error(module + ".pieFromFacts found no positive values");
    const char* colors[] = {"#18f0d7", "#5eead4", "#2dd4bf", "#14b8a6", "#0f766e", "#99f6e4", "#67e8f9", "#22d3ee"};
    std::ostringstream out;
    out << "[";
    double start = 0.0;
    size_t emitted = 0;
    for (size_t i = 0; i < facts->items.size(); ++i) {
        const Json* value = field(facts->items[i], valueField);
        const Json* label = field(facts->items[i], labelField);
        if (!value || value->kind != Json::Number || value->number <= 0) continue;
        const double width = (value->number / total) * 360.0;
        if (emitted) out << ",";
        out << "{\"__type\":\"" << module << ".sector\",\"module\":" << q(module)
            << ",\"kind\":\"sector\",\"cx\":" << cx << ",\"cy\":" << cy
            << ",\"radius\":" << radius << ",\"start\":" << start
            << ",\"end\":" << (start + width)
            << ",\"name\":" << q(label ? scalarText(*label) : ("sector" + std::to_string(i + 1)))
            << ",\"fill\":" << q(colors[emitted % 8])
            << ",\"weight\":" << value->number << "}";
        start += width;
        ++emitted;
    }
    out << "]";
    return "{\"__type\":\"" + module + ".pieSectors\",\"module\":" + q(module) + ",\"kind\":\"pie_sectors\",\"count\":" + std::to_string(emitted) + ",\"elements\":" + out.str() + "}";
}

Json factsByTypeArgs(const Json& args, const std::string& targetType, const std::string& factFieldName) {
    const Json* allFacts = field(args, "__facts");
    if (!allFacts || allFacts->kind != Json::Array) throw std::runtime_error("graphics native call expected interpreter fact index");
    Json filtered;
    filtered.kind = Json::Array;
    for (const auto& fact : allFacts->items) {
        const Json* type = field(fact, "__type");
        if (type && scalarText(*type) == targetType) filtered.items.push_back(fact);
    }
    if (filtered.items.empty()) throw std::runtime_error("No indexed facts found for type '" + targetType + "'");
    Json out = args;
    out.fields[factFieldName] = filtered;
    return out;
}

std::string pieFromFactTypeResponse(const std::string& module, const Json& args) {
    const std::string type = stringField(args, "type");
    if (type.empty()) throw std::runtime_error(module + ".pieFromFactType expects type");
    Json mapped = factsByTypeArgs(args, type, "facts");
    return pieFromFactsResponse(module, mapped);
}

std::string graphFromFactsResponse(const std::string& module, const Json& args) {
    const Json* edges = field(args, "edges");
    if (!edges || edges->kind != Json::Array) throw std::runtime_error(module + ".graphFromFacts expects edges array");
    const std::string fromField = stringField(args, "from", "from");
    const std::string toField = stringField(args, "to", "to");
    const std::string labelField = stringField(args, "label", "label");
    const double cx = numberField(args, "cx", 380);
    const double cy = numberField(args, "cy", 260);
    const double radius = numberField(args, "radius", 170);
    std::vector<std::string> nodes;
    std::vector<std::pair<std::string, std::string>> links;
    for (const auto& edge : edges->items) {
        const Json* from = field(edge, fromField);
        const Json* to = field(edge, toField);
        if (!from || !to) continue;
        const std::string a = scalarText(*from);
        const std::string b = scalarText(*to);
        if (a.empty() || b.empty()) continue;
        if (std::find(nodes.begin(), nodes.end(), a) == nodes.end()) nodes.push_back(a);
        if (std::find(nodes.begin(), nodes.end(), b) == nodes.end()) nodes.push_back(b);
        links.push_back({a, b});
    }
    if (nodes.empty()) throw std::runtime_error(module + ".graphFromFacts found no usable edges");
    auto indexOf = [&](const std::string& id) {
        return static_cast<size_t>(std::distance(nodes.begin(), std::find(nodes.begin(), nodes.end(), id)));
    };
    auto nx = [&](size_t i) {
        const double angle = (static_cast<double>(i) / static_cast<double>(nodes.size())) * 6.283185307179586;
        return cx + std::cos(angle) * radius;
    };
    auto ny = [&](size_t i) {
        const double angle = (static_cast<double>(i) / static_cast<double>(nodes.size())) * 6.283185307179586;
        return cy + std::sin(angle) * radius;
    };
    std::ostringstream out;
    out << "[";
    size_t emitted = 0;
    for (const auto& link : links) {
        const size_t a = indexOf(link.first);
        const size_t b = indexOf(link.second);
        if (emitted) out << ",";
        out << "{\"__type\":\"" << module << ".line\",\"module\":" << q(module) << ",\"kind\":\"line\",\"x1\":" << nx(a)
            << ",\"y1\":" << ny(a) << ",\"x2\":" << nx(b) << ",\"y2\":" << ny(b)
            << ",\"name\":" << q(link.first + "->" + link.second) << ",\"stroke\":\"#507079\",\"width\":2}";
        ++emitted;
    }
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (emitted) out << ",";
        out << "{\"__type\":\"" << module << ".circle\",\"module\":" << q(module) << ",\"kind\":\"circle\",\"cx\":" << nx(i)
            << ",\"cy\":" << ny(i) << ",\"radius\":24,\"name\":" << q(nodes[i]) << ",\"fill\":\"#18f0d7\"}";
        ++emitted;
    }
    for (size_t i = 0; i < edges->items.size(); ++i) {
        const Json* label = field(edges->items[i], labelField);
        if (!label) continue;
        const Json* from = field(edges->items[i], fromField);
        const Json* to = field(edges->items[i], toField);
        if (!from || !to) continue;
        const std::string aText = scalarText(*from);
        const std::string bText = scalarText(*to);
        auto aIt = std::find(nodes.begin(), nodes.end(), aText);
        auto bIt = std::find(nodes.begin(), nodes.end(), bText);
        if (aIt == nodes.end() || bIt == nodes.end()) continue;
        const size_t a = static_cast<size_t>(std::distance(nodes.begin(), aIt));
        const size_t b = static_cast<size_t>(std::distance(nodes.begin(), bIt));
        if (emitted) out << ",";
        out << "{\"__type\":\"" << module << ".text\",\"module\":" << q(module) << ",\"kind\":\"text\",\"x\":" << ((nx(a) + nx(b)) / 2.0)
            << ",\"y\":" << ((ny(a) + ny(b)) / 2.0) << ",\"content\":" << q(scalarText(*label)) << ",\"size\":12}";
        ++emitted;
    }
    out << "]";
    return "{\"__type\":\"" + module + ".graph\",\"module\":" + q(module) + ",\"kind\":\"graph\",\"node_count\":" + std::to_string(nodes.size()) + ",\"edge_count\":" + std::to_string(links.size()) + ",\"elements\":" + out.str() + "}";
}

std::string graphFromFactTypeResponse(const std::string& module, const Json& args) {
    const std::string type = stringField(args, "type");
    if (type.empty()) throw std::runtime_error(module + ".graphFromFactType expects type");
    Json mapped = factsByTypeArgs(args, type, "edges");
    return graphFromFactsResponse(module, mapped);
}

void appendRenderedElement(std::ostringstream& svg, const Json& item) {
    if (item.kind == Json::Array) {
        for (const auto& child : item.items) appendRenderedElement(svg, child);
        return;
    }
    if (item.kind != Json::Object) return;

    std::string kind = stringField(item, "kind");
    if (kind == "circle") {
        svg << "<circle cx=\"" << numberField(item, "cx", 100) << "\" cy=\"" << numberField(item, "cy", 100)
            << "\" r=\"" << numberField(item, "radius", 40) << "\" fill=\"" << xmlEsc(stringField(item, "fill", "#18f0d7"))
            << "\" stroke=\"#10292d\"/><text x=\"" << (numberField(item, "cx", 100) + numberField(item, "radius", 40) + 8)
            << "\" y=\"" << numberField(item, "cy", 100) << "\" font-size=\"13\">" << xmlEsc(stringField(item, "name", "")) << "</text>";
    } else if (kind == "rect") {
        svg << "<rect x=\"" << numberField(item, "x", 20) << "\" y=\"" << numberField(item, "y", 20)
            << "\" width=\"" << numberField(item, "width", 120) << "\" height=\"" << numberField(item, "height", 72)
            << "\" rx=\"8\" fill=\"" << xmlEsc(stringField(item, "fill", "#18f0d7")) << "\" stroke=\"#10292d\"/>"
            << "<text x=\"" << (numberField(item, "x", 20) + 12) << "\" y=\"" << (numberField(item, "y", 20) + 24)
            << "\" font-size=\"13\">" << xmlEsc(stringField(item, "name", "")) << "</text>";
    } else if (kind == "line") {
        svg << "<line x1=\"" << numberField(item, "x1", 0) << "\" y1=\"" << numberField(item, "y1", 0)
            << "\" x2=\"" << numberField(item, "x2", 100) << "\" y2=\"" << numberField(item, "y2", 100)
            << "\" stroke=\"" << xmlEsc(stringField(item, "stroke", "#10292d")) << "\" stroke-width=\"" << numberField(item, "width", 2) << "\"/>";
    } else if (kind == "sector") {
        const double cx = numberField(item, "cx", 220), cy = numberField(item, "cy", 220), r = numberField(item, "radius", 140);
        const double start = numberField(item, "start", 0) * 3.141592653589793 / 180.0;
        const double end = numberField(item, "end", 45) * 3.141592653589793 / 180.0;
        const double x1 = cx + std::cos(start) * r, y1 = cy + std::sin(start) * r;
        const double x2 = cx + std::cos(end) * r, y2 = cy + std::sin(end) * r;
        const int large = (end - start) > 3.141592653589793 ? 1 : 0;
        svg << "<path d=\"M " << cx << " " << cy << " L " << x1 << " " << y1 << " A " << r << " " << r << " 0 " << large << " 1 " << x2 << " " << y2 << " Z\" fill=\"" << xmlEsc(stringField(item, "fill", "#18f0d7")) << "\" stroke=\"#fbfeff\" stroke-width=\"2\"/>";
    } else if (kind == "h1" || kind == "p" || kind == "text") {
        svg << "<text x=\"" << numberField(item, "x", 48) << "\" y=\"" << numberField(item, "y", 82)
            << "\" font-size=\"" << numberField(item, "size", 15) << "\" font-family=\"Segoe UI,Arial\">" << xmlEsc(stringField(item, "content", "")) << "</text>";
    } else if (kind == "plot") {
        svg << "<foreignObject x=\"" << numberField(item, "x", 48) << "\" y=\"" << numberField(item, "y", 110)
            << "\" width=\"" << numberField(item, "width", 640) << "\" height=\"" << numberField(item, "height", 360)
            << "\"><div xmlns=\"http://www.w3.org/1999/xhtml\" style=\"width:100%;height:100%;overflow:hidden;border:1px solid #c7f7f2;border-radius:8px;background:#f7fffe\">"
            << stringField(item, "svg", "") << "</div></foreignObject>";
    } else if (kind == "button" || kind == "radio" || kind == "checkbox") {
        const std::string label = xmlEsc(stringField(item, "label", kind));
        svg << "<foreignObject x=\"" << numberField(item, "x", 48) << "\" y=\"" << numberField(item, "y", 110)
            << "\" width=\"" << numberField(item, "width", 140) << "\" height=\"" << numberField(item, "height", 38) << "\"><div xmlns=\"http://www.w3.org/1999/xhtml\">";
        if (kind == "radio") svg << "<label style=\"font:13px Segoe UI,Arial;color:#10292d\"><input type=\"radio\"/> " << label << "</label>";
        else if (kind == "checkbox") svg << "<label style=\"font:13px Segoe UI,Arial;color:#10292d\"><input type=\"checkbox\"/> " << label << "</label>";
        else svg << "<button type=\"button\" style=\"border:1px solid #18f0d7;background:#0e1f23;color:#eafffb;border-radius:999px;padding:8px 12px;font:13px Segoe UI,Arial\">" << label << "</button>";
        svg << "</div></foreignObject>";
    }
}

std::string renderResponse(const std::string& module, const Json& args) {
    const Json* canvas = field(args, "canvas");
    const Json* elements = field(args, "elements");
    if (!canvas || canvas->kind != Json::Object || !elements || elements->kind != Json::Array) {
        throw std::runtime_error(module + ".render expects canvas object and elements array");
    }
    const double width = numberField(*canvas, "width", 720);
    const double height = numberField(*canvas, "height", 420);
    const std::string title = stringField(*canvas, "title", module + " render");
    std::ostringstream svg;
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " << width << " " << height << "\" role=\"img\">"
        << "<rect width=\"100%\" height=\"100%\" fill=\"#fbfeff\"/>";
    for (const auto& item : elements->items) {
        appendRenderedElement(svg, item);
    }
    svg << "</svg>";
    std::string html = "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>" +
                       esc(title) + "</title><style>body{margin:0;background:#071114;color:#eafffb;font-family:Segoe UI,Arial,sans-serif}main{max-width:980px;margin:0 auto;padding:22px}.toolbar{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:14px}button,label{border:1px solid #18f0d7;background:#0e1f23;color:#eafffb;border-radius:999px;padding:8px 12px}svg{width:100%;height:auto;border-radius:8px}</style></head><body><main><h1>" +
                       esc(title) + "</h1><section>" + svg.str() + "</section></main></body></html>";
    return "{\"__type\":\"" + module + ".render\",\"module\":" + q(module) + ",\"kind\":\"render\",\"title\":" + q(title) + ",\"svg\":" + q(svg.str()) + ",\"html\":" + q(html) + "}";
}

std::string dispatch(const std::string& function, const Json& args) {
    const std::string module = moduleName(function);
    const std::string op = operationName(function);
    if (op == "button_at") return primitiveResponse(module, "button", args);
    if (op == "radio_at") return primitiveResponse(module, "radio", args);
    if (op == "checkbox_at") return primitiveResponse(module, "checkbox", args);
    if (op == "canvas" || op == "circle" || op == "rect" || op == "line" || op == "sector" || op == "text" || op == "p" || op == "h1" ||
        op == "button" || op == "radio" || op == "checkbox" || op == "plot") return primitiveResponse(module, op, args);
    if (op == "pie_from_facts") return pieFromFactsResponse(module, args);
    if (op == "pie_from_fact_type") return pieFromFactTypeResponse(module, args);
    if (op == "graph_from_facts") return graphFromFactsResponse(module, args);
    if (op == "graph_from_fact_type") return graphFromFactTypeResponse(module, args);
    if (op == "render") return renderResponse(module, args);
    return "{\"error\":\"Unsupported " + module + " graphics function '" + esc(op) + "'\"}";
}

char* copyResponse(const std::string& value) {
    char* out = static_cast<char*>(std::malloc(value.size() + 1));
    if (!out) return nullptr;
    std::memcpy(out, value.c_str(), value.size() + 1);
    return out;
}

} // namespace

extern "C" FELIDAE_GRAPHICS_EXPORT char* felidae_native_call(const char* functionName, const char* argsJson) {
    try {
        Json args = parseJson(argsJson ? argsJson : "{}");
        return copyResponse(dispatch(functionName ? functionName : "", args));
    } catch (const std::exception& ex) {
        return copyResponse(std::string("{\"error\":\"") + esc(ex.what()) + "\"}");
    } catch (...) {
        return copyResponse("{\"error\":\"Unknown native graphics module failure\"}");
    }
}

extern "C" FELIDAE_GRAPHICS_EXPORT void felidae_native_free(char* value) {
    std::free(value);
}
