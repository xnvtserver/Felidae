import "system"

Increment(value: number) =>
    return value + 1

Double(value: number) =>
    return value * 2

Stop(value: any) =>
    value == value
    return nil

Explode(value: any) =>
    return value / 0

Wrap(value: any) =>
    return (
        seen: value,
        tag: "wrapped"
    )

UseNested(value: number) =>
    inner := Increment(value: value)
        then Double(value: system.result)
    return inner + 3

main() =>
    direct := Increment(value: 1)
        then Double(value: system.result)
        then Wrap(value: system.result)
    nested := UseNested(value: 4)
        then Wrap(value: system.result)
    stopped := Increment(value: 1)
        then Stop(value: system.result)
        then Explode(value: system.result)
    arithmeticPrecedence := Increment(value: 1)
        then Double(value: (system.result) + 3)
    return (
        direct: direct,
        nested: nested,
        stopped: stopped,
        arithmeticPrecedence: arithmeticPrecedence
    )
