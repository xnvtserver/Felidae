import ("gtk").

Node(name: "Interpreter", concentration: 18, x: 150, y: 135).
Node(name: "FactMemory", concentration: 12, x: 390, y: 120).
Node(name: "NativeDll", concentration: 9, x: 600, y: 240).
Node(name: "Renderer", concentration: 15, x: 360, y: 360).

Edge(from: "Interpreter", to: "FactMemory", label: "indexes").
Edge(from: "FactMemory", to: "NativeDll", label: "loads").
Edge(from: "NativeDll", to: "Renderer", label: "draws").
Edge(from: "Renderer", to: "Interpreter", label: "returns").

DrawNode(node: any) =>
    return (gtk.circle(cx: node.x, cy: node.y, radius: node.concentration, name: node.name, fill: "#18f0d7")).

DrawEdge(edge: any) =>
    edge.from == "Interpreter",
    return (gtk.line(x1: 300, y1: 165, x2: 390, y2: 152, name: edge.label, stroke: "#08363a", width: 3))
else
    edge.from == "FactMemory",
    return (gtk.line(x1: 540, y1: 152, x2: 600, y2: 240, name: edge.label, stroke: "#08363a", width: 3))
else
    edge.from == "NativeDll",
    return (gtk.line(x1: 600, y1: 240, x2: 360, y2: 360, name: edge.label, stroke: "#08363a", width: 3))
else
    return (gtk.line(x1: 360, y1: 360, x2: 225, y2: 199, name: edge.label, stroke: "#08363a", width: 3)).

main() =>
    nodeShapes := lambda(Node, node => DrawNode(node: node)),
    edgeShapes := lambda(Edge, edge => DrawEdge(edge: edge)),
    canvas := gtk.canvas(width: 760, height: 520, title: "GTK Complex Fact Shape Regression"),
    title := gtk.H1(content: "Complex fact-shaped renderer"),
    intro := gtk.P(content: "Shapes and graph edges are generated from Felidae facts."),
    render := gtk.render(canvas: canvas, elements: [title, intro, edgeShapes, nodeShapes]),
    saved := gtk.save(path: "examples/data/gtk_complex_shapes_graph.html", render: render),

    return (
        title: title,
        intro: intro,
        nodes: nodeShapes,
        edges: edgeShapes,
        render: render,
        saved: saved
    ).
