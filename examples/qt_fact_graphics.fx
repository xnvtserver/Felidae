import ("qt")

MetricFact(name: "CPU", concentration: 40)
MetricFact(name: "Memory", concentration: 30)
MetricFact(name: "Disk", concentration: 20)
MetricFact(name: "Network", concentration: 10)

main() =>
    metrics := lambda(MetricFact, fact => {
        __type: "MetricFact",
        name: fact.name,
        concentration: fact.concentration
    }),
    canvas := qt.canvas(width: 760, height: 520, title: "Qt Fact Rendering"),
    pie := qt.pieFromFacts(facts: metrics, label: "name", value: "concentration", cx: 280, cy: 270, radius: 155),
    heading := qt.H1(content: "Runtime fact renderer"),
    paragraph := qt.P(content: "Qt import keeps toolkit identity while Felidae stays the rendering language."),
    center := qt.circle(cx: 280, cy: 270, radius: 38, name: "center", fill: "#08363a"),
    render := qt.render(canvas: canvas, elements: [heading, paragraph, center]),
    pieRender := qt.render(canvas: canvas, elements: [heading, paragraph, pie.elements]),
    saved := qt.save(path: "examples/data/qt_fact_graphics.html", render: pieRender),

    return (
        heading: heading,
        paragraph: paragraph,
        center: center,
        sectors: pie,
        render: render,
        pieRender: pieRender,
        saved: saved
    )
