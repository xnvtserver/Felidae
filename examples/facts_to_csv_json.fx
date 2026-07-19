import ("csv", "json").

Employee(name: "Alice", role: "Engineer", office: "SEA").
Employee(name: "Bob", role: "Manager", office: "LAX").
Employee(name: "Carol", role: "Engineer", office: "SEA").

EmployeeExportRow(input: Employee) =>
    e := input,
    return (
        name: e.name,
        role: e.role,
        office: e.office
    ).

SeaEngineer(input: Employee) =>
    e := input,
    e.office == "SEA",
    e.role == "Engineer",
    return (
        name: e.name,
        role: e.role,
        office: e.office
    ).

main() =>
    rows := lambda(Employee, employee => EmployeeExportRow(input: employee)),
    seaEngineers := lambda(Employee, employee => SeaEngineer(input: employee)),
    csvText := csv.toText(data: rows),
    factText := csv.toFelidaeFacts(data: seaEngineers, type: "SeaEngineer"),
    jsonText := json.toText(data: rows),
    parsedRows := json.parse(data: jsonText),
    return (
        row_count: count(rows),
        sea_engineer_count: count(seaEngineers),
        csv_text: csvText,
        fact_text: factText,
        json_text: jsonText,
        parsed_rows: parsedRows
    ).
