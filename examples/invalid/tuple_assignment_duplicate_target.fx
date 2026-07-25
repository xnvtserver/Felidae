MakeProfile() =>
    return fn:tuple(first: "Alice", second: true)

main() =>
    name: string, name: bool := MakeProfile()
    return (
        name: name
    )
