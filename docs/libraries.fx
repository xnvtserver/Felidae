import ("html_components.fx", "str").

LibraryLinkCard(title: string, text: string, href: string) =>
    intro := RenderParagraph(text: text),
    link := RenderAnchor(input: HtmlAnchorData(label: "Open", href: href, class: "tool-button")),
    return (RenderMiniCardContent(input: HtmlCardContentData(title: title, content: str.concat(left: intro, right: link)))).

DocsLibraries() =>
    overview := RenderParagraph(text: "Use this index when you know the module name or the kind of boundary you are working with. Every linked route focuses on one module only: one import, one API surface, one set of examples."),
    cards := str.join(data: [
        LibraryLinkCard(title: "str", text: "Text projection, matching, cleanup, splitting, joining, and replacement.", href: "#lib-str"),
        LibraryLinkCard(title: "array", text: "Ordered values: get, len, and push.", href: "#lib-array"),
        LibraryLinkCard(title: "json", text: "JSON parsing, key access, shape checks, updates, and serialization.", href: "#lib-json"),
        LibraryLinkCard(title: "csv", text: "CSV parsing, row updates, exports, and fact generation.", href: "#lib-csv"),
        LibraryLinkCard(title: "file", text: "Local file reads, writes, appends, existence checks, and deletes.", href: "#lib-file"),
        LibraryLinkCard(title: "math", text: "Scalar constants, numeric helpers, and trigonometry.", href: "#lib-math"),
        LibraryLinkCard(title: "system", text: "Runtime print and type inspection helpers.", href: "#lib-system"),
        LibraryLinkCard(title: "console", text: "Interactive terminal input and output.", href: "#lib-console"),
        LibraryLinkCard(title: "thread", text: "Independent method execution with thread handles.", href: "#lib-thread")
    ], delimiter: ""),
    content := str.concat(left: overview, right: RenderModuleGrid(content: cards)),
    return (HtmlRichSectionData(id: "libraries", title: "Library Index", p: "Felidae library documentation is organized one subject at a time. The index is only a chooser; the focused module page carries the signatures, examples, notes, and risks for that module.", p2: "This keeps the docs straightforward: open json for JSON, csv for CSV, file for file I/O, and so on. Cross-cutting runtime-library behavior has its own page.", content: content, code: "# Reading flow\n# 1. Standard Library: understand the module map\n# 2. Library Index: choose one module\n# 3. Module page: read one API surface\n# 4. Copy, download, or run the focused example", note: "Avoid mixed module pages. They make examples look convenient, but they hide which import owns which behavior.", code2: "import \"json\".\n\nPerson(name: \"\").\n\nmain() =>\n    rows := Person.all(),\n    names := lambda(rows, p => p.name),\n    return (json: json.toText(data: names))")).
