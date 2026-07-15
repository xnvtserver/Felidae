import ("csv" "file" "data/customer_facts.fx").

UpdateCustomer(input: row) =>
    c := row,
    where c.total >= 200,
    return (
        name: c.name,
        city: c.city,
        total: c.total,
        status: c.status,
        tier: "gold"
    )
else
    return (
        name: c.name,
        city: c.city,
        total: c.total,
        status: c.status,
        tier: c.tier
    ).

main() =>
    updatedRows := lambda(Customer, c => UpdateCustomer(input: c)),
    goldRows := lambda(updatedRows, c => c.tier == "gold"),
    factText := csv.toFelidaeFacts(data: updatedRows, type: "Customer"),
    writeStatus := file.writeFile(
        path: "examples/data/customer_facts_updated.fx",
        data: factText,
        mode: "write"
    ),
    return (
        updated_count: count(updatedRows),
        gold_count: count(goldRows),
        output_file: "examples/data/customer_facts_updated.fx",
        write: writeStatus,
        facts: factText
    ).
