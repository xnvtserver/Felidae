Source(category: "base")
Target(category: "base")

Source.membership(input: Source, against: Fact) =>
    return true

main() =>
    source := Source(category: "base")
    target := Target(category: "base")
    return Relation.compare(left: source, right: target)
