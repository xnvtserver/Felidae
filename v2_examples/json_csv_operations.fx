import ("json", "csv")

main() =>
    source := {name: "Ada", score: 42},
    updated := json.set(data: source, key: "active", value: 1),
    rows := [source, {name: "Grace", score: 37}],
    return (
        name: json.get(data: updated, key: "name"),
        has_active: json.has(data: updated, key: "active"),
        keys: json.keys(data: updated),
        removed: json.remove(data: updated, key: "score"),
        json_text: json.toText(data: updated),
        csv_text: csv.toText(data: rows)
    )
