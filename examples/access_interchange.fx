SampleAccess(
    nested: {
        left: {
            right: {
                value: 42
            }
        }
    }
).

ReadAccess(input: any, value: int) =>
    dotValue := input.nested.left.right.value,
    colonValue := input:nested:left:right:value,
    mixedValue := input.nested:left.right:value,
    value == dotValue,
    value == colonValue,
    value == mixedValue,
    return (
        dot: dotValue,
        colon: colonValue,
        mixed: mixedValue
    ).

ReturnInt(value: int) =>
    return (value).

main() =>
    item := SampleAccess(
        nested: {
            left: {
                right: {
                    value: 42
                }
            }
        }
    ),
    access := ReadAccess(input: item, value: 42),
    direct := ReturnInt(value: 42),
    return (
        dot: item.nested.left.right.value,
        colon: item:nested:left:right:value,
        mixed: item.nested:left.right:value,
        direct: direct,
        checked: access
    ).
