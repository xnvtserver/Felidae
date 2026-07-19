import ("csv", "file").

SourceCustomer(name: "Alice", city: "SEA", total: 250, status: "active").
SourceCustomer(name: "Bob", city: "LAX", total: 90, status: "active").
SourceCustomer(name: "Carol", city: "SEA", total: 140, status: "inactive").
SourceCustomer(name: "Dana", city: "SEA", total: 320, status: "active").

CustomerSeedRow(input: SourceCustomer) =>
    c := input,
    return (
        name: c.name,
        city: c.city,
        total: c.total,
        status: c.status,
        tier: "standard"
    ).

SqlWhereSeed(input: SourceCustomer) =>
    c := input,
    where c.city == "SEA",
    where c.total >= 100,
    return (
        name: c.name,
        city: c.city,
        total: c.total,
        status: c.status,
        tier: "standard"
    ).

main() =>
    allRows := lambda(SourceCustomer, c => CustomerSeedRow(input: c)),
    filteredRows := lambda(SourceCustomer, c => SqlWhereSeed(input: c)),
    factText := csv.toFelidaeFacts(data: filteredRows, type: "Customer"),
    writeStatus := file.writeFile(
        path: "examples/data/customer_facts.fx",
        data: factText,
        mode: "write"
    ),
    return (
        source_count: count(allRows),
        inserted_count: count(filteredRows),
        output_file: "examples/data/customer_facts.fx",
        write: writeStatus,
        facts: factText
    ).
