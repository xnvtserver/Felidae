import ("fact.fx", "fact_analysis.fx").

main() =>
    Employee1 := {name: "Mira", role: "Engineer", skill: "backend", score: 91},
    Employee2 := {name: "Ravi", role: "Engineer", skill: "platform", score: 88},
    Candidate := {name: "Asha", role: "Designer", skill: "product", score: 76},
    similar := fact.areSimilar(fact1: Employee1, fact2: Employee2, algorithm: "Leacock-Chodorow"),
    propertyMatch := fact.compareProperties(fact1: Employee1, fact2: Employee2),
    difference := fact.difference(fact1: Employee1, fact2: Candidate),
    nearest := fact_analysis.nearestFacts(input: Employee1, candidates: [Employee2, Candidate], count: 1),
    return (
        similar: similar,
        propertyMatch: propertyMatch,
        difference: difference,
        nearest: nearest
    ).
