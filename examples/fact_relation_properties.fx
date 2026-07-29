import "fact"

main() =>
    ravi := Person(name: "Ravi")
    leela := Person(name: "Leela")
    government := Government(name: "public")
    politician := Politician(name: "council")
    policeman := Policeman(name: "community")
    mutualRespect := Relation.properties(pairs: [
        [ravi, leela],
        [leela, ravi]
    ])
    hierarchy := Relation.properties(pairs: [
        [government, politician],
        [politician, policeman],
        [government, policeman]
    ])
    return RelationPropertyReport(mutual_respect: mutualRespect, hierarchy: hierarchy)
