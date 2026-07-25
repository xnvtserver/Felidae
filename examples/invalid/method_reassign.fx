Person(name: "Ravi", age: 30)

Bad(input: Person) =>
    p := input
    p := input
    return (name: p.name)

BadResult := lambda(Person, p => Bad(input: p))
