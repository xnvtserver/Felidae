BoundaryRequirement extend OperatorRequirement(value: number)

@overload(
    operator: invalidBoundary,
    pattern: "{left} invalidBoundary {right}",
    captures: {left: number, right: number},
    factors: [lower: BoundaryRequirement, upper: BoundaryRequirement],
    result: number
)
invalidBoundaries() =>
    return left
