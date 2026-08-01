@overload(
    operator: requiredPositive,
    pattern: "requiredPositive {value}",
    type: prefix,
    captures: {value: number},
    result: number,
    precedence: prefix,
    associativity: right,
    cardinality: one,
    effects: pure,
    visibility: private
)
positiveOnly() =>
    where value > 0
    return value

main() =>
    return requiredPositive -1
