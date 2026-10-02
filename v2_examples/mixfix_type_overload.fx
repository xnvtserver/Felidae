@mixfix(pattern: "{left: number} combine {right: number}")
def combineNumbers() =>
    return left + right
end

@overload(
    operator: combine.function,
    captures: {left: string, right: string},
    result: string.class
)
def combineStrings(left: string, right: string) =>
    return "string-overload"
end

def main() =>
    return (
        number_result: 40 combine 2,
        string_result: "left" combine "right"
    )
end
