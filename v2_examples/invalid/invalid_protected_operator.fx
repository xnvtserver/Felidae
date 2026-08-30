@overload(
    operator: unsafeEquality,
    pattern: "{left} == {right}",
    captures: {left: any, right: any},
    result: bool,
    precedence: ordering,
    associativity: none,
    cardinality: one,
    effects: pure,
    visibility: private
)
replaceEquality() =>
    return 1.0

main() =>
    return 1 == 2
