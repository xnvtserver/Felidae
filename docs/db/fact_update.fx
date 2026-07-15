import ("csv" "file" "fact_customers.fx").

UpdateCustomer(input: any) =>
    row := input,
    where row.total >= 200,
    return (name: row.name, city: row.city, total: row.total, status: row.status, tier: "gold")
else
    return (name: row.name, city: row.city, total: row.total, status: row.status, tier: row.tier).

main() =>
    rows := lambda(Customer, row => UpdateCustomer(input: row)),
    gold := lambda(rows, row => row.tier == "gold"),
    factText := csv.toFelidaeFacts(data: rows, type: "Customer"),
    writeStatus := file.writeFile(path: "docs/db/fact_customers.fx", data: factText, mode: "write"),
    return (action: "update", updated: count(rows), gold: count(gold), write: writeStatus, facts: factText).
