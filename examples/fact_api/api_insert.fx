import ("array", "csv", "file", "db.fx", "temp_insert.fx").

# temp_insert.fx represents a request payload written by the API adapter.
# Insert appends fact rows and rewrites the fact store output file only.

ExistingRow(input: row) =>
    c := row,
    return (
        id: c.id,
        name: c.name,
        city: c.city,
        total: c.total,
        status: c.status,
        tier: c.tier
    ).

InsertedRow(input: row) =>
    c := row,
    return (
        id: c.id,
        name: c.name,
        city: c.city,
        total: c.total,
        status: c.status,
        tier: c.tier
    ).

main() =>
    existingRows := lambda(Customer, c => ExistingRow(input: c)),
    insertRows := lambda(NewCustomer, c => InsertedRow(input: c)),
    newRow := array.get(data: insertRows, position: 0),
    insertedRows := array.push(data: existingRows, value: newRow),
    factText := csv.toFelidaeFacts(data: insertedRows, type: "Customer"),
    writeStatus := file.writeFile(
        path: "examples/fact_api/db_after_insert.fx",
        data: factText,
        mode: "write"
    ),
    return (
        before_count: count(existingRows),
        inserted_count: count(insertRows),
        after_count: count(insertedRows),
        db_file: "examples/fact_api/db_after_insert.fx",
        write: writeStatus,
        facts: factText
    ).
