# Native GTK graphics declaration layer.
# User code should import "gtk" and call gtk.* methods.

system.flibrary.gtk.canvas(width: number, height: number, title: string) => ().
system.flibrary.gtk.circle(cx: number, cy: number, radius: number, name: string, fill: string) => ().
system.flibrary.gtk.rect(x: number, y: number, width: number, height: number, name: string, fill: string) => ().
system.flibrary.gtk.line(x1: number, y1: number, x2: number, y2: number, name: string, stroke: string, width: number) => ().
system.flibrary.gtk.sector(cx: number, cy: number, radius: number, start: number, end: number, name: string, fill: string, weight: number) => ().
system.flibrary.gtk.text(x: number, y: number, content: string, size: number) => ().
system.flibrary.gtk.p(content: string) => ().
system.flibrary.gtk.h1(content: string) => ().
system.flibrary.gtk.button(id: string, label: string, value: string) => ().
system.flibrary.gtk.radio(id: string, label: string, value: string) => ().
system.flibrary.gtk.checkbox(id: string, label: string, value: string) => ().
system.flibrary.gtk.button_at(id: string, label: string, value: string, x: number, y: number, width: number, height: number) => ().
system.flibrary.gtk.radio_at(id: string, label: string, value: string, x: number, y: number, width: number, height: number) => ().
system.flibrary.gtk.checkbox_at(id: string, label: string, value: string, x: number, y: number, width: number, height: number) => ().
system.flibrary.gtk.pie_from_facts(facts: array, label: string, value: string, cx: number, cy: number, radius: number) => ().
system.flibrary.gtk.pie_from_fact_type(type: string, label: string, value: string, cx: number, cy: number, radius: number) => ().
system.flibrary.gtk.graph_from_facts(edges: array, from: string, to: string, label: string, cx: number, cy: number, radius: number) => ().
system.flibrary.gtk.graph_from_fact_type(type: string, from: string, to: string, label: string, cx: number, cy: number, radius: number) => ().
system.flibrary.gtk.plot(plot: any, x: number, y: number, width: number, height: number) => ().
system.flibrary.gtk.render(canvas: any, elements: array) => ().
