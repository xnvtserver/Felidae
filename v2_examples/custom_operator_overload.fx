@overload(
    operator: combine,
    pattern: "{left} combine {right}",
    captures: {left: number, right: number},
    result: number,
    precedence: additive,
    associativity: left,
    cardinality: one,
    effects: pure,
    visibility: private
)
combineNumbers() =>
    return left + right

main() =>
    combined := 2 combine 3
    chained := 1 combine 2 combine 4
    next_chained := 1 combine 2 combine 4 combine 5 combine 2 combine 4 combine 5 combine 2 combine 4 combine 5 combine 2 combine 4 combine 5 combine 2 combine 4 combine 5 combine 2 combine 4 combine 5 combine 2 combine 4 combine 5 combine 2 combine 4 combine 5 combine 2 combine 4 combine 5
    return (combined: combined, chained: chained, next_chained: next_chained)
