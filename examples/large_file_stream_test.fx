main() =>
    rows := ["alpha", "beta", "gamma", "delta", "epsilon"],
    writeStatus := file.writeLines(path: "build/large_file_stream_test.tmp", data: rows, mode: "write"),
    lines := file.readLines(path: "build/large_file_stream_test.tmp"),
    text := file.readFile(path: "build/large_file_stream_test.tmp"),
    third := file.readLine(path: "build/large_file_stream_test.tmp", line: 2),
    return (
        write: writeStatus,
        line_count: count(lines),
        third: third,
        has_epsilon: str.contains(data: text, needle: "epsilon")
    ).
