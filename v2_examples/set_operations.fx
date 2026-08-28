import ("set")

main() =>
    left := [{id: 1, kind: "a"}, {id: 2, kind: "b"}],
    right := [{id: 2, kind: "b"}, {id: 3, kind: "c"}],
    return (
        union: Set.union(sets: [left, right]),
        intersection: Set.intersectionBy(sets: [left, right], fields: ["id"]),
        difference: Set.differenceBy(sets: [left, right], fields: ["id"]),
        symmetric: Set.symmetricDifferenceBy(sets: [left, right], fields: ["id"]),
        equals: Set.equals(sets: [left, right]),
        subset: Set.subset(sets: [left, right]),
        superset: Set.superset(sets: [left, right]),
        disjoint: Set.disjoint(sets: [left, right]),
        cardinality: Set.cardinality(set: left),
        contains: Set.containsBy(set: left, value: 2, fields: ["id"])
    )
