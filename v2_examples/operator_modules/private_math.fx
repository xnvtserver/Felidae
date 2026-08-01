@overload(
    operator: secretBlend,
    pattern: "{left} secretBlend {right}",
    captures: {left: number, right: number},
    result: number,
    precedence: additive,
    associativity: left,
    cardinality: one,
    effects: pure,
    visibility: private
)
secretBlendNumbers() =>
    return left + right
