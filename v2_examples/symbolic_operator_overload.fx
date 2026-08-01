@overload(
    operator: mergeValue,
    pattern: "{left} ^^^ {right}",
    captures: {left: number, right: number},
    result: number,
    precedence: additive,
    associativity: left,
    cardinality: one,
    effects: pure,
    visibility: private
)
mergeNumbers() =>
    return left + right

main() =>
    return 2 ^^^ 3 ^^^ 4
