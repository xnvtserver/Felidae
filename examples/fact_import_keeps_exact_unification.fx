import "fact.fx".

Employee(fullName: "Alice")

main() =>
    candidate := {fullName: "Alice"},
    pattern := {name: "Alice"},
    semantic := fact.semantic_unify(pattern: pattern, candidate: candidate, threshold: 0.25),
    return (semantic: semantic)
