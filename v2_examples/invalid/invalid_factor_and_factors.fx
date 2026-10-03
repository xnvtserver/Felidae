FirstRequirement extend OperatorRequirement(value: number)
SecondRequirement extend OperatorRequirement(value: number)

@mixfix(
    pattern: "{left: number} invalidFactors {right: number}",
    factor: first: FirstRequirement,
    factors: [second: SecondRequirement],
    result: number.class
)
def invalidFactorDeclaration() =>
    return left
end
