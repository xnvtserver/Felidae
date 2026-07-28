Stored(category: "known")

main() =>
    missing := Missing(category: "not-stored")
    stored := Stored(category: "known")
    return Relation.compare(left: missing, right: stored)
