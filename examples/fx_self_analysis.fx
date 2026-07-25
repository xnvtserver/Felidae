import "file".

LineKind(line: string) =>
    contains(line, "import") == "true",
    return ("import")
else
    contains(line, "=>") == "true",
    return ("method")
else
    contains(line, "http.") == "true",
    return ("http-call")
else
    contains(line, "return") == "true",
    return ("return")
else
    contains(line, "#") == "true",
    return ("comment")
else
    return ("other")

LineInfo(line: string) =>
    return (
        text: line,
        kind: LineKind(line: line),
        length: length(line),
        has_open_paren: contains(line, "("),
        has_close_paren: contains(line, ")"),
        has_arrow: contains(line, "=>"),
        has_assignment: contains(line, ":=")
    )

main() =>
    lines := file.readLines(path: "examples/web_server.fx"),
    classified := lambda(lines, line => LineInfo(line: line)),
    imports := lambda(classified, item => item.kind == "import"),
    methods := lambda(classified, item => item.kind == "method"),
    httpCalls := lambda(classified, item => item.kind == "http-call"),
    comments := lambda(classified, item => item.kind == "comment"),
    return (
        file: "examples/web_server.fx",
        line_count: count(lines),
        import_count: count(imports),
        method_count: count(methods),
        http_call_count: count(httpCalls),
        comment_count: count(comments),
        classified: classified,
        missing_language_primitives: [
            "string.split",
            "string.trim",
            "string.startsWith",
            "regex.match",
            "array.reduce",
            "expression array:get evaluation",
            "method body builtin output binding"
        ]
    )
