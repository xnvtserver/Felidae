# Native fact plotting declarations.
# User code should import "plot" and call plot.* methods.

system.flibrary.plot.scatter(facts: array, x: string, y: string, label: string, title: string) => ().
system.flibrary.plot.bar(facts: array, category: string, value: string, title: string) => ().
system.flibrary.plot.pie(facts: array, category: string, value: string, title: string) => ().
system.flibrary.plot.histogram(facts: array, value: string, bins: number, title: string) => ().
system.flibrary.plot.time_series(facts: array, time: string, value: string, title: string) => ().
system.flibrary.plot.relationships(facts: array, title: string) => ().
system.flibrary.plot.html(plot: any, title: string) => ().
system.flibrary.plot.button(id: string, label: string, value: string) => ().
system.flibrary.plot.radio(id: string, label: string, value: string) => ().
system.flibrary.plot.checkbox(id: string, label: string, value: string) => ().
system.flibrary.plot.dashboard(plots: array, controls: array, title: string) => ().
