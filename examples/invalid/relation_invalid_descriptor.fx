Source(id: 1)
Target(id: 2)

main() =>
    source := Source(id: 1)
    target := Target(id: 2)
    source.relate(to: target, as: {name: "not-a-relationship"})
    return source
