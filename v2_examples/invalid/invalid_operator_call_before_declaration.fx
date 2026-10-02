def mainCall() =>
    return 2 lateCombine 3
end

mainCall()

@mixfix(
    pattern: "{left: number} lateCombine {right: number}",
    result: number.class
)
def combineLate() =>
    return left + right
end
