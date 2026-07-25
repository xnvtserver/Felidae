import "html_components.fx".

DocsLanguageReference() =>
    andCard := RenderMiniCard(input: HtmlCardData(title: "AND", text: "Comma means every goal in the sequence must hold.")),
    orCard := RenderMiniCard(input: HtmlCardData(title: "OR", text: "Pipe creates alternative goal branches.")),
    anonymous := RenderMiniCard(input: HtmlCardData(title: "Anonymous", text: "Use _ when a matched field should not be printed.")),
    errors := RenderMiniCard(input: HtmlCardData(title: "Errors", text: "throw(msg: reason) binds an in-language error reason or routes to a handler.")),
    memory := RenderMiniCard(input: HtmlCardData(title: "Memory", text: "Facts and rules are stored by predicate name with runtime indexes and caches.")),
    visual := RenderMiniCard(input: HtmlCardData(title: "Visualization", text: "Celidae can inspect graphs and export data-analysis HTML.")),
    part1 := str.concat(left: andCard, right: orCard),
    part2 := str.concat(left: part1, right: anonymous),
    part3 := str.concat(left: part2, right: errors),
    part4 := str.concat(left: part3, right: memory),
    part5 := str.concat(left: part4, right: visual),
    content := RenderGrid(content: part5),
    return (HtmlRichSectionData(id: "reference", title: "Language Reference", p: "This route summarizes important language behavior from docs_language.md that does not fit into the beginner pages. Use it when you already know the basics and need exact rules for operators, anonymous variables, exceptions, memory, or visualization.", p2: "Felidae stores facts and rules by predicate name. Runtime caches and compatible-fact indexes are invalidated when new program state is loaded. felidae_debug may cache parsed AST diagnostics, Celidae builds graphs directly from parsed facts, and felidae.exe stays focused on execution.", content: content, code: "EngineerInSEA(employee: e, name: Name) =>\n    Name == e.name,\n    e.role == \"Engineer\",\n    e.office == \"SEA\".\n\nTechnicalOrManager(name: name) =>\n    Employee(name: name, role: \"Engineer\") |\n    Employee(name: name, role: \"Manager\").\n\n? Employee(name: Name, role: _, office: \"SEA\")", note: "Method calls do not implicitly scan fact types. Use lambda(Type, item => ...) or db helpers when iteration is intended.", code2: "DivideFailure(error_reason: error_reason) =>\n    throw(msg: \"DivisionByZero\"),\n    error_reason == \"DivisionByZero\".\n\n# Visualization is a separate tool path:\ncelidae program.fx --json --load-imports\ncelidae program.fx --html --load-imports")).
