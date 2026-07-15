import ("csv" "file" "fact_customers.fx").

KeepCustomer(input: any) =>
    row := input,
    where row.status == "active",
    return (name: row.name, city: row.city, total: row.total, status: row.status, tier: row.tier).

main() =>
    kept := lambda(Customer, row => KeepCustomer(input: row)),
    removed := count(Customer) - count(kept),
    factText := csv.toFelidaeFacts(data: kept, type: "Customer"),
    writeStatus := file.writeFile(path: "docs/db/fact_customers.fx", data: factText, mode: "write"),
    return (action: "delete", kept: count(kept), removed: removed, write: writeStatus, facts: factText).
