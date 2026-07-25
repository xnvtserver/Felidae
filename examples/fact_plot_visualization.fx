import ("plot", "gtk", "qt")

AnimalMetric(name: "Goat", month: "Jan", weight: 54, milk: 7, wool: 1, horns: 2)
AnimalMetric(name: "BlackSheep", month: "Feb", weight: 65, milk: 4, wool: 8, horns: 0)
AnimalMetric(name: "Wolf", month: "Mar", weight: 47, milk: 0, wool: 0, horns: 0)

FactType(type: "Animal", parent: "LivingThing")
FactType(type: "Mammal", parent: "Animal")
FactType(type: "Ruminant", parent: "Mammal")
FactType(type: "Canine", parent: "Mammal")
FactType(type: "Goat", parent: "Ruminant")
FactType(type: "BlackSheep", parent: "Ruminant")
FactType(type: "Wolf", parent: "Canine")

main() =>
    animalFacts := lambda(AnimalMetric, fact => {
        __type: "AnimalMetric",
        name: fact.name,
        month: fact.month,
        weight: fact.weight,
        milk: fact.milk,
        wool: fact.wool,
        horns: fact.horns
    }),
    graphFacts := lambda(FactType, fact => {
        __type: fact.type,
        __parent: fact.parent
    }),

    scatter := plot.scatter(facts: animalFacts, x: "weight", y: "milk", label: "name", title: "Animal Weight vs Milk"),
    bars := plot.bar(facts: animalFacts, category: "name", value: "wool", title: "Wool by Animal"),
    pie := plot.pie(facts: animalFacts, category: "name", value: "weight", title: "Weight Share"),
    histogram := plot.histogram(facts: animalFacts, value: "weight", bins: 3, title: "Weight Histogram"),
    timeSeries := plot.timeSeries(facts: animalFacts, time: "month", value: "milk", title: "Milk Over Time"),
    relationships := plot.relationships(facts: graphFacts, title: "Animal Fact Inheritance"),

    controls := [
        plot.button(id: "bar", label: "Bar", value: "bar"),
        plot.button(id: "pie", label: "Pie", value: "pie"),
        plot.radio(id: "histogram", label: "Histogram", value: "histogram"),
        plot.checkbox(id: "time", label: "Time Series", value: "time_series"),
        plot.button(id: "graph", label: "Relationships", value: "relationships")
    ],
    dashboard := plot.dashboard(plots: [bars, pie, histogram, timeSeries, relationships], controls: controls, title: "Fact Visualization Dashboard"),

    gtkCanvas := gtk.canvas(width: 820, height: 560, title: "GTK Plot Experiment Canvas"),
    gtkTitle := gtk.H1(content: "Fact plot experiment"),
    gtkNote := gtk.P(content: "Plot builds analysis graphics, GTK hosts controls and visual experiments."),
    gtkPlot := gtk.plot(plot: scatter, x: 48, y: 150, width: 700, height: 360),
    gtkBarMode := gtk.buttonAt(id: "gtkBar", label: "Bar", value: "bar", x: 48, y: 104, width: 92, height: 34),
    gtkTrendMode := gtk.radioAt(id: "gtkTrend", label: "Trend", value: "scatter", x: 156, y: 108, width: 120, height: 30),
    gtkRender := gtk.render(canvas: gtkCanvas, elements: [gtkTitle, gtkNote, gtkBarMode, gtkTrendMode, gtkPlot]),

    qtCanvas := qt.canvas(width: 820, height: 560, title: "Qt Plot Experiment Canvas"),
    qtTitle := qt.H1(content: "Relationship visualization"),
    qtNote := qt.P(content: "Qt can host plot output beside custom logic controls."),
    qtPlot := plot.toQtElement(plot: relationships, x: 48, y: 150, width: 700, height: 360),
    qtGraphMode := qt.checkboxAt(id: "qtGraph", label: "Show Graph", value: "relationships", x: 48, y: 108, width: 140, height: 30),
    qtRender := qt.render(canvas: qtCanvas, elements: [qtTitle, qtNote, qtGraphMode, qtPlot]),

    scatterSaved := plot.saveHtml(path: "examples/data/fact_scatter_plot.html", plot: scatter, title: "Animal Weight vs Milk"),
    barSaved := plot.saveSvg(path: "examples/data/fact_wool_bar.svg", plot: bars),
    graphSaved := plot.saveHtml(path: "examples/data/fact_relationship_graph.html", plot: relationships, title: "Animal Fact Inheritance"),
    dashboardSaved := plot.saveDashboard(path: "examples/data/fact_visualization_dashboard.html", dashboard: dashboard),
    gtkSaved := gtk.save(path: "examples/data/gtk_plot_experiment.html", render: gtkRender),
    qtSaved := qt.save(path: "examples/data/qt_plot_experiment.html", render: qtRender),

    return (
        animalFacts: animalFacts,
        scatter: scatter,
        bars: bars,
        pie: pie,
        histogram: histogram,
        timeSeries: timeSeries,
        relationships: relationships,
        controls: controls,
        dashboard: dashboard,
        gtkPlot: gtkPlot,
        gtkControls: [gtkBarMode, gtkTrendMode],
        gtkRender: gtkRender,
        qtPlot: qtPlot,
        qtControls: [qtGraphMode],
        qtRender: qtRender,
        scatterSaved: scatterSaved,
        barSaved: barSaved,
        graphSaved: graphSaved,
        dashboardSaved: dashboardSaved,
        gtkSaved: gtkSaved,
        qtSaved: qtSaved
    )