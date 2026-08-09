# Direct hierarchy reasoning works on first-class fact values.  The runtime
# indexes the fact store, but this program never reaches through a fact.* API.

Root(name: "root")
Alpha extend Root(name: "alpha")
Beta extend Root(name: "beta")
Left extend Alpha, Beta(name: "left")
Right extend Alpha, Beta(name: "right")

main() =>
    lefts := lambda(Left, fact => fact.name == "left")
    rights := lambda(Right, fact => fact.name == "right")
    left := array.get(data: lefts, position: 0)
    right := array.get(data: rights, position: 0)

    common := commonAncestors(left: left, right: right)
    lowest := lowestCommonAncestor(left: left, right: right)
    highest := highestCommonAncestor(left: left, right: right)
    report := ancestorAnalysis(left: left, right: right)
    return (
        common: common,
        lowest: lowest,
        highest: highest,
        report: report
    )
