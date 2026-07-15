import ("json" "str").

DocSearchEntry(id: "overview", title: "Overview", route: "#overview", tags: "felidae logic programming facts data code database overview", summary: "Felidae is a functional logic language where facts are executable knowledge and documentation is generated from data.", searchText: "overview felidae logic programming facts data code database executable knowledge generated documentation").
DocSearchEntry(id: "basics", title: "Logic Programming Basics", route: "#basics", tags: "logic programming facts rules queries basics", summary: "Start with facts and rules, then ask questions over relationships instead of writing step-by-step control flow.", searchText: "basics logic programming facts rules queries relationships dataflow").
DocSearchEntry(id: "start", title: "Getting Started", route: "#start", tags: "install run command celidae felidae main query", summary: "Run Felidae programs through main(), use optional CLI fact inspection, and use Celidae for checks, graphs, and editor diagnostics.", searchText: "getting started run command main query cli celidae felidae check graph diagnostics").
DocSearchEntry(id: "syntax", title: "Syntax Essentials", route: "#syntax", tags: "syntax named arguments immutable fields str concat", summary: "Felidae uses named fields, immutable := bindings, package calls, member access, and strict current syntax.", searchText: "syntax named fields named arguments immutable bindings str concat left right package calls member access").
DocSearchEntry(id: "facts", title: "Facts As Database", route: "#facts", tags: "facts database no sql db repeated fields nested data", summary: "Facts are the database layer. Repeated fields become arrays and can be queried with db helpers or lambdas.", searchText: "facts database no sql db repeated fields arrays nested rain cat data").
DocSearchEntry(id: "queries", title: "Queries And Aggregates", route: "#queries", tags: "queries aggregates search contains count db main cli", summary: "Prefer reusable queries in main() or methods, with command-line queries reserved for ad hoc fact inspection.", searchText: "queries aggregates main methods search contains count average sum min max db all find first command line").
DocSearchEntry(id: "methods", title: "Rules, Methods, And Returns", route: "#methods", tags: "rules methods returns arguments fallback where tuple main", summary: "Use rules for relationships and methods/main() for repeatable application queries and structured return values.", searchText: "rules methods returns explicit inputs where fallback tuple immutable arguments main queries").
DocSearchEntry(id: "reference", title: "Language Reference", route: "#reference", tags: "reference docs language operators visualization exceptions", summary: "Exact language rules for operators, anonymous variables, runtime behavior, exceptions, and visualization.", searchText: "language reference operators anonymous variables exceptions visualization runtime celidae").
DocSearchEntry(id: "stdlib", title: "Standard Library", route: "#stdlib", tags: "stdlib modules file http json csv str array math db probability", summary: "Core modules provide file, HTTP, JSON, CSV, strings, arrays, math, facts, probability, ML, process, and thread helpers.", searchText: "standard library stdlib modules file http json csv str array math db probability ml process thread").
DocSearchEntry(id: "libraries", title: "Core Modules And Libraries", route: "#libraries", tags: "core modules libraries native imports", summary: "A module index for Felidae declaration files and native-backed library capabilities.", searchText: "core modules libraries native imports db probability str http json csv").
DocSearchEntry(id: "probability", title: "Probability On Facts", route: "#probability", tags: "probability statistics normalize entropy variance correlation facts", summary: "Probability helpers operate on arrays derived from facts for distributions, similarity, and statistical analysis.", searchText: "probability statistics normalize entropy variance standard deviation covariance correlation facts rain cats similarity").
DocSearchEntry(id: "native", title: "Native Modules", route: "#native", tags: "native modules c++ bridge performance", summary: "Native modules keep heavy work behind stable Felidae declarations while C++ handles implementation details.", searchText: "native modules bridge cpp performance runtime implementation").
DocSearchEntry(id: "debugging", title: "Debugging And Editor Diagnostics", route: "#debugging", tags: "debugging diagnostics celidae vscode lsp graph", summary: "Celidae owns diagnostics, graph inspection, visualization, LSP, and editor integration workflows.", searchText: "debugging diagnostics celidae vscode intellij lsp graph visualization check json").
DocSearchEntry(id: "hosting", title: "Documentation Hosting", route: "#hosting", tags: "hosting documentation server html components", summary: "The docs site is a Felidae program that renders documentation facts into HTML at startup.", searchText: "hosting documentation server html components render startup generated static").
DocSearchEntry(id: "server", title: "Server Features", route: "#server", tags: "server routes navigation spa copy playground search", summary: "The server renders a single-page app with routing, navigation, code copy, playground, and generated docs.", searchText: "server features routes navigation spa copy playground search static response").
DocSearchEntry(id: "downloads", title: "Download", route: "#downloads", tags: "download felidae celidae extension interpreter binaries releases", summary: "Per-OS download guidance for Felidae and Celidae plus separate VS Code and IntelliJ extension downloads.", searchText: "download felidae celidae extension interpreter vscode intellij build install windows linux macos binaries releases").
DocSearchEntry(id: "version", title: "Version And Release Notes", route: "#version", tags: "version release notes snapshot", summary: "Current documentation snapshot and product split between Felidae execution and Celidae diagnostics.", searchText: "version release notes snapshot felidae celidae diagnostics execution").
DocSearchEntry(id: "milestones", title: "Language Milestones", route: "#milestones", tags: "milestones roadmap interpreter visualizer extension celidae vscode intellij", summary: "Development milestones from interpreter to fact database, visualizer, editor extensions, documentation SPA, and future releases.", searchText: "milestones roadmap interpreter facts database stdlib visualizer celidae diagnostics vscode intellij extension documentation spa releases").
DocSearchEntry(id: "about", title: "About Felidae", route: "#about", tags: "about github project code data", summary: "Project information, design principles, repository link, and editor ecosystem notes.", searchText: "about github project repository code data logic language").
DocSearchEntry(id: "playground", title: "Playground", route: "#playground", tags: "playground examples copy run command", summary: "Edit a Felidae sample, copy the program or run command, and try examples locally.", searchText: "playground examples copy run command local sample editor").

DocsSearchIndex() =>
    return (lambda(DocSearchEntry, entry => entry)).

DocsSearchJson() =>
    rows := DocsSearchIndex(),
    return (json.toText(data: rows)).

SearchDocumentationResult(entry: any, query: string) =>
    matched := str.contains(data: entry.searchText, needle: query),
    matched == "true",
    return (
        id: entry.id,
        title: entry.title,
        route: entry.route,
        summary: entry.summary
    ).

SearchDocumentation(query: string) =>
    normalized := str.lower(data: query),
    matches := lambda(DocSearchEntry, entry => SearchDocumentationResult(entry: entry, query: normalized)),
    return (
        query: query,
        count: count(matches),
        results: matches
    ).
