@overload(
    operator: shorterThan,
    pattern: "{left} < {right}",
    captures: {left: string, right: string},
    result: bool,
    precedence: ordering,
    associativity: none,
    cardinality: one,
    effects: pure,
    visibility: private
)
compareStringLength() =>
    return left == "cat"

main() =>
    return (
        custom: "cat" < "tiger",
        builtin: 8 < 3
    )
