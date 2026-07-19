main() =>
    text := file.readFile(path: "build/large_over_10mb_input.txt"),
    first := file.readLine(path: "build/large_over_10mb_input.txt", line: 0),
    middle := file.readLine(path: "build/large_over_10mb_input.txt", line: 60000),
    last := file.readLine(path: "build/large_over_10mb_input.txt", line: 119999),
    return (
        has_first: str.contains(data: text, needle: "row-000000"),
        has_middle: str.contains(data: text, needle: "row-060000"),
        has_last: str.contains(data: text, needle: "row-119999"),
        first: first,
        middle: middle,
        last: last
    ).
