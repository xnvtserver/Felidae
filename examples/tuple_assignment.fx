MakeProfile() =>
    return fn:tuple(first: "Alice", second: true, third: 2.5).

MakeFlags() =>
    "left" == "left",
    "right" != "left",
    2 < 3.

main() =>
    name: string, active: bool, score: float := MakeProfile(),
    first: bool, second: bool, third: bool := MakeFlags(),
    rawFirst, rawSecond, rawThird := MakeFlags(),
    
    return (
        name: name,
        active: active,
        score: score,
        flags: fn:tuple(first: first, second: second, third: third),
        raw_flags: fn:tuple(first: rawFirst, second: rawSecond, third: rawThird)
    ).
