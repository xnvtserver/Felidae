#include "Visualization.h"

#include <sstream>

namespace Felidae {

std::string graphJsonEnvelope(const std::string& graphJson) {
    return "FELIDAE_GRAPH_BEGIN\n" + graphJson + "\nFELIDAE_GRAPH_END\n";
}

static std::string scriptSafeJson(std::string json) {
    for (size_t pos = json.find("</"); pos != std::string::npos; pos = json.find("</", pos + 3)) {
        json.replace(pos, 2, "<\\/");
    }
    return json;
}

std::string standaloneDataVisualizationHtml(const std::string& graphJson) {
    std::ostringstream out;
    out << "<!doctype html>\n"
        << "<html lang=\"en\">\n"
        << "<head>\n"
        << "<meta charset=\"utf-8\">\n"
        << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
        << "<title>Celidae Data Visualization</title>\n"
        << "<style>\n"
        << ":root{color-scheme:light dark;font-family:Inter,Segoe UI,Arial,sans-serif;background:#f8fafc;color:#172033}\n"
        << "body{margin:0;min-height:100vh;display:grid;grid-template-columns:minmax(260px,340px) 1fr;background:#f8fafc;color:#172033}\n"
        << "aside{border-right:1px solid #d8dee9;background:#ffffff;padding:18px;overflow:auto}\n"
        << "main{position:relative;overflow:hidden;background-image:linear-gradient(#e3e8f1 1px,transparent 1px),linear-gradient(90deg,#e3e8f1 1px,transparent 1px);background-size:24px 24px}\n"
        << "h1{font-size:20px;margin:0 0 8px} h2{font-size:13px;margin:22px 0 8px;text-transform:uppercase;color:#5d687a;letter-spacing:.04em}\n"
        << ".metric{display:flex;justify-content:space-between;border-bottom:1px solid #edf0f5;padding:8px 0;font-size:14px}\n"
        << ".pill{display:inline-flex;gap:6px;align-items:center;border:1px solid #ccd5e1;border-radius:999px;padding:4px 8px;margin:4px 4px 0 0;font-size:12px;background:#fff}\n"
        << "svg{width:100%;height:100vh;display:block} line{stroke:#9aa7b8;stroke-width:1.4} circle{stroke:#172033;stroke-width:1.3} text{font-size:12px;fill:#172033;paint-order:stroke;stroke:#fff;stroke-width:3px;stroke-linejoin:round}\n"
        << "@media(max-width:760px){body{grid-template-columns:1fr;grid-template-rows:auto 1fr}aside{max-height:42vh;border-right:0;border-bottom:1px solid #d8dee9}svg{height:58vh}}\n"
        << "</style>\n"
        << "</head>\n"
        << "<body>\n"
        << "<aside><h1>Celidae Data Visualization</h1><div id=\"metrics\"></div><h2>Node Types</h2><div id=\"types\"></div><h2>Data Quality</h2><div id=\"quality\"></div></aside>\n"
        << "<main><svg id=\"graph\" role=\"img\" aria-label=\"Celidae runtime data graph\"></svg></main>\n"
        << "<script type=\"application/json\" id=\"celidae-data\">" << scriptSafeJson(graphJson) << "</script>\n"
        << "<script>\n"
        << "const data=JSON.parse(document.getElementById('celidae-data').textContent);"
        << "const nodes=data.nodes||[],edges=data.edges||[],total=Math.max(nodes.length,1);"
        << "const metricEl=document.getElementById('metrics'),typesEl=document.getElementById('types'),qualityEl=document.getElementById('quality');"
        << "const byId=new Map(nodes.map(n=>[n.id,n]));"
        << "const counts=nodes.reduce((m,n)=>(m[n.kind]=(m[n.kind]||0)+1,m),{});"
        << "const degree=new Map();edges.forEach(e=>{degree.set(e.from,(degree.get(e.from)||0)+1);degree.set(e.to,(degree.get(e.to)||0)+1)});"
        << "const sparse=nodes.filter(n=>n.kind==='fact'&&/records=0/.test(n.detail||'')).length;"
        << "const isolated=nodes.filter(n=>!degree.has(n.id)).length;"
        << "metricEl.innerHTML=['Nodes: '+nodes.length,'Edges: '+edges.length,'Isolated: '+isolated,'Sparse facts: '+sparse].map(x=>'<div class=\"metric\"><span>'+x.split(': ')[0]+'</span><strong>'+x.split(': ')[1]+'</strong></div>').join('');"
        << "typesEl.innerHTML=Object.entries(counts).map(([k,v])=>'<span class=\"pill\">'+k+' '+v+'</span>').join('')||'No nodes';"
        << "qualityEl.innerHTML=(sparse?'<div class=\"metric\"><span>Referenced empty facts</span><strong>'+sparse+'</strong></div>':'')+(isolated?'<div class=\"metric\"><span>Disconnected nodes</span><strong>'+isolated+'</strong></div>':'<div class=\"metric\"><span>Disconnected nodes</span><strong>0</strong></div>');"
        << "const svg=document.getElementById('graph'),w=1200,h=760,cx=w/2,cy=h/2;svg.setAttribute('viewBox','0 0 '+w+' '+h);"
        << "nodes.forEach((n,i)=>{const a=(i/total)*Math.PI*2,r=260+(i%5)*34;n.x=cx+Math.cos(a)*r;n.y=cy+Math.sin(a)*r});"
        << "const color={fact:'#14b8a6',field:'#f59e0b',method:'#6366f1',global:'#ef4444',library:'#64748b'};"
        << "edges.forEach(e=>{const a=byId.get(e.from),b=byId.get(e.to);if(!a||!b)return;const l=document.createElementNS('http://www.w3.org/2000/svg','line');l.setAttribute('x1',a.x);l.setAttribute('y1',a.y);l.setAttribute('x2',b.x);l.setAttribute('y2',b.y);svg.appendChild(l)});"
        << "nodes.forEach(n=>{const g=document.createElementNS('http://www.w3.org/2000/svg','g');const c=document.createElementNS('http://www.w3.org/2000/svg','circle');c.setAttribute('cx',n.x);c.setAttribute('cy',n.y);c.setAttribute('r',n.kind==='fact'?18:13);c.setAttribute('fill',color[n.kind]||'#94a3b8');const t=document.createElementNS('http://www.w3.org/2000/svg','text');t.setAttribute('x',n.x+18);t.setAttribute('y',n.y+4);t.textContent=n.detail?n.label+' - '+n.detail:n.label;g.append(c,t);svg.appendChild(g)});"
        << "</script>\n"
        << "</body>\n"
        << "</html>\n";
    return out.str();
}

} // namespace Felidae
