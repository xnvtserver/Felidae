import "operator_modules/public_score.fx"

@overload(
    operator: scoreWith,
    pattern: "{left} scoreWith {right}",
    captures: {left: number, right: number},
    result: string,
    visibility: private
)
privateScore() =>
    return "private"

main() =>
    return 8 scoreWith 3
