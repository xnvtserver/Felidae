import "set"

# Vectors are ordinary typed fact values. Set operations compare their complete
# structural values unless a field projection is requested explicitly.

main() =>
    a := [
        SignalVector(id: "v1", components: [1, 0, 1]),
        SignalVector(id: "v2", components: [0, 1, 1]),
        SignalVector(id: "v3", components: [1, 1, 0])
    ]
    b := [
        SignalVector(id: "v2", components: [0, 1, 1]),
        SignalVector(id: "v3", components: [1, 1, 0]),
        SignalVector(id: "v4", components: [0, 0, 1])
    ]
    union := Set.union(sets: [a, b])
    intersection := Set.intersection(sets: [a, b])
    left := Set.cardinality(set: union)
    right := Set.cardinality(set: a) + Set.cardinality(set: b) - Set.cardinality(set: intersection)
    if left == right then
        return InclusionExclusionProof(
            theorem: "|A union B| = |A| + |B| - |A intersection B|",
            union_cardinality: left,
            right_hand_side: right,
            proved: true,
            v2_in_intersection: Set.contains(set: intersection, value: SignalVector(id: "v2", components: [0, 1, 1])),
            a_is_subset_of_union: Set.subset(sets: [a, union]),
            union_is_superset_of_a: Set.superset(sets: [union, a])
        )
    else
        return InclusionExclusionProof(
            theorem: "|A union B| = |A| + |B| - |A intersection B|",
            union_cardinality: left,
            right_hand_side: right,
            proved: false,
            v2_in_intersection: Set.contains(set: intersection, value: SignalVector(id: "v2", components: [0, 1, 1])),
            a_is_subset_of_union: Set.subset(sets: [a, union]),
            union_is_superset_of_a: Set.superset(sets: [union, a])
        )
