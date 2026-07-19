MakeProfile() =>
    return fn:tuple(first: "Alice", second: true, third: 2.5).

main() =>
    name: string, active: number, score: float := MakeProfile(),
    return (
        name: name,
        active: active,
        score: score
    ).
