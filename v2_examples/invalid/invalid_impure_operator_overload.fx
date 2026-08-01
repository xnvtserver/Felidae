@overload(
    operator: noisyAdd,
    pattern: "{left} noisyAdd {right}",
    captures: {left: number, right: number},
    result: number,
    effects: pure
)
noisyAddNumbers() =>
    system.print(value: left)
    return left + right
