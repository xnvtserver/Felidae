Person(
    name: "Default",
    age: 0,
    country: "India"
)

Employee extend Person(
    name: "Ravi",
    age: 30,
    role: "Engineer"
)

Student extend Person(
    name: "Anu",
    age: 17,
    class: "12"
)

Animal(
    name: "Tiger",
    age: 5
)

isAdult(input: Person) =>
    p := input
    where p.age >= 18
    return (
        name: p.name
    )

Adults := lambda(Person, p => isAdult(input: p))

Names := lambda(Person, p => p.name)

ParsedDocs() =>
    docs := [
        {id: 1, doc: "Primary"},
        {id: 2, doc: "Secondary"}
    ]
    parsed := lambda(docs, d => ParseDoc(d.doc))
    return (
        result: parsed
    )

main() =>
    adults := Adults
    names := Names
    parsedDocs := ParsedDocs()
    system.print(value: adults)
    system.print(value: names)
    system.print(value: parsedDocs.result)
    return (
        adults: adults,
        names: names,
        parsedDocs: parsedDocs.result
    )
