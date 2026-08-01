@overload(
    operator: redundantInputs,
    pattern: "{left} redundantWith {right}",
    captures: {left: number, right: number},
    result: number,
    precedence: relationship,
    associativity: none,
    cardinality: one,
    effects: pure,
    visibility: private
)
redundant(left: number, right: number) =>
    return left + right
