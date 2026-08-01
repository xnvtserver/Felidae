@overload(
    operator: conflictMerge,
    pattern: "{left} conflictMerge {right}",
    captures: {left: number, right: number},
    result: number,
    visibility: public
)
mergeFromA() =>
    return left + right
