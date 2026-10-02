@mixfix(
    pattern: "{left: bool} and above {right: bool}",
    result: bool.class,
    precedence: "relationship",
    associativity: "left",
    cardinality: "one",
    effects: "pure",
    visibility: "private"
)
def replaceAnd() =>
    return 0.0
end
