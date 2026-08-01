mainCall() =>
    return 2 lateCombine 3

mainCall()

@overload(
    operator: lateCombine,
    pattern: "{left} lateCombine {right}",
    captures: {left: number, right: number},
    result: number
)
combineLate() =>
    return left + right
