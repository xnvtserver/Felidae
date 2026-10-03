OperatorRequirement()
VisibilityRequirement extend OperatorRequirement(value: number)

@mixfix(
    pattern: "{left: number} visibilityCheck {right: number}",
    result: number.class,
    precedence: "relationship",
    associativity: "none",
    cardinality: "one",
    effects: "pure",
    visibility: "private"
)
def privateCheck() =>
    return left
end

@matcher(
    operator: visibilityCheck.function,
    captures: {left: number, right: number},
    produces: [requirement: VisibilityRequirement],
    visibility: "public"
)
def publicMatcher() =>
    return RequirementMatch(
        requirement: VisibilityRequirement(value: left.literalValue)
    )
end
