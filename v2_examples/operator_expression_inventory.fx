import ("array", "system")

Increment(value: number) =>
    return value + 1

Double(value: number) =>
    return value * 2

Eligible(score: number, active: bool) =>
    where score >= 70
    where active == true
    return true
else
    return false

main() =>
    point := {x: 3, y: 4}
    record := {owner: {name: "Ava"}, scores: [10, 20, 30]}
    mapped := lambda(record.scores, score => score + point.x)
    first := array.get(data: mapped, position: 0)
    piped := Increment(value: first)
        then Double(value: system.result)
    return (
        precedence: 2 + 3 * 4,
        grouped: (2 + 3) * 4,
        unary: -(point.x + point.y),
        ordered: point.y > point.x,
        unequal: point.x != point.y,
        eligible: Eligible(score: 75, active: true),
        member: record.owner.name,
        mapped: mapped,
        pipeline: piped
    )
