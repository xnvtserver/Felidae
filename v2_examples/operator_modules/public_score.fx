@overload(
    operator: scoreWith,
    pattern: "{left} scoreWith {right}",
    captures: {left: number, right: number},
    result: string,
    visibility: public
)
publicScore() =>
    return "public"
