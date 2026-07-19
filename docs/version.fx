import "html_components.fx".

DocsVersion() =>
    language := RenderMiniCard(input: HtmlCardData(title: "Language", text: ".fx files, named fields, immutable bindings, explicit fact iteration, and data-shaped docs.")),
    runtime := RenderMiniCard(input: HtmlCardData(title: "Runtime", text: "felidae for main() workflows, program execution, hosted documentation, and optional command-line fact inspection.")),
    analysis := RenderMiniCard(input: HtmlCardData(title: "Analysis", text: "celidae for check-json, LSP, graph inspection, visualization, and editor diagnostics.")),
    compatibility := RenderMiniCard(input: HtmlCardData(title: "Compatibility", text: "Windows release assets use .exe names; felidae_debug.exe remains a legacy Celidae-compatible binary name.")),
    part1 := str.concat(left: language, right: runtime),
    part2 := str.concat(left: part1, right: analysis),
    part3 := str.concat(left: part2, right: compatibility),
    content := RenderGrid(content: part3),
    return (HtmlRichSectionData(id: "version", title: "Version And Release Notes", p: "Docs version: 2026-07-13 local documentation snapshot. This version reflects the current checkout, README.md, docs_language.md, docs_native_modules.md, and the generated Felidae SPA under docs/.", p2: "The product split is Felidae for execution and Celidae for diagnostics, debugging, analytics, LSP, and visualization. The docs site is a living release surface: supported syntax, modules, editor setup, download guidance, and runtime commands are recorded directly in Felidae files.", content: content, code: "./build/felidae program.fx\n./build/celidae program.fx --check-json\n./build/celidae program.fx --inspect-graph\n./build/celidae program.fx --visualize-data-html --load-imports\n./build/celidae --lsp\n\n# Optional fact inspection:\n./build/felidae program.fx '? Fact(field: value)'", note: "Version pages should be updated when syntax contracts change, especially stdlib method names like str.concat(left: ..., right: ...), main() query guidance, download paths, Linux hosting, or Celidae diagnostics ownership.", code2: "Legacy Windows compatibility:\nbuild\\felidae_debug.exe program.fx --check-json\n\nRecommended Linux naming:\n./build/celidae program.fx --check-json\n\nCanonical notes:\ndocs_language.md\ndocs_native_modules.md\nREADME.md")).
