@overload(
    operator: conflictMerge,
    pattern: "{left} conflictMerge {right}",
    captures: {left: number, right: number},
    result: number,
    visibility: public
)
mergeFromB() =>
    return left - right
