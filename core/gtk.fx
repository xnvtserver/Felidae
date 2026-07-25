# GTK-compatible graphics API. The current backend produces portable SVG/HTML;
# query gtk.backend() before assuming native widget-toolkit behavior.

import ("flibrary", "file", "system.flibrary.gtk")

gtk.backend() =>
    return (system_library_loader(module: "gtk", function: "backend", args: {}))

gtk.canvas(width: number, height: number, title: string) =>
    return (system_library_loader(module: "gtk", function: "canvas", args: {width: width, height: height, title: title}))

gtk.circle(cx: number, cy: number, radius: number, name: string, fill: string) =>
    return (system_library_loader(module: "gtk", function: "circle", args: {cx: cx, cy: cy, radius: radius, name: name, fill: fill}))

gtk.rect(x: number, y: number, width: number, height: number, name: string, fill: string) =>
    return (system_library_loader(module: "gtk", function: "rect", args: {x: x, y: y, width: width, height: height, name: name, fill: fill}))

gtk.line(x1: number, y1: number, x2: number, y2: number, name: string, stroke: string, width: number) =>
    return (system_library_loader(module: "gtk", function: "line", args: {x1: x1, y1: y1, x2: x2, y2: y2, name: name, stroke: stroke, width: width}))

gtk.sector(cx: number, cy: number, radius: number, start: number, end: number, name: string, fill: string, weight: number) =>
    return (system_library_loader(module: "gtk", function: "sector", args: {cx: cx, cy: cy, radius: radius, start: start, end: end, name: name, fill: fill, weight: weight}))

gtk.text(x: number, y: number, content: string, size: number) =>
    return (system_library_loader(module: "gtk", function: "text", args: {x: x, y: y, content: content, size: size}))

gtk.p(content: string) =>
    return (system_library_loader(module: "gtk", function: "p", args: {content: content}))

gtk.P(content: string) =>
    return (gtk.p(content: content))

gtk.h1(content: string) =>
    return (system_library_loader(module: "gtk", function: "h1", args: {content: content}))

gtk.H1(content: string) =>
    return (gtk.h1(content: content))

gtk.button(id: string, label: string, value: string) =>
    return (system_library_loader(module: "gtk", function: "button", args: {id: id, label: label, value: value}))

gtk.radio(id: string, label: string, value: string) =>
    return (system_library_loader(module: "gtk", function: "radio", args: {id: id, label: label, value: value}))

gtk.checkbox(id: string, label: string, value: string) =>
    return (system_library_loader(module: "gtk", function: "checkbox", args: {id: id, label: label, value: value}))


gtk.buttonAt(id: string, label: string, value: string, x: number, y: number, width: number, height: number) =>
    return (system_library_loader(module: "gtk", function: "button_at", args: {id: id, label: label, value: value, x: x, y: y, width: width, height: height}))

gtk.radioAt(id: string, label: string, value: string, x: number, y: number, width: number, height: number) =>
    return (system_library_loader(module: "gtk", function: "radio_at", args: {id: id, label: label, value: value, x: x, y: y, width: width, height: height}))

gtk.checkboxAt(id: string, label: string, value: string, x: number, y: number, width: number, height: number) =>
    return (system_library_loader(module: "gtk", function: "checkbox_at", args: {id: id, label: label, value: value, x: x, y: y, width: width, height: height}))
gtk.pieFromFacts(facts: array, label: string, value: string, cx: number, cy: number, radius: number) =>
    return (system_library_loader(module: "gtk", function: "pie_from_facts", args: {facts: facts, label: label, value: value, cx: cx, cy: cy, radius: radius}))

gtk.pieFromFactType(type: string, label: string, value: string, cx: number, cy: number, radius: number) =>
    return (system_library_loader(module: "gtk", function: "pie_from_fact_type", args: {type: type, label: label, value: value, cx: cx, cy: cy, radius: radius}))

gtk.graphFromFacts(edges: array, from: string, to: string, label: string, cx: number, cy: number, radius: number) =>
    return (system_library_loader(module: "gtk", function: "graph_from_facts", args: {edges: edges, from: from, to: to, label: label, cx: cx, cy: cy, radius: radius}))

gtk.graphFromFactType(type: string, from: string, to: string, label: string, cx: number, cy: number, radius: number) =>
    return (system_library_loader(module: "gtk", function: "graph_from_fact_type", args: {type: type, from: from, to: to, label: label, cx: cx, cy: cy, radius: radius}))


gtk.plot(plot: any, x: number, y: number, width: number, height: number) =>
    return (system_library_loader(module: "gtk", function: "plot", args: {plot: plot, x: x, y: y, width: width, height: height}))
gtk.render(canvas: any, elements: array) =>
    return (system_library_loader(module: "gtk", function: "render", args: {canvas: canvas, elements: elements}))

gtk.save(path: string, render: any) =>
    return (file.writeFile(path: path, data: render.html, mode: "write"))
