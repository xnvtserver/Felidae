import ("csv", "file", "data/customer_facts_updated.fx")

KeepCustomer(input: row) =>
    c := row
    where c.status != "inactive"
    where c.total >= 200
    return (
        name: c.name,
        city: c.city,
        total: c.total,
        status: c.status,
        tier: c.tier
    )

DeletedCustomer(input: row) =>
    c := row
if c.status == "inactive" then
    return (
        name: c.name,
        reason: "inactive"
    )
else
    where c.total < 200
    return (
        name: c.name,
        reason: "below minimum total"
    )

main() =>
    keptRows := lambda(Customer, c => KeepCustomer(input: c))
    deletedRows := lambda(Customer, c => DeletedCustomer(input: c))
    factText := csv.toFelidaeFacts(data: keptRows, type: "Customer")
    writeStatus := file.writeFile(
        path: "examples/data/customer_facts_final.fx",
        data: factText,
        mode: "write"
    )
    return (
        kept_count: count(keptRows),
        deleted_count: count(deletedRows),
        output_file: "examples/data/customer_facts_final.fx",
        write: writeStatus,
        deleted: deletedRows,
        facts: factText
    )
