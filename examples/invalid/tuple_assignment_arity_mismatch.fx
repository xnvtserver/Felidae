MakeProfile() =>
    return fn:tuple(first: "Alice", second: true, third: 2.5).

main() =>
    name: string, active: bool := MakeProfile(),
    return (
        name: name,
        active: active
    ).
