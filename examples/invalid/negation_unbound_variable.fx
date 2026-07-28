Bird(name: "sparrow")

Bad() =>
    not Bird(name: name)
    return

main() =>
    Bad()
    return "unreachable"
