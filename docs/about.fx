import "html_components.fx".

DocsAbout() =>
    project := RenderMiniCard(input: HtmlCardData(title: "Project", text: "Felidae is a functional logic language where facts act as an in-memory knowledge database.")),
    principle := RenderMiniCard(input: HtmlCardData(title: "Principle", text: "Code is data and data is code. Documentation, examples, and routes are also Felidae facts.")),
    editors := RenderMiniCard(input: HtmlCardData(title: "Editors", text: "VS Code and IntelliJ integrations delegate language checks to Celidae.")),
    link := RenderAnchor(input: HtmlAnchorData(label: "github.com/xnvtserver/Felidae", href: "https://github.com/xnvtserver/Felidae", class: "")),
    follow := RenderMiniCardContent(input: HtmlCardContentData(title: "Follow", content: link)),
    part1 := str.concat(left: project, right: principle),
    part2 := str.concat(left: part1, right: editors),
    part3 := str.concat(left: part2, right: follow),
    content := RenderGrid(content: part3),
    return (HtmlRichSectionData(id: "about", title: "About Felidae", p: "Felidae is designed for explicit logic dataflow, typed facts, local fact databases, and native modules for heavy work. It is similar in spirit to logic/data languages such as Logica, but it keeps its own Felidae syntax and runtime contract.", p2: "The documentation site is intentionally implemented in Felidae: routes are component records, code examples are copyable, the playground is a Felidae fact, and the server renders everything at startup.", content: content, code: "FactDatabase() =>\n    facts := db.types(),\n    return (types: facts).\n\nDocsAbout() =>\n    return (HtmlRichSectionData(id: \"about\", title: \"About Felidae\", content: \"...\" )).", note: "Follow the repository for language, runtime, Celidae, VS Code extension, IntelliJ plugin, and docs-site updates.", code2: "Repository:\nhttps://github.com/xnvtserver/Felidae\n\nDocs command:\n./build/felidae docs/server.fx")).

