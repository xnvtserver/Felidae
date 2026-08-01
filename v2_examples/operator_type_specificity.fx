Parent(name: "parent", value: 3)
Child extend Parent(name: "child-a", value: 1)
Child(name: "child-b", value: 2)

@overload(
    operator: affinity,
    pattern: "{left} affinity {right}",
    captures: {left: Parent, right: Parent},
    result: string,
    precedence: relationship,
    associativity: none,
    cardinality: one,
    effects: pure,
    visibility: private
)
parentAffinity() =>
    return "parent"

@overload(
    operator: affinity,
    captures: {left: Child, right: Child},
    result: string,
    cardinality: one,
    effects: pure,
    visibility: private
)
childAffinity() =>
    return "child"

main() =>
    childA := Child(name: "child-a", value: 1)
    childB := Child(name: "child-b", value: 2)
    childC := Child(name: "child-c", value: 2)
    parent := Parent(name: "parent", value: 3)
    return (
        exact: childA affinity childB,
        inherited: childA affinity parent
    )
