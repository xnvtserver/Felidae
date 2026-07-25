# Qt-compatible graphics API. The current backend produces portable SVG/HTML;
# query qt.backend() before assuming native widget-toolkit behavior.

import ("flibrary", "file", "system.flibrary.qt")

qt.backend() =>
    return (system_library_loader(module: "qt", function: "backend", args: {}))

qt.canvas(width: number, height: number, title: string) =>
    return (system_library_loader(module: "qt", function: "canvas", args: {width: width, height: height, title: title}))

qt.circle(cx: number, cy: number, radius: number, name: string, fill: string) =>
    return (system_library_loader(module: "qt", function: "circle", args: {cx: cx, cy: cy, radius: radius, name: name, fill: fill}))

qt.rect(x: number, y: number, width: number, height: number, name: string, fill: string) =>
    return (system_library_loader(module: "qt", function: "rect", args: {x: x, y: y, width: width, height: height, name: name, fill: fill}))

qt.line(x1: number, y1: number, x2: number, y2: number, name: string, stroke: string, width: number) =>
    return (system_library_loader(module: "qt", function: "line", args: {x1: x1, y1: y1, x2: x2, y2: y2, name: name, stroke: stroke, width: width}))

qt.sector(cx: number, cy: number, radius: number, start: number, end: number, name: string, fill: string, weight: number) =>
    return (system_library_loader(module: "qt", function: "sector", args: {cx: cx, cy: cy, radius: radius, start: start, end: end, name: name, fill: fill, weight: weight}))

qt.text(x: number, y: number, content: string, size: number) =>
    return (system_library_loader(module: "qt", function: "text", args: {x: x, y: y, content: content, size: size}))

qt.p(content: string) =>
    return (system_library_loader(module: "qt", function: "p", args: {content: content}))

qt.P(content: string) =>
    return (qt.p(content: content))

qt.h1(content: string) =>
    return (system_library_loader(module: "qt", function: "h1", args: {content: content}))

qt.H1(content: string) =>
    return (qt.h1(content: content))

qt.button(id: string, label: string, value: string) =>
    return (system_library_loader(module: "qt", function: "button", args: {id: id, label: label, value: value}))

qt.radio(id: string, label: string, value: string) =>
    return (system_library_loader(module: "qt", function: "radio", args: {id: id, label: label, value: value}))

qt.checkbox(id: string, label: string, value: string) =>
    return (system_library_loader(module: "qt", function: "checkbox", args: {id: id, label: label, value: value}))


qt.buttonAt(id: string, label: string, value: string, x: number, y: number, width: number, height: number) =>
    return (system_library_loader(module: "qt", function: "button_at", args: {id: id, label: label, value: value, x: x, y: y, width: width, height: height}))

qt.radioAt(id: string, label: string, value: string, x: number, y: number, width: number, height: number) =>
    return (system_library_loader(module: "qt", function: "radio_at", args: {id: id, label: label, value: value, x: x, y: y, width: width, height: height}))

qt.checkboxAt(id: string, label: string, value: string, x: number, y: number, width: number, height: number) =>
    return (system_library_loader(module: "qt", function: "checkbox_at", args: {id: id, label: label, value: value, x: x, y: y, width: width, height: height}))
qt.pieFromFacts(facts: array, label: string, value: string, cx: number, cy: number, radius: number) =>
    return (system_library_loader(module: "qt", function: "pie_from_facts", args: {facts: facts, label: label, value: value, cx: cx, cy: cy, radius: radius}))

qt.pieFromFactType(type: string, label: string, value: string, cx: number, cy: number, radius: number) =>
    return (system_library_loader(module: "qt", function: "pie_from_fact_type", args: {type: type, label: label, value: value, cx: cx, cy: cy, radius: radius}))

qt.graphFromFacts(edges: array, from: string, to: string, label: string, cx: number, cy: number, radius: number) =>
    return (system_library_loader(module: "qt", function: "graph_from_facts", args: {edges: edges, from: from, to: to, label: label, cx: cx, cy: cy, radius: radius}))

qt.graphFromFactType(type: string, from: string, to: string, label: string, cx: number, cy: number, radius: number) =>
    return (system_library_loader(module: "qt", function: "graph_from_fact_type", args: {type: type, from: from, to: to, label: label, cx: cx, cy: cy, radius: radius}))


qt.plot(plot: any, x: number, y: number, width: number, height: number) =>
    return (system_library_loader(module: "qt", function: "plot", args: {plot: plot, x: x, y: y, width: width, height: height}))
qt.render(canvas: any, elements: array) =>
    return (system_library_loader(module: "qt", function: "render", args: {canvas: canvas, elements: elements}))

qt.save(path: string, render: any) =>
    return (file.writeFile(path: path, data: render.html, mode: "write"))
