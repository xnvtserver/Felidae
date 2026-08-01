NumericRequirement extend OperatorRequirement(value: number)

unsafeCheck() =>
    return true

@overload(
    operator: unsafeContext,
    pattern: "{left} unsafeContext {right}",
    captures: {left: number, right: number},
    factor: numericRequirement: NumericRequirement,
    result: number
)
unsafeContextNumbers() =>
    return numericRequirement.value

@matcher(
    operator: unsafeContext,
    pattern: "{left} unsafeContext {right}",
    captures: {left: number, right: number},
    produces: [numericRequirement: NumericRequirement]
)
matchWithRuntimeCall() =>
    where unsafeCheck() == true
    return RequirementMatch(
        numericRequirement: NumericRequirement(value: left)
    )
