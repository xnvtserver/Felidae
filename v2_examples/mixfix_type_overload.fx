@mixfix(pattern: '{left: number} combine {right: number}')
def combineNumbers() =>
    left + right.
end

@overload(
    operator: combine.function,
    captures: [left: string, right: string],
    result: string.class
)
def combineStrings(left: string, right: string) =>
    "string-overload".
end

def main() =>
    (
        number_result: 40 combine 2,
        string_result: "left" combine "right"
    ).
end
