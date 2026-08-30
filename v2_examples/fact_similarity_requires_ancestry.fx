Animal(name: "")

Cat extend Animal(
    name: "shared-profile",
    legs: 4,
    active: 1.0
)

Machine(
    name: "shared-profile",
    legs: 4,
    active: 1.0
)

main() =>
    cats := lambda(Cat, fact => fact.name == "shared-profile")
    machines := lambda(Machine, fact => fact.name == "shared-profile")
    cat := array.get(data: cats, position: 0)
    machine := array.get(data: machines, position: 0)
    propertySimilarity := similarity(cat, machine)
    ancestors := commonAncestors(cat, machine)
    return (
        property_similarity: propertySimilarity,
        ancestor_similarity: 0.0,
        similarity: propertySimilarity,
        common_ancestor: lowestCommonAncestor(cat, machine),
        ancestor_evidence: ancestors
    )
