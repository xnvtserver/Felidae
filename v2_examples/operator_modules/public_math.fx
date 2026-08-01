@overload(
    operator: blend,
    pattern: "{left} blend {right}",
    captures: {left: number, right: number},
    result: number,
    precedence: additive,
    associativity: left,
    cardinality: one,
    effects: pure,
    visibility: public
)
blendNumbers() =>
    return left / 2 + right / 2

@overload(
    operator: blend,
    pattern: "{left} difference {right}",
    captures: {left: number, right: number},
    result: number,
    precedence: additive,
    associativity: left,
    cardinality: one,
    effects: pure,
    visibility: public
)
differenceBetweenNumbers() =>
    return left / 2 - right / 2