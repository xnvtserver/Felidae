import ("db", "fact_customers.fx").

main() =>
    sea := db.find(type: "Customer", field: "city", equals: "SEA"),
    active := lambda(Customer, row => row.status == "active"),
    names := sort(lambda(active, row => row.name)),
    return (action: "query", sea_count: count(sea), active_count: count(active), active_names: names, sea_rows: sea).
