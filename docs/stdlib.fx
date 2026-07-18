import ("html_components.fx", "str").

DocsStdlib() =>
    cards := str.join(data: [
        RenderMiniCard(input: HtmlCardData(title: "str", text: "Text projection and matching.")),
        RenderMiniCard(input: HtmlCardData(title: "array", text: "Ordered values and small list helpers.")),
        RenderMiniCard(input: HtmlCardData(title: "db", text: "Fact database access.")),
        RenderMiniCard(input: HtmlCardData(title: "json", text: "JSON boundary values.")),
        RenderMiniCard(input: HtmlCardData(title: "csv", text: "CSV rows and fact generation.")),
        RenderMiniCard(input: HtmlCardData(title: "file", text: "Local file I/O.")),
        RenderMiniCard(input: HtmlCardData(title: "http", text: "HTTP calls and serving helpers.")),
        RenderMiniCard(input: HtmlCardData(title: "math", text: "Scalar numeric helpers.")),
        RenderMiniCard(input: HtmlCardData(title: "ml", text: "Small vector scoring helpers.")),
        RenderMiniCard(input: HtmlCardData(title: "probability", text: "Statistics and distributions over arrays.")),
        RenderMiniCard(input: HtmlCardData(title: "system", text: "Runtime print and type inspection.")),
        RenderMiniCard(input: HtmlCardData(title: "console", text: "Interactive terminal I/O.")),
        RenderMiniCard(input: HtmlCardData(title: "process", text: "Host process and platform helpers.")),
        RenderMiniCard(input: HtmlCardData(title: "thread", text: "Thread handles for method execution."))
    ], delimiter: ""),
    content := RenderModuleGrid(content: cards),
    return (HtmlRichSectionData(id: "stdlib", title: "Standard Library", p: "The Felidae standard library is a compact set of explicit modules. Each core/*.fx file is the public contract: it declares callable names and argument shapes, while native C++ implementations handle heavy I/O, networking, numeric work, or runtime services where needed.", p2: "This page is only the stable overview. Open one module page at a time for focused signatures, examples, and cautions. That keeps json separate from csv, file separate from http, and math separate from ml or probability.", content: content, code: "import (\"str\", \"db\", \"json\").\n\nmain() =>\n    rows := db.all(type: \"Customer\"),\n    names := lambda(rows, row => row.name),\n    label := str.join(data: names, delimiter: \", \"),\n    return (count: count(rows), names: label, json: json.toText(data: names)).", note: "Read module pages when choosing an API. The overview should stay stable and short; module-specific risks belong near that module.", code2: "Core files:\ncore/str.fx\ncore/array.fx\ncore/db.fx\ncore/json.fx\ncore/csv.fx\ncore/file.fx\ncore/http.fx\ncore/math.fx\ncore/ml.fx\ncore/probability.fx\ncore/system.fx\ncore/console.fx\ncore/process.fx\ncore/thread.fx")).
