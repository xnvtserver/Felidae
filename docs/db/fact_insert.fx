import ("csv" "file").

IncomingCustomer(name: "Ada", city: "SEA", total: 250, status: "active").
IncomingCustomer(name: "Mina", city: "BLR", total: 180, status: "active").
IncomingCustomer(name: "Ravi", city: "SEA", total: 90, status: "inactive").

CustomerRow(input: any) =>
    row := input,
    return (
        name: row.name,
        city: row.city,
        total: row.total,
        status: row.status,
        tier: "standard"
    ).

main() =>
    rows := lambda(IncomingCustomer, row => CustomerRow(input: row)),
    factText := csv.toFelidaeFacts(data: rows, type: "Customer"),
    writeStatus := file.writeFile(path: "docs/db/fact_customers.fx", data: factText, mode: "write"),
    return (action: "insert", count: count(rows), output: "docs/db/fact_customers.fx", write: writeStatus, facts: factText).
