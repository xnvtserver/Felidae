@overload(
    operator: malformedPrefix,
    pattern: "before {value}",
    type: infix,
    captures: {value: number},
    result: number,
    precedence: prefix,
    associativity: right,
    cardinality: one,
    effects: pure,
    visibility: private
)
malformed() =>
    return value
