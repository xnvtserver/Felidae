# Programmatic fact visualization library.
# Plot methods return SVG/HTML strings as data so Felidae programs can save,
# serve, test, or transform visualizations like ordinary facts.

import ("flibrary", "file", "system.flibrary.plot")

plot.scatter(facts: array, x: string, y: string, label: string, title: string) =>
    return (system_library_loader(module: "plot", function: "scatter", args: {facts: facts, x: x, y: y, label: label, title: title}))

plot.bar(facts: array, category: string, value: string, title: string) =>
    return (system_library_loader(module: "plot", function: "bar", args: {facts: facts, category: category, value: value, title: title}))

plot.pie(facts: array, category: string, value: string, title: string) =>
    return (system_library_loader(module: "plot", function: "pie", args: {facts: facts, category: category, value: value, title: title}))

plot.histogram(facts: array, value: string, bins: number, title: string) =>
    return (system_library_loader(module: "plot", function: "histogram", args: {facts: facts, value: value, bins: bins, title: title}))

plot.timeSeries(facts: array, time: string, value: string, title: string) =>
    return (system_library_loader(module: "plot", function: "time_series", args: {facts: facts, time: time, value: value, title: title}))

plot.relationships(facts: array, title: string) =>
    return (system_library_loader(module: "plot", function: "relationships", args: {facts: facts, title: title}))

plot.html(plot: any, title: string) =>
    return (system_library_loader(module: "plot", function: "html", args: {plot: plot, title: title}))

plot.button(id: string, label: string, value: string) =>
    return (system_library_loader(module: "plot", function: "button", args: {id: id, label: label, value: value}))

plot.radio(id: string, label: string, value: string) =>
    return (system_library_loader(module: "plot", function: "radio", args: {id: id, label: label, value: value}))

plot.checkbox(id: string, label: string, value: string) =>
    return (system_library_loader(module: "plot", function: "checkbox", args: {id: id, label: label, value: value}))


plot.toGtkElement(plot: any, x: number, y: number, width: number, height: number) =>
    return ({__type: "gtk.plot", module: "gtk", kind: "plot", title: plot.title, svg: plot.svg, x: x, y: y, width: width, height: height})

plot.toQtElement(plot: any, x: number, y: number, width: number, height: number) =>
    return ({__type: "qt.plot", module: "qt", kind: "plot", title: plot.title, svg: plot.svg, x: x, y: y, width: width, height: height})
plot.dashboard(plots: array, controls: array, title: string) =>
    return (system_library_loader(module: "plot", function: "dashboard", args: {plots: plots, controls: controls, title: title}))

plot.saveSvg(path: string, plot: any) =>
    return (file.writeFile(path: path, data: plot.svg, mode: "write"))

plot.saveHtml(path: string, plot: any, title: string) =>
    page := plot.html(plot: plot, title: title),
    return (file.writeFile(path: path, data: page.html, mode: "write"))

plot.saveDashboard(path: string, dashboard: any) =>
    return (file.writeFile(path: path, data: dashboard.html, mode: "write"))
