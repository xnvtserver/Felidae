@mixfix(
    pattern: '{left: number} blend {right: number}',
    result: number.class,
    precedence: "additive",
    associativity: "left",
    cardinality: "one",
    effects: "pure",
    visibility: "public"
)
def blendNumbers() =>
    left / 2 + right / 2.
end

@mixfix(
    pattern: '{left: number} difference {right: number}',
    result: number.class,
    precedence: "additive",
    associativity: "left",
    cardinality: "one",
    effects: "pure",
    visibility: "public"
)
def differenceBetweenNumbers() =>
    left / 2 - right / 2.
end
