import ("file", "csv")

# The simplest possible CSV -> facts -> Celidae pipeline: no filtering, no
# transformation, just convert every row of a CSV file into facts and write
# them out as a .fx file. Point celidae at the result to get an interactive,
# presentation-ready diagram with no other Felidae code to write.
#
#   .\build\felidae.exe examples\csv_to_visualization.fx
#   .\build\celidae.exe examples\data\School_facts.fx --html > school.html
#   .\build\celidae.exe examples\data\School_facts.fx --svg --type=er > school.svg

main() =>
    rows := csv.toFacts(
        data: file.readFile(path: "examples/data/School.csv"),
        type: "School"
    )
    factText := csv.toFelidaeFacts(data: rows, type: "School")
    exportStatus := file.writeFile(
        path: "examples/data/School_facts.fx",
        data: factText,
        mode: "write"
    )
    return (
        export: exportStatus,
        rows_converted: count(rows)
    )
