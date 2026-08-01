OperatorRequirement()
VisibilityRequirement extend OperatorRequirement(value: number)

@overload(
    operator: privateVisibilityCheck,
    pattern: "{left} visibilityCheck {right}",
    captures: {left: number, right: number},
    result: number,
    precedence: relationship,
    associativity: none,
    cardinality: one,
    effects: pure,
    visibility: private
)
privateCheck() =>
    return left

@matcher(
    operator: privateVisibilityCheck,
    captures: {left: number, right: number},
    produces: [requirement: VisibilityRequirement],
    visibility: public
)
publicMatcher() =>
    return RequirementMatch(
        requirement: VisibilityRequirement(value: left.literalValue)
    )
