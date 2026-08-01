Choice(value: 1)
Choice(value: 2)
Choice(value: 3)

@overload(
    operator: choicesThrough,
    pattern: "choices through {limit}",
    type: prefix,
    captures: {limit: number},
    result: number,
    precedence: prefix,
    associativity: right,
    cardinality: many,
    effects: pure,
    visibility: private
)
collectChoices() =>
    Choice(value: choice)
    where choice <= limit
    return choice

@overload(
    operator: maybeAbove,
    pattern: "{value} maybeAbove {minimum}",
    captures: {value: number, minimum: number},
    result: number,
    precedence: relationship,
    associativity: none,
    cardinality: optional,
    effects: pure,
    visibility: private
)
keepWhenAbove() =>
    where value > minimum
    return value

main() =>
    return (
        many: choices through 2,
        some: 7 maybeAbove 5,
        none: 2 maybeAbove 5
    )
