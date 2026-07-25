import "file".

main() =>
    writeStatus := file.writeFile(
        path: "build/file_operations_test.tmp",
        data: "alpha\nbeta\n",
        mode: "write"
    ),
    appendStatus := file.writeFile(
        path: "build/file_operations_test.tmp",
        data: "gamma\n",
        mode: "append"
    ),
    lines := file.readLines(path: "build/file_operations_test.tmp"),
    text := file.readFile(path: "build/file_operations_test.tmp"),
    return (
        write: writeStatus,
        append: appendStatus,
        lines: lines,
        text: text
    )
