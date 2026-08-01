@overload(
    operator: requirementValue,
    pattern: "Requirement {value}",
    type: prefix,
    captures: {value: number},
    result: number,
    precedence: prefix,
    associativity: right,
    cardinality: one,
    effects: pure,
    visibility: private
)
incrementRequirement() =>
    return value + 1

@overload(
    operator: classify,
    pattern: "{left} classify {requirement}",
    captures: {left: number, requirement: number},
    result: number,
    precedence: relationship,
    associativity: none,
    cardinality: one,
    effects: pure,
    visibility: private
)
classifyUsingRequirement() =>
    return left + requirement

main() =>
    return 2 classify Requirement 4
