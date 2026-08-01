NumericRequirement extend OperatorRequirement(
    left: number,
    right: number
)

@overload(
    operator: contextualAdd,
    pattern: "{left} contextualAdd {right}",
    captures: {left: number, right: number},
    factor: numericRequirement: NumericRequirement,
    result: number,
    cardinality: one,
    effects: pure
)
addNumericLiterals() =>
    return numericRequirement.left + numericRequirement.right

@matcher(
    operator: contextualAdd,
    pattern: "{left} contextualAdd {right}",
    captures: {left: number, right: number},
    produces: [numericRequirement: NumericRequirement]
)
matchNumericLiterals() =>
    where context.pattern == "{left} contextualAdd {right}"
    where context.precedence == 40
    where left.literalKind == "number"
    where left.sourceSpan.startLine > 0
    where right.literalKind == "number"
    return RequirementMatch(
        numericRequirement: NumericRequirement(left: left, right: right)
    )

main() =>
    return 2 contextualAdd 3
