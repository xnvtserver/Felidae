# Lazy in-memory fact selection: the descriptor is cheap; materialization is
# explicit and uses the existing type/property indexes.
import ("db", "set", "data/converted_csv_country.fx")

main() =>
    india := db.select(type: "Country", field: "alpha_2", equals: "IN")
    rows := db.materialize(selection: india)
    setCount := Set.cardinality(set: india)
    return {selection: india, count: count(rows), set_count: setCount, country: rows}
