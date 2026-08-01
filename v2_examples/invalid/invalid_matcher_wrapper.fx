WrappedRequirement extend OperatorRequirement(value: number)

@overload(
    operator: wrappedContext,
    pattern: "{left} wrappedContext {right}",
    captures: {left: number, right: number},
    factor: wrapped: WrappedRequirement,
    result: number
)
useWrappedRequirement() =>
    return wrapped.value

@matcher(
    operator: wrappedContext,
    captures: {left: number, right: number},
    produces: [wrapped: WrappedRequirement]
)
invalidWrapper() =>
    return WrappedRequirement(value: left)
