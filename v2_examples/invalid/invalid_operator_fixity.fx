@mixfix(
    pattern: "before {value: number}",
    type: "infix",
    result: number.class,
    precedence: "prefix",
    associativity: "right",
    cardinality: "one",
    effects: "pure",
    visibility: "private"
)
def malformed() =>
    return value
end
