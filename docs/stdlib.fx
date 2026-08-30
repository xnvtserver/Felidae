import ("html_components.fx", "str").

DocsStdlib() =>
    cards := str.join(data: [
        RenderMiniCard(input: HtmlCardData(title: "str", text: "Text projection and matching.")),
        RenderMiniCard(input: HtmlCardData(title: "array", text: "Ordered values and small list helpers.")),
        RenderMiniCard(input: HtmlCardData(title: "json", text: "JSON boundary values.")),
        RenderMiniCard(input: HtmlCardData(title: "csv", text: "CSV rows and fact generation.")),
        RenderMiniCard(input: HtmlCardData(title: "file", text: "Local file I/O.")),
        RenderMiniCard(input: HtmlCardData(title: "math", text: "Scalar numeric helpers.")),
        RenderMiniCard(input: HtmlCardData(title: "system", text: "Runtime print and type inspection.")),
        RenderMiniCard(input: HtmlCardData(title: "console", text: "Interactive terminal I/O.")),
        RenderMiniCard(input: HtmlCardData(title: "thread", text: "Thread handles for method execution."))
    ], delimiter: ""),
    content := RenderModuleGrid(content: cards),
    return (HtmlRichSectionData(id: "stdlib", title: "Standard Library", p: "The Felidae standard library is a compact set of explicit modules. Each core/*.fx file is the public contract. Deterministic operations execute as verified Builtin IR calls in RegisterVm; external services must be supplied explicitly by a runtime.", p2: "This page is only the stable overview. Open one module page at a time for focused signatures, examples, and cautions. That keeps json separate from csv, file separate from math, and str separate from array.", content: content, code: "import (\"str\", \"json\").\n\nCustomer(name: \"\").\n\nmain() =>\n    rows := Customer.all(),\n    names := lambda(rows, row => row.name),\n    label := str.join(data: names, delimiter: \", \"),\n    return (count: count(rows), names: label, json: json.toText(data: names)).", note: "Read module pages when choosing an API. The overview should stay stable and short; module-specific risks belong near that module.", code2: "Core files:\ncore/str.fx\ncore/array.fx\ncore/json.fx\ncore/csv.fx\ncore/file.fx\ncore/math.fx\ncore/system.fx\ncore/console.fx\ncore/thread.fx")).
