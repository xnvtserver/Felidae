import "data/converted_csv_country.fx"

CountryByCode(alpha2: alpha2, name: name, code: code) =>
    Country(name: name, alpha_2: alpha2, country_code: code)
    return

main() =>
    graphJson := visualize.dataJson(loadImports: "true")
    html := visualize.dataHtml(loadImports: "true")
    wroteHtml := file.writeFile(path: "build/country_visualization.html", data: html, mode: "write")
    jsonHasCountry := str.contains(data: graphJson, needle: "\"label\":\"Country\"")
    jsonHasRecords := str.contains(data: graphJson, needle: "records=249 fields=4")
    htmlHasDocument := str.contains(data: html, needle: "<!doctype html>")
    return (
        json_has_country: jsonHasCountry,
        json_has_records: jsonHasRecords,
        html_has_document: htmlHasDocument,
        wrote_html: wroteHtml
    )
