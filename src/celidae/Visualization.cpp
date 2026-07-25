#include "celidae/Visualization.h"

#include "BuiltinRegistry.h"

#include <algorithm>
#include <functional>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
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

struct FactProfile {
    std::size_t records = 0;
    std::map<std::string, std::size_t> fields;
};

struct GraphWriter {
    std::ostringstream nodes;
    std::ostringstream edges;
    bool firstNode = true;
    bool firstEdge = true;
    std::set<std::string> nodeIds;
    std::set<std::string> edgeIds;

    void node(const std::string& id,
              const std::string& label,
              const std::string& kind,
              const std::string& detail = {}) {
        if (!nodeIds.insert(id).second) return;
        if (!firstNode) nodes << ",";
        firstNode = false;
        nodes << "{\"id\":\"" << jsonEscape(id)
              << "\",\"label\":\"" << jsonEscape(label)
              << "\",\"kind\":\"" << jsonEscape(kind) << "\"";
        if (!detail.empty()) nodes << ",\"detail\":\"" << jsonEscape(detail) << "\"";
        nodes << "}";
    }

    void edge(const std::string& from, const std::string& to, const std::string& label) {
        const std::string identity = from + "\n" + to + "\n" + label;
        if (!edgeIds.insert(identity).second) return;
        if (!firstEdge) edges << ",";
        firstEdge = false;
        edges << "{\"from\":\"" << jsonEscape(from)
              << "\",\"to\":\"" << jsonEscape(to)
              << "\",\"label\":\"" << jsonEscape(label) << "\"}";
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

} // namespace

std::string graphJson(const Program& program,
                      const std::vector<std::string>& unresolvedImports) {
    GraphWriter graph;
    std::map<std::string, FactProfile> facts;
    std::set<std::string> methods;
    std::set<std::string> globals;
    std::set<std::string> imports;

    for (const auto& import : program.imports) {
        imports.insert(import->paths.begin(), import->paths.end());
    }
    for (const auto& global : program.globals) globals.insert(global->name);
    for (const auto& clause : program.clauses) {
        if (clause->isFact()) {
            auto& profile = facts[clause->head.name];
            profile.records++;
            std::set<std::string> fieldsInRecord;
            for (const auto& arg : clause->head.args) {
                if (!arg.name.empty()) fieldsInRecord.insert(arg.name);
            }
            for (const auto& field : fieldsInRecord) profile.fields[field]++;
            if (!clause->parentName.empty()) facts.try_emplace(clause->parentName);
        } else {
            methods.insert(clause->head.name);
        }
    }

    auto classify = [&](const std::string& name) {
        if (methods.count(name)) return std::string("method");
        if (facts.count(name)) return std::string("fact");
        if (isBuiltinFunctionName(name)) return std::string("library");
        return std::string("fact");
    };

    for (const auto& item : facts) {
        std::ostringstream detail;
        detail << "records=" << item.second.records << " fields=" << item.second.fields.size();
        graph.node(nodeId("fact", item.first), item.first, "fact", detail.str());
        for (const auto& field : item.second.fields) {
            const std::string fieldName = item.first + "." + field.first;
            const std::size_t missing = item.second.records - field.second;
            const double coverage = item.second.records == 0
                ? 0.0
                : static_cast<double>(field.second) * 100.0 / static_cast<double>(item.second.records);
            std::ostringstream fieldDetail;
            fieldDetail << "present=" << field.second << " missing=" << missing
                        << " coverage=" << std::fixed << std::setprecision(1) << coverage << "%";
            graph.node(nodeId("field", fieldName), field.first, "field", fieldDetail.str());
            graph.edge(nodeId("fact", item.first), nodeId("field", fieldName), "field");
        }
    }

    for (const auto& method : methods) graph.node(nodeId("method", method), method, "method");
    for (const auto& global : globals) graph.node(nodeId("global", global), global, "global");
    for (const auto& import : imports) graph.node(nodeId("library", import), import, "library");
    for (const auto& unresolved : unresolvedImports) {
        graph.node(nodeId("library", unresolved), unresolved, "library", "source not resolved; may be native");
    }

    for (const auto& clause : program.clauses) {
        if (clause->isFact()) {
            if (!clause->parentName.empty()) {
                graph.edge(
                    nodeId("fact", clause->head.name),
                    nodeId("fact", clause->parentName),
                    "extends");
            }
            continue;
        }

        const std::string from = nodeId("method", clause->head.name);
        auto addCall = [&](const std::string& name) {
            const std::string kind = classify(name);
            graph.node(nodeId(kind, name), name, kind);
            graph.edge(from, nodeId(kind, name), "calls");
        };
        for (const auto& goal : clause->body) visitGoal(goal, addCall);
        for (const auto& branch : clause->fallbackBranches) {
            for (const auto& goal : branch) visitGoal(goal, addCall);
        }
    }

    for (const auto& global : program.globals) {
        visitExpr(global->expr, [&](const std::string& name) {
            const std::string kind = classify(name);
            graph.node(nodeId(kind, name), name, kind);
            graph.edge(nodeId("global", global->name), nodeId(kind, name), "references");
        });
    }

    std::ostringstream out;
    out << "{\"nodes\":[" << graph.nodes.str()
        << "],\"edges\":[" << graph.edges.str() << "]}";
    return out.str();
}

std::string graphJsonEnvelope(const std::string& json) {
    return "FELIDAE_GRAPH_BEGIN\n" + json + "\nFELIDAE_GRAPH_END\n";
}

std::string standaloneHtml(const std::string& json) {
    std::ostringstream out;
    out << "<!doctype html>\n<html lang=\"en\">\n<head>\n"
        << "<meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
        << "<title>Celidae Fact Graph</title>\n<style>\n"
        << ":root{font-family:Inter,Segoe UI,Arial,sans-serif;color:#d9fffb;background:#071110}"
        << "body{margin:0;min-height:100vh;display:grid;grid-template-columns:minmax(240px,320px) 1fr}"
        << "aside{padding:18px;border-right:1px solid #1d4742;background:#0a1715;overflow:auto}"
        << "main{min-width:0;overflow:auto;background:#071110}"
        << "h1{font-size:20px;margin:0 0 8px;color:#18f0d7}h2{font-size:12px;margin:20px 0 8px;color:#88aaa6}"
        << "p{color:#88aaa6;font-size:12px;line-height:1.45}input{box-sizing:border-box;width:100%;padding:9px;border:1px solid #28665e;border-radius:5px;background:#071110;color:#d9fffb}"
        << ".metric{display:flex;justify-content:space-between;padding:8px 0;border-bottom:1px solid #173430}"
        << ".pill{display:inline-block;margin:3px;padding:4px 7px;border:1px solid #28665e;border-radius:4px}"
        << "svg{display:block;min-width:900px;width:100%;height:100vh}.edge{stroke:#376760;stroke-width:1.2;marker-end:url(#arrow)}"
        << ".edge-label{fill:#7fa39e;font-size:10px}.node rect{stroke-width:1.4;rx:7}.node text{fill:#d9fffb;font-size:12px;pointer-events:none}.node.dim{opacity:.12}"
        << "@media(max-width:760px){body{grid-template-columns:1fr;grid-template-rows:auto 1fr}"
        << "aside{max-height:34vh;border-right:0;border-bottom:1px solid #1d4742}svg{height:66vh}}\n"
        << "</style></head><body>\n"
        << "<aside><h1>Celidae Fact & Dependency View</h1><p>Facts and fields form the schema layer; methods, globals and libraries form the execution layer.</p><input id=\"search\" type=\"search\" placeholder=\"Filter nodes\" aria-label=\"Filter graph nodes\"><div id=\"metrics\"></div>"
        << "<h2>Node Types</h2><div id=\"types\"></div></aside>"
        << "<main><svg id=\"graph\" role=\"img\" aria-label=\"Felidae fact relationship graph\"></svg></main>\n"
        << "<script type=\"application/json\" id=\"celidae-data\">" << scriptSafeJson(json) << "</script>\n"
        << "<script>"
        << "const d=JSON.parse(document.getElementById('celidae-data').textContent),n=d.nodes||[],e=d.edges||[];"
        << "const count=n.reduce((m,x)=>(m[x.kind]=(m[x.kind]||0)+1,m),{});"
        << "document.getElementById('metrics').innerHTML='<div class=\"metric\"><span>Nodes</span><strong>'+n.length+'</strong></div><div class=\"metric\"><span>Relationships</span><strong>'+e.length+'</strong></div>';"
        << "document.getElementById('types').innerHTML=Object.entries(count).map(([k,v])=>'<span class=\"pill\">'+k+' '+v+'</span>').join('');"
        << "const svg=document.getElementById('graph'),w=1200,row=72,kinds=['fact','field','method','global','library'],by=new Map(n.map(x=>[x.id,x]));"
        << "const cols=new Map(kinds.map((k,i)=>[k,i]));const lanes=new Map;for(const x of n){const k=x.kind||'library',a=lanes.get(k)||[];a.push(x);lanes.set(k,a)}"
        << "const maxRows=Math.max(1,...[...lanes.values()].map(x=>x.length)),h=Math.max(760,100+maxRows*row);svg.setAttribute('viewBox','0 0 '+w+' '+h);"
        << "const ns='http://www.w3.org/2000/svg',defs=document.createElementNS(ns,'defs'),marker=document.createElementNS(ns,'marker'),path=document.createElementNS(ns,'path');marker.setAttribute('id','arrow');marker.setAttribute('viewBox','0 0 10 10');marker.setAttribute('refX','9');marker.setAttribute('refY','5');marker.setAttribute('markerWidth','6');marker.setAttribute('markerHeight','6');marker.setAttribute('orient','auto-start-reverse');path.setAttribute('d','M 0 0 L 10 5 L 0 10 z');path.setAttribute('fill','#376760');marker.append(path);defs.append(marker);svg.append(defs);"
        << "for(const [kind,a] of lanes){const col=cols.has(kind)?cols.get(kind):4;a.forEach((x,i)=>{x.x=70+col*225;x.y=55+i*row})}"
        << "e.forEach((x,i)=>{const a=by.get(x.from),b=by.get(x.to);if(!a||!b)return;const l=document.createElementNS(ns,'line');for(const [k,v] of Object.entries({x1:a.x+150,y1:a.y+20,x2:b.x,y2:b.y+20}))l.setAttribute(k,v);l.setAttribute('class','edge');svg.appendChild(l);const t=document.createElementNS(ns,'text');t.setAttribute('class','edge-label');t.setAttribute('x',(a.x+b.x+150)/2);t.setAttribute('y',(a.y+b.y+40)/2-4);t.textContent=x.label||'';svg.appendChild(t)});"
        << "const c={fact:'#18f0d7',field:'#f7c948',method:'#6ca8ff',global:'#f27d9d',library:'#9ca3af'};"
        << "n.forEach(x=>{const g=document.createElementNS(ns,'g'),q=document.createElementNS(ns,'rect'),t=document.createElementNS(ns,'text'),u=document.createElementNS(ns,'title');g.setAttribute('class','node');g.dataset.text=(x.label+' '+(x.detail||'')).toLowerCase();q.setAttribute('x',x.x);q.setAttribute('y',x.y);q.setAttribute('width',150);q.setAttribute('height',40);q.setAttribute('fill',c[x.kind]||'#9ca3af');q.setAttribute('fill-opacity','.22');q.setAttribute('stroke',c[x.kind]||'#9ca3af');t.setAttribute('x',x.x+10);t.setAttribute('y',x.y+24);t.textContent=x.label.length>19?x.label.slice(0,18)+'…':x.label;u.textContent=(x.kind+': '+x.label)+(x.detail?' — '+x.detail:'');g.append(q,t,u);svg.appendChild(g)});"
        << "document.getElementById('search').addEventListener('input',ev=>{const q=ev.target.value.trim().toLowerCase();document.querySelectorAll('.node').forEach(x=>x.classList.toggle('dim',q&&!x.dataset.text.includes(q))) });"
        << "</script></body></html>\n";
    return out.str();
}

} // namespace Felidae::Celidae
