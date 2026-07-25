import ("qt")

Relation(from: "Felidae", to: "Facts", label: "stores")
Relation(from: "Facts", to: "Rules", label: "query")
Relation(from: "Rules", to: "NativeDll", label: "invoke")
Relation(from: "NativeDll", to: "QtRenderer", label: "render")
Relation(from: "QtRenderer", to: "Felidae", label: "result")

main() =>
    relations := lambda(Relation, relation => {
        __type: "Relation",
        from: relation.from,
        to: relation.to,
        label: relation.label
    })
    canvas := qt.canvas(width: 820, height: 560, title: "Qt Fact Relationship Graph")
    graph := qt.graphFromFacts(edges: relations, from: "from", to: "to", label: "label", cx: 410, cy: 285, radius: 185)
    heading := qt.H1(content: "Fact relationship graph")
    render := qt.render(canvas: canvas, elements: [heading, graph.elements])
    saved := qt.save(path: "examples/data/qt_fact_relationship_graph.html", render: render)

    return (
        heading: heading,
        graph: graph,
        render: render,
        saved: saved
    )
