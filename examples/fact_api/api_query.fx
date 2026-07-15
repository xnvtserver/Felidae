import ("csv" "json" "db.fx").

main() =>
    rows := lambda(Customer, c => CustomerRow(input: c)),
    activeSea := lambda(Customer, c => ActiveSeaCustomer(input: c)),
    jsonText := json.toText(data: activeSea),
    csvText := csv.toText(data: activeSea),
    return (
        total_count: count(rows),
        result_count: count(activeSea),
        query: "city == SEA and status == active",
        json: jsonText,
        csv: csvText,
        rows: activeSea
    ).
