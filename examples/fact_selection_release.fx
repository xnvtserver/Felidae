# A selection retains its immutable snapshot until explicitly released.
import ("data/converted_csv_country.fx")

main() =>
    india := Fact.select(type: "Country", field: "alpha_2", equals: "IN")
    rows := Fact.materialize(selection: india)
    released := Fact.release(selection: india)
    return {count: count(rows), released: released}
