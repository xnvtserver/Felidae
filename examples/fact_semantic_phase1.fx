import "fact.fx"

main() =>
    rex := {
        identity: {name: "Rex", species: "dog"},
        physical: {size: "medium", color: "brown", weight: 24},
        behaviour: {temperament: "friendly", activity: "high"},
        tags: ["pet", "domestic"]
    }
    milo := {
        identity: {name: "Milo", species: "cat"},
        physical: {size: "small", color: "brown", weight: 6},
        behaviour: {temperament: "friendly", activity: "medium"},
        tags: ["pet", "domestic"]
    }
    projection := fact.extract_semantics(input: rex)
    similarity := fact.areSimilar(fact1: rex, fact2: milo, algorithm: "Wu-Palmer", threshold: 0.70)
    difference := fact.difference(fact1: rex, fact2: milo)
    near := fact.near(fact1: rex, fact2: milo, threshold: 0.40)
    return (
        projection: projection,
        similarity: similarity,
        difference: difference,
        near: near
    )
