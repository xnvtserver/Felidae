NumericRequirement extend OperatorRequirement(value: number)

def unsafeCheck() =>
    return 1.0
end

@mixfix(
    pattern: "{left: number} unsafeContext {right: number}",
    factor: numericRequirement: NumericRequirement,
    result: number.class
)
def unsafeContextNumbers() =>
    return numericRequirement.value
end

@matcher(
    operator: unsafeContext.function,
    captures: {left: number, right: number},
    produces: [numericRequirement: NumericRequirement]
)
def matchWithRuntimeCall() =>
    where unsafeCheck() == 1.0
    return RequirementMatch(
        numericRequirement: NumericRequirement(value: left)
    )
end
