# Deep, executable mixfix nesting. Every annotation has an explicit method
# parameter list, so nested captures lower to ordinary Call IR rather than an
# interpreter-only implicit-capture environment.

@mixfix(pattern: "{left: number} blend {right: number}")
blend(left: number, right: number) =>
    return left + right

@mixfix(pattern: "combine {left: number} with {right: number}")
combine(left: number, right: number) =>
    return left * right

@mixfix(pattern: "scale {value: number} by {factor: number}")
scale(value: number, factor: number) =>
    return value * factor

@mixfix(pattern: "offset {value: number} by {amount: number}")
offset(value: number, amount: number) =>
    return value + amount

main() =>
    leaf := 4 blend 5
    branch := combine (1 blend 2) with (3 blend leaf)
    deep := scale (combine branch with (offset (2 blend 3) by (1 blend 1))) by (2 blend 1)
    return (leaf: leaf, branch: branch, deep: deep)
