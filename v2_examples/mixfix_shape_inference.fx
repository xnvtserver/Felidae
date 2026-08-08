# @mixfix derives the operator name and fixity from the typed pattern.

@mixfix(
    pattern: "{left: number} combine {right: number}"
)
combineValue() =>
    return left + right

main() =>
    return 2 combine 3
