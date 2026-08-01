@overload(
    operator: chooseOtherwise,
    pattern: "choose {left} otherwise {right}",
    type: mixfix,
    captures: {left: number, right: number},
    result: number,
    precedence: relationship,
    associativity: right,
    cardinality: one,
    effects: pure,
    visibility: private
)
chooseLarger() =>
    if left > right then
        return left
    else
        return right

main() =>
    return choose 3 otherwise choose 5 otherwise 4
