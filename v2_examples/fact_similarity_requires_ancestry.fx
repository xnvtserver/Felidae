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

def main() =>
    cats := lambda(Cat, fact => fact.name == "shared-profile")
    machines := lambda(Machine, fact => fact.name == "shared-profile")
    cat := array.get(data: cats, position: 0)
    machine := array.get(data: machines, position: 0)
    comparison := Relation.compare(left: cat, right: machine)
    ancestors := commonAncestors(cat, machine)
    return (
        property_similarity: comparison.evidence.propertySimilarity,
        ancestor_similarity: comparison.evidence.ancestorSimilarity,
        similarity: comparison.evidence.similarity,
        common_ancestors: ancestors,
        ancestor_evidence: ancestors
    )
