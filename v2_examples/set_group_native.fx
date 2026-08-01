import ("set", "group")

Fruit(name: "apple", taste: "sweet")
Fruit(name: "orange", taste: "sweet")
Fruit(name: "green mango", taste: "sour")

Vegis(name: "carrot", taste: "sweet")
Vegis(name: "tomato", taste: "sweet")

main() =>
    fruit := lambda(Fruit, item => item)
    vegis := lambda(Vegis, item => item)
    allFood := Set.union(sets: [fruit, vegis])
    commonTaste := Set.intersectionBy(sets: [fruit, vegis], fields: ["taste"])
    fruitOnlyTaste := Set.differenceBy(sets: [fruit, vegis], fields: ["taste"])
    disjointExact := Set.disjoint(sets: [fruit, vegis])
    hasSweet := Set.containsBy(set: fruit, value: "sweet", fields: ["taste"])
    sameTasteValues := Set.equalsBy(sets: [fruit, fruit], fields: ["taste"])
    fruitSubset := Set.subset(sets: [fruit, allFood])
    groupValidation := Group.validate(
        set: [0, 1],
        table: [
            {left: 0, right: 0, result: 0},
            {left: 0, right: 1, result: 1},
            {left: 1, right: 0, result: 1},
            {left: 1, right: 1, result: 0}
        ],
        identity: 0
    )
    abelianValidation := Group.abelian(
        set: [0, 1],
        table: [
            {left: 0, right: 0, result: 0},
            {left: 0, right: 1, result: 1},
            {left: 1, right: 0, result: 1},
            {left: 1, right: 1, result: 0}
        ],
        identity: 0
    )
    return (
        union_count: count(allFood),
        common_taste_count: count(commonTaste),
        fruit_only_taste_count: count(fruitOnlyTaste),
        exact_disjoint: disjointExact,
        has_sweet: hasSweet,
        same_tastes: sameTasteValues,
        fruit_subset: fruitSubset,
        group: groupValidation,
        abelian: abelianValidation.abelian,
        commutative: abelianValidation.commutative
    )
