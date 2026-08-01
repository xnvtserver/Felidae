@overload(
    operator: transformWith,
    pattern: "{value} transform {model} with {count}",
    type: mixfix,
    captures: {value: number, model: number, count: number},
    result: number,
    precedence: relationship,
    associativity: none,
    cardinality: one,
    effects: pure,
    visibility: private
)
transformValue() =>
    return value + model * count

main() =>
    result := 2 transform 3 with 4
    nested := (1 + 1) transform (2 + 1) with 2
    return (result: result, nested: nested)
