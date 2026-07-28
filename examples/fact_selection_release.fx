# A selection retains its immutable snapshot until explicitly released.
import ("db", "data/converted_csv_country.fx")

main() =>
    india := db.select(type: "Country", field: "alpha_2", equals: "IN")
    rows := db.materialize(selection: india)
    released := db.release(selection: india)
    return {count: count(rows), released: released}
