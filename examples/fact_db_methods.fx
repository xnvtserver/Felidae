import ("json")

Customer(name: "Alice", city: "SEA", status: "active", total: 250)
Customer(name: "Bob", city: "LAX", status: "active", total: 90)
Customer(name: "Carol", city: "SEA", status: "inactive", total: 140)
Customer(name: "Dana", city: "SEA", status: "active", total: 320)

ActiveSeaCustomers() =>
    rows := Fact.find(type: "Customer", field: "city", equals: "SEA")
    active := lambda(rows, row => row.status == "active")
    return (rows: active)

main() =>
    allRows := Fact.all(type: "Customer")
    seaActive := ActiveSeaCustomers()
    types := Fact.types()
    fields := Fact.fields(type: "Customer")
    jsonRows := json.toText(data: seaActive.rows)
    return (
        total: count(allRows),
        active_sea_total: count(seaActive.rows),
        types: types,
        fields: fields,
        json: jsonRows
    )
