main() =>
    raw := "  Alice,Engineer,SEA  ",
    trimmed := str.trim(data: raw),
    parts := str.split(data: trimmed, delimiter: ","),
    normalized := str.replace(data: trimmed, search: "SEA", replacement: "NYC"),
    hasEngineer := str.contains(data: trimmed, needle: "Engineer"),
    startsAlice := str.startsWith(data: trimmed, prefix: "Alice"),
    endsSea := str.endsWith(data: trimmed, suffix: "SEA"),

    obj := json.parse(data: "{\"name\":\"Alice\",\"role\":\"Engineer\"}"),
    withOffice := json.set(data: obj, key: "office", value: "SEA"),
    withoutRole := json.remove(data: withOffice, key: "role"),
    jsonKeys := json.keys(data: withoutRole),
    hasOffice := json.has(data: withoutRole, key: "office"),
    office := json.get(data: withoutRole, key: "office"),

    rows := csv.parse(data: "name,role,office\nAlice,Engineer,SEA\nBob,Manager,LAX\n"),
    added := csv.addRow(data: rows, row: {name: "Carol", role: "Engineer", office: "SEA"}),
    seaRows := csv.findRows(data: added, key: "office", value: "SEA"),
    updated := csv.updateRows(data: added, key: "name", value: "Bob", patch: {office: "SEA"}),
    deleted := csv.deleteRows(data: updated, key: "role", value: "Manager"),
    csvText := csv.toText(data: deleted),

    writeStatus := file.writeLines(path: "build/stdlib_utilities.tmp", data: parts, mode: "write"),
    secondLine := file.readLine(path: "build/stdlib_utilities.tmp", line: 1),
    lines := file.readLines(path: "build/stdlib_utilities.tmp"),

    return (
        trimmed: trimmed,
        parts: parts,
        normalized: normalized,
        has_engineer: hasEngineer,
        starts_alice: startsAlice,
        ends_sea: endsSea,
        json_keys: jsonKeys,
        has_office: hasOffice,
        office: office,
        sea_count: count(seaRows),
        deleted_count: count(deleted),
        csv_text: csvText,
        write_status: writeStatus,
        second_line: secondLine,
        line_count: count(lines)
    ).
