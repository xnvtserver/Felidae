Customer(id: "c001", name: "Alice", city: "SEA", total: 250, status: "active", tier: "standard")
Customer(id: "c002", name: "Bob", city: "LAX", total: 90, status: "active", tier: "standard")
Customer(id: "c003", name: "Carol", city: "SEA", total: 140, status: "inactive", tier: "standard")

CustomerRow(input: row) =>
    c := row
    return (
        id: c.id,
        name: c.name,
        city: c.city,
        total: c.total,
        status: c.status,
        tier: c.tier
    )

ActiveSeaCustomer(input: row) =>
    c := row
    where c.city == "SEA"
    where c.status == "active"
    return (
        id: c.id,
        name: c.name,
        city: c.city,
        total: c.total,
        status: c.status,
        tier: c.tier
    )

CleanCustomer(input: row) =>
    c := row
    where c.status != "deleted"
    return (
        id: c.id,
        name: c.name,
        city: c.city,
        total: c.total,
        status: c.status,
        tier: c.tier
    )
