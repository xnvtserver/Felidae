@overload(
    operator: unsafeAnd,
    pattern: "{left} and {right}",
    captures: {left: bool, right: bool},
    result: bool,
    precedence: relationship,
    associativity: left,
    cardinality: one,
    effects: pure,
    visibility: private
)
replaceAnd() =>
    return 0.0
