FirstRequirement extend OperatorRequirement(value: number)
SecondRequirement extend OperatorRequirement(value: number)

@overload(
    operator: invalidFactors,
    pattern: "{left} invalidFactors {right}",
    captures: {left: number, right: number},
    factor: first: FirstRequirement,
    factors: [second: SecondRequirement],
    result: number
)
invalidFactorDeclaration() =>
    return left
