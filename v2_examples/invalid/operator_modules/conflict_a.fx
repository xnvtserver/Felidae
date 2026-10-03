@mixfix(
    pattern: "{left: number} conflictMerge {right: number}",
    result: number.class,
    visibility: "public"
)
def mergeFromA() =>
    return left + right
end
