Animal(name: "")

Cat extend Animal(
    name: "shared-profile",
    legs: 4,
    active: true
)

Machine(
    name: "shared-profile",
    legs: 4,
    active: true
)

main() =>
    cats := lambda(Cat, fact => fact.name == "shared-profile")
    machines := lambda(Machine, fact => fact.name == "shared-profile")
    cat := array.get(data: cats, position: 0)
    machine := array.get(data: machines, position: 0)
    comparison := Relation.compare(left: cat, right: machine)
    return (
        property_similarity: comparison.evidence.propertySimilarity,
        ancestor_similarity: comparison.evidence.ancestorSimilarity,
        similarity: comparison.evidence.similarity,
        common_ancestor: comparison.evidence.matchedAncestor
    )
