import "data/converted_csv_country.fx".

CountryByCode(alpha2: alpha2, name: name, code: code) =>
    Country(name: name, alpha_2: alpha2, country_code: code)
    return

IndiaCountry(name: name, alpha2: alpha2, code: code) =>
    Country(name: name, alpha_2: "IN", country_code: code)
    alpha2 == "IN"
    return
