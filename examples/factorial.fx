
#factorial 

Factorial(n: int) =>
    n == 0,
        return 1
    else
        return n * Factorial(n: n - 1).

main() =>
    result := Factorial(n: 5),
    system.print(value: result),
    return (
        result: result
    ).