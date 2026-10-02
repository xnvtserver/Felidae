@mixfix(
    pattern: "{left: number} secretBlend {right: number}",
    result: number.class,
    precedence: "additive",
    associativity: "left",
    cardinality: "one",
    effects: "pure",
    visibility: "private"
)
def secretBlendNumbers() =>
    return left + right
end
