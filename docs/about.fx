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
    return (HtmlRichSectionData(id: "about", title: "About Felidae", p: "Felidae is designed for explicit logic dataflow, typed facts, local fact databases, and verified VM operations for heavy work.", p2: "Facts are first-class queryable database rows; hierarchy and graded numeric evidence support expert-system analysis without forcing every result into 0.0 or 1.0.", content: content, code: "School(name: \"North\", score: 3.432).\n\nmain() =>\n    return School.all()\nend", note: "Follow the repository for compiler, VM, debugger, editor, and documentation updates.", code2: "Repository:\nhttps://github.com/xnvtserver/Felidae")).
