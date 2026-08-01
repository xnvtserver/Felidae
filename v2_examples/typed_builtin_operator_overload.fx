Element(name: "first", x: 2, y: 3)
Element(name: "second", x: 5, y: 7)

@overload(
    operator: add,
    pattern: "{left} + {right}",
    captures: {left: Element, right: Element},
    result: Element,
    precedence: additive,
    associativity: left,
    cardinality: one,
    effects: pure,
    visibility: private
)
addElements() =>
    return Element(
        name: "combined",
        x: left.x + right.x,
        y: left.y + right.y
    )

main() =>
    first := Element(name: "first", x: 2, y: 3)
    second := Element(name: "second", x: 5, y: 7)
    combined := first + second
    numeric := 2 + 3
    return (combined: combined, numeric: numeric)
