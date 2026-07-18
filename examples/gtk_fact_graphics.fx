import ("gtk").

FactConcentration(name: "North", concentration: 12).
FactConcentration(name: "South", concentration: 8).
FactConcentration(name: "East", concentration: 5).
FactConcentration(name: "West", concentration: 3).

main() =>
    concentrations := lambda(FactConcentration, fact => {
        __type: "FactConcentration",
        name: fact.name,
        concentration: fact.concentration
    }),
    canvas := gtk.canvas(width: 720, height: 520, title: "GTK Fact Concentration"),
    pie := gtk.pieFromFacts(facts: concentrations, label: "name", value: "concentration", cx: 260, cy: 270, radius: 150),
    heading := gtk.H1(content: "Fact concentration by region"),
    paragraph := gtk.P(content: "Felidae code describes the canvas and the graphics layer executes the controls."),
    barButton := gtk.button(id: "bar", label: "Bar", value: "bar"),
    pieRadio := gtk.radio(id: "pie", label: "Pie", value: "pie"),
    histogramCheck := gtk.checkbox(id: "histogram", label: "Histogram", value: "histogram"),
    render := gtk.render(canvas: canvas, elements: [heading, paragraph, pie.elements]),
    saved := gtk.save(path: "examples/data/gtk_fact_graphics.html", render: render),

    return (
        heading: heading,
        paragraph: paragraph,
        controls: [barButton, pieRadio, histogramCheck],
        sectors: pie,
        render: render,
        saved: saved
    ).
