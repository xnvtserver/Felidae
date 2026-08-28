Choice(value: 1)
Choice(value: 2)
Choice(value: 3)

# @mixfix derives operator name and fixity from the pattern shape alone --
# no explicit type/precedence/associativity/cardinality. Ambiguous
# compositions, if any, are resolved by the compiler's SSM fallback, not by
# hand-written metadata (see IntegerParser::resolveModelMixfixMethod).
@mixfix(pattern: "choices through {limit: number}")
collectChoices() =>
    Choice(value: choice)
    where choice <= limit
    return choice

@mixfix(pattern: "{value: number} may be Above {minimum: number}")
keepWhenAbove() =>
    where value > minimum
    return value

main() =>
    return (
        many: choices through 2,
        some: 7 may be Above 5,
        none: 2 may be Above 5
    )
