# Lazy in-memory fact selection: the descriptor is cheap; materialization is
# explicit and uses the interpreter's type/property indexes.
import ("set", "data/converted_csv_country.fx")

main() =>
    india := Fact.select(type: "Country", field: "alpha_2", equals: "IN")
    rows := Fact.materialize(selection: india)
    setCount := Set.cardinality(set: india)
    return {selection: india, count: count(rows), set_count: setCount, country: rows}
