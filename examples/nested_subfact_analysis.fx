import "fact"

# Fact and vector nesting is structural: no fixed field names or type-specific
# identity shortcuts are required to inspect a composed expert-system input.

main() =>
    person := Person(name: "Ravi", role: "analyst")
    model := DecisionModel(
        regions: [
            Region(name: "north", members: []),
            Region(name: "south", members: [person])
        ],
        policy: Policy(reviewers: [Person(name: "Leela", role: "reviewer")])
    )
    found := fact.containsSubfact(input: model, candidate: person, maximumDepth: 4)
    missing := fact.containsSubfact(
        input: model,
        candidate: Person(name: "Maya", role: "analyst"),
        maximumDepth: 4
    )
    return NestedSubfactReport(
        found: found,
        missing: missing,
        bounded: fact.containsSubfact(input: model, candidate: person, maximumDepth: 2)
    )
