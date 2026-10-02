@mixfix(
    pattern: "{left: number} conflictMerge {right: number}",
    result: number.class,
    visibility: "public"
)
def mergeFromB() =>
    return left - right
end
