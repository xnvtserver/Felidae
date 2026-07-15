import ("str" "html_components.fx").

MilestoneItem(step: string, title: string) =>
    stepTag := RenderDiv(input: HtmlDivData(name: "div", id: "", class: "milestone-year", content: step)),
    titleTag := RenderHtmlTag(name: "h3", id: "", class: "milestone-title", content: title),
    content := str.concat(left: stepTag, right: titleTag),
    return (RenderDiv(input: HtmlDivData(name: "div", id: "", class: "milestone", content: content))).

MilestoneTimeline() =>
    interpreter := MilestoneItem(step: "1", title: "Interpreter"),
    logic := MilestoneItem(step: "2", title: "Facts, Rules, And Methods"),
    database := MilestoneItem(step: "3", title: "Fact Database And Aggregates"),
    libraries := MilestoneItem(step: "4", title: "Core Libraries"),
    diagnostics := MilestoneItem(step: "5", title: "Celidae Diagnostics And Visualizer"),
    editors := MilestoneItem(step: "6", title: "Editor Extensions"),
    wasm := MilestoneItem(step: "7", title: "WASM Playground"),
    releases := MilestoneItem(step: "Next", title: "Stable Releases And Packages"),
    part1 := str.concat(left: interpreter, right: logic),
    part2 := str.concat(left: part1, right: database),
    part3 := str.concat(left: part2, right: libraries),
    part4 := str.concat(left: part3, right: diagnostics),
    part5 := str.concat(left: part4, right: editors),
    part6 := str.concat(left: part5, right: wasm),
    return (RenderDiv(input: HtmlDivData(name: "div", id: "", class: "timeline", content: str.concat(left: part6, right: releases)))).

DocsMilestones() =>
    timeline := MilestoneTimeline(),
    return (HtmlRichSectionData(id: "milestones", title: "Language Milestones", p: "Felidae milestones are the short development path of the language itself.", p2: "This list intentionally stays compact: interpreter first, then logic features, fact storage, libraries, tooling, editor support, browser execution, and stable packages.", content: timeline, code: "MilestoneItem(step: \"1\", title: \"Interpreter\")\nMilestoneItem(step: \"3\", title: \"Fact Database And Aggregates\")\nMilestoneItem(step: \"7\", title: \"WASM Playground\")", note: "Keep this page short. Detailed explanations belong in the dedicated documentation sections.", code2: "Current progression:\n1. Interpreter\n2. Facts, rules, and methods\n3. Fact database and aggregates\n4. Core libraries\n5. Celidae diagnostics and visualizer\n6. Editor extensions\n7. WASM playground\nNext. Stable releases and packages")).
