import ("str" "html_components.fx").

MilestoneItem(year: string, title: string, text: string) =>
    yearTag := RenderDiv(input: HtmlDivData(name: "div", id: "", class: "milestone-year", content: year)),
    titleTag := RenderHtmlTag(name: "h3", id: "", class: "milestone-title", content: title),
    textTag := RenderHtmlTag(name: "p", id: "", class: "milestone-text", content: text),
    body := str.concat(left: titleTag, right: textTag),
    content := str.concat(left: yearTag, right: body),
    return (RenderDiv(input: HtmlDivData(name: "div", id: "", class: "milestone", content: content))).

MilestoneTimeline() =>
    interpreter := MilestoneItem(year: "Step 1", title: "Interpreter", text: "Felidae begins with a C++ interpreter for facts, rules, methods, main() workflows, imports, and command-line fact inspection."),
    facts := MilestoneItem(year: "Step 2", title: "Facts As Database", text: "Facts become the local no-SQL data layer. Repeated fields, nested values, db helpers, and aggregate-friendly queries turn data into executable knowledge."),
    stdlib := MilestoneItem(year: "Step 3", title: "Core Libraries", text: "Native-backed modules such as file, http, csv, json, db, probability, process, and thread keep heavy work behind stable Felidae declarations."),
    visualizer := MilestoneItem(year: "Step 4", title: "Visualizer", text: "Celidae adds graph inspection, data visualization, JSON diagnostics, and runtime-aware analysis for understanding loaded facts and rule relationships."),
    extension := MilestoneItem(year: "Step 5", title: "Editor Extensions", text: "VS Code and IntelliJ integrations connect editor feedback to Celidae so diagnostics, syntax support, and graph workflows stay aligned with the runtime."),
    docs := MilestoneItem(year: "Step 6", title: "Documentation SPA", text: "The documentation site is implemented as Felidae modules, rendered into a responsive SPA, searchable through documentation facts, and hostable behind Nginx."),
    next := MilestoneItem(year: "Next", title: "Production Language Surface", text: "The next milestone is a tighter request-router story, richer package documentation, release binaries per OS, and stable extension downloads for users."),
    part1 := str.concat(left: interpreter, right: facts),
    part2 := str.concat(left: part1, right: stdlib),
    part3 := str.concat(left: part2, right: visualizer),
    part4 := str.concat(left: part3, right: extension),
    part5 := str.concat(left: part4, right: docs),
    content := str.concat(left: part5, right: next),
    return (RenderDiv(input: HtmlDivData(name: "div", id: "", class: "timeline", content: content))).

DocsMilestones() =>
    timeline := MilestoneTimeline(),
    guidance := RenderGuidance(doText: "Keep milestones tied to working runtime capabilities, examples, diagnostics, and installable tooling.", dontText: "Do not present future plans as completed features. Label planned work clearly so users know what is stable today.", recommendText: "Update this page whenever the interpreter, Celidae, docs server, release packaging, or editor extensions gain a new user-visible capability."),
    content := str.concat(left: timeline, right: guidance),
    return (HtmlRichSectionData(id: "milestones", title: "Language Milestones", p: "Felidae is growing as a practical logic programming language: first the interpreter, then fact-database workflows, visualization, diagnostics, editor integrations, and this generated documentation SPA.", p2: "The timeline below shows the development path in plain product terms. It helps users understand what exists today, why Celidae matters, and where the language surface is heading next.", content: content, code: "MilestoneItem(year: \"Step 1\", title: \"Interpreter\", text: \"Facts, rules, methods, and main().\")\nMilestoneItem(year: \"Step 4\", title: \"Visualizer\", text: \"Celidae graph and data inspection.\")\nMilestoneItem(year: \"Step 5\", title: \"Editor Extensions\", text: \"VS Code and IntelliJ support.\")", note: "Milestones are documentation facts rendered through html_components.fx, so the page stays maintainable instead of becoming a long hand-written HTML string.", code2: "Recommended release progression:\n1. Interpreter and main() execution\n2. Fact database and aggregates\n3. Core native libraries\n4. Celidae diagnostics and visualizer\n5. VS Code and IntelliJ extensions\n6. Hosted documentation SPA\n7. OS release binaries and package routing")).
