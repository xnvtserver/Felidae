import ("file", "csv")

CountryRows := csv.toFacts(
    data: file.readFile(path: "data/countries.csv"),
    type: "Country"
)

getCountry(name: string) =>
    return (
        # lambda(CountryRows, row => row.name == name)
        lambda(CountryRows, row => row)
    )


main() =>
    countryRows := getCountry(name: "India"),
    factText := csv.toFelidaeFacts(
        data: lambda(countryRows, row => {name: row.name, alpha_2: row.alpha_2, country_code : row.country_code}),
        type: "Country"
    ),
    exportStatus := file.writeFile(
        path: "data/converted_csv_country.fx",
        data: factText,
        mode: "write"
    ),
    return (
        export: exportStatus,
        country: getCountry(name: "India")
    )
