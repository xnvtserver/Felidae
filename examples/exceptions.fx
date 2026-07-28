# Recoverable failures are normal Result values. There is no try/catch:
# application code explicitly checks `ok` and calls its own recovery method.

import "exception"

# These kinds belong to this program, not to core/exception.fx.
CalculatorActions.handle(result: Result) =>
    if result.error.kind == "DivisionByZero" then
        return exception.ok(value: {
            quotient: 0,
            recovered: true,
            reason: result.error.message
        })
    else
        if result.error.kind == "UnsupportedOperation" then
            return exception.ok(value: {
                quotient: nil,
                recovered: true,
                reason: "Choose a supported calculator operation"
            })
        else
            return result

SafeDivide(dividend: number, divisor: number) =>
    if divisor == 0 then
        rejected := exception.failure(
            kind: "DivisionByZero",
            message: "Cannot divide by zero",
            source: "calculator"
        )
        return CalculatorActions.handle(result: rejected)
    else
        quotient := math.div(lhs: dividend, rhs: divisor)
        return exception.ok(value: {quotient: quotient, recovered: false})

UnsupportedCalculatorOperation() =>
    rejected := exception.failure(
        kind: "UnsupportedOperation",
        message: "The selected calculator operation is unavailable",
        source: "calculator"
    )
    return CalculatorActions.handle(result: rejected)

main() =>
    normal := SafeDivide(dividend: 12, divisor: 3)
    recovered := SafeDivide(dividend: 12, divisor: 0)
    unsupported := UnsupportedCalculatorOperation()
    return {normal: normal, recovered: recovered, unsupported: unsupported}
