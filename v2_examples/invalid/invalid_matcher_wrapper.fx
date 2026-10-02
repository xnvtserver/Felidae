WrappedRequirement extend OperatorRequirement(value: number)

@mixfix(
    pattern: "{left: number} wrappedContext {right: number}",
    factor: wrapped: WrappedRequirement,
    result: number.class
)
def useWrappedRequirement() =>
    return wrapped.value
end

@matcher(
    operator: wrappedContext.function,
    captures: {left: number, right: number},
    produces: [wrapped: WrappedRequirement]
)
def invalidWrapper() =>
    return WrappedRequirement(value: left)
end
