BaseRequirement extend OperatorRequirement(value: number)
SpecificRequirement extend BaseRequirement(value: number)

@overload(
    operator: classifyRequirement,
    pattern: "{left} classifyRequirement {right}",
    captures: {left: number, right: number},
    factor: requirement: SpecificRequirement,
    result: string
)
exactRequirement() =>
    return "exact"

@overload(
    operator: classifyRequirement,
    captures: {left: number, right: number},
    factor: requirement: BaseRequirement,
    result: string
)
parentRequirement() =>
    return "parent"

@overload(
    operator: classifyRequirement,
    captures: {left: number, right: number},
    factor: requirement: OperatorRequirement,
    result: string
)
genericRequirement() =>
    return "generic"

@matcher(
    operator: classifyRequirement,
    captures: {left: number, right: number},
    produces: [specific: SpecificRequirement]
)
matchSpecificRequirement() =>
    return RequirementMatch(
        specific: SpecificRequirement(value: left + right)
    )

main() =>
    return 2 classifyRequirement 3
