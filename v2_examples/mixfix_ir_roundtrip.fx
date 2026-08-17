# A strict-IR mixfix baseline: captures are explicit procedure parameters.
# It exercises the same Call IR path as a conventional method call.

@mixfix(pattern: "{left: number} combine {right: number}")
combineValue(left: number, right: number) =>
    return left + right

main() =>
    return 17 combine 25
