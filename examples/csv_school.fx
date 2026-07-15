import ("file" "csv").

SchoolRows := csv.toFacts(
    data: file.readFile(path: "examples/data/School.csv"),
    type: "School"
).

PhysicsStudents() =>
    return (
        lambda(SchoolRows, row => row.subject == "physics")
    ).

Class10cStudents() =>
    return (
        lambda(SchoolRows, row => row.class == "10c")
    ).

main() =>
    physicsRows := PhysicsStudents(),
    factText := csv.toFelidaeFacts(
        data: lambda(physicsRows, row => {student: row.student, subject: row.subject}),
        type: "School"
    ),
    exportStatus := file.writeFile(
        path: "examples/data/converted_csv_school.fx",
        data: factText,
        mode: "write"
    ),
    return (
        export: exportStatus,
        physics: PhysicsStudents(),
        class10c: Class10cStudents()
    ).
