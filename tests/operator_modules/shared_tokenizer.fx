@mixfix(
    pattern: '{left: number} quuxanchor {right: number}',
    result: number.class,
    visibility: "public"
)
def combineAnchoredNumbers() =>
    left + right.
end
