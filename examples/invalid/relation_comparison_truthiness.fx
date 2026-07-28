Source(category: "base")
Target(category: "base")

main() =>
    source := Source(category: "base")
    target := Target(category: "base")
    comparison := Relation.compare(left: source, right: target)
    where comparison
    return comparison
