@mixfix(
    pattern: "{left: number} quuxanchor {right: number}",
    result: number.class,
    visibility: "public"
)
def combineAnchoredNumbers() =>
    return left + right.
end
