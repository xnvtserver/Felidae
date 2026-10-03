BoundaryRequirement extend OperatorRequirement(value: number)

@mixfix(
    pattern: "{left: number} invalidBoundary {right: number}",
    factors: [lower: BoundaryRequirement, upper: BoundaryRequirement],
    result: number.class
)
def invalidBoundaries() =>
    return left
end
