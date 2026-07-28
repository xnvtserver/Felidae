First(id: "first")
Second(id: "second")

main() =>
    first := First(id: "first")
    second := Second(id: "second")
    first.depends(on: second)
    second.depends(on: first)
    return Relation.compare(left: first, right: second)
