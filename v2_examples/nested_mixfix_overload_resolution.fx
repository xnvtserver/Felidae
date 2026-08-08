# Structural mixfix captures are more specific than broad expr captures.

@mixfix(pattern: "{left: string} build {right: string}")
buildValue() =>
    return Built(value: str.concat(left: str.concat(left: left, right: ":"), right: right))

@mixfix(pattern: "{left: string} label {right: string}")
labelValue() =>
    return Label(value: str.concat(left: str.concat(left: left, right: ":"), right: right))

@mixfix(pattern: "{left: string} wraps {right: mixfix}")
nestedVersion() =>
    return Resolution(kind: "mixfix", value: right)

@mixfix(pattern: "{left: string} wraps {right: expr}")
expressionVersion() =>
    return Resolution(kind: "expr", value: type(right))

@mixfix(pattern: "combine {left: mixfix} with {right: mixfix}")
combineNested() =>
    return Pair(left: left, right: right)

@mixfix(pattern: "{left: string} accepts {right: expr}")
fallbackExpression() =>
    return Resolution(kind: "fallback", value: right)

forward(value: expr) =>
    return "outer" wraps value

main() =>
    direct := "outer" wraps ("first" build "one")
    alternate := "outer" wraps ("second" label "two")
    plain := "outer" wraps "value"
    bound := forward(value: ("bound" build "three"))
    deep := "outer" wraps ("middle" wraps ("deep" build "four"))
    branches := combine ("left" build "five") with ("right" label "six")
    fallbackResult := "outer" accepts ("fallback" build "seven")
    return (
        direct: direct,
        alternate: alternate,
        plain: plain,
        bound: bound,
        deep: deep,
        branches: branches,
        fallback_result: fallbackResult
    )
