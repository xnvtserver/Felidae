# A call to a name that is not a native builtin, not a declared method or
# clause, and not capitalized (so not a fact/class literal either) must fail
# loudly. This used to silently "succeed": evalBuiltinTerm's final fallback
# treated *any* unresolved call as an ad-hoc fact literal of that name,
# regardless of capitalization, so a plain typo'd or unimported lowercase
# function name printed itself back (e.g. `totallyUndefinedFunction(x: 1)`)
# as if it were a legitimate return value instead of erroring.
def main() =>
    return totallyUndefinedFunction(x: 1)
