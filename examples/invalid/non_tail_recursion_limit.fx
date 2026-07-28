# Non-tail calls must fail as a Felidae resource error rather than exhausting
# the native process stack.
Countdown(value: number) =>
    if value == 0 then
        return 0
    else
        return 1 + Countdown(value: value - 1)

main() =>
    return Countdown(value: 100)
