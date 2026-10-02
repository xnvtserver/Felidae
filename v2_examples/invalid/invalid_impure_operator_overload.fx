@mixfix(
    pattern: "{left: number} noisyAdd {right: number}",
    result: number.class,
    effects: "pure"
)
def noisyAddNumbers() =>
    system.print(value: left)
    return left + right
end
