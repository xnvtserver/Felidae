# Direct hierarchy reasoning works on first-class fact values.  The runtime
# indexes the fact store, but this program never reaches through a fact.* API.

Root(name: "root")
Alpha extend Root(name: "alpha")
Beta extend Root(name: "beta")
Left extend Alpha, Beta(name: "left")
Right extend Alpha, Beta(name: "right")

main() =>
    left := Left(name: "left")
    right := Right(name: "right")

    common := commonAncestors(left: left, right: right)
    lowest := lowestCommonAncestor(left: left, right: right)
    highest := highestCommonAncestor(left: left, right: right)
    return (
        common: common,
        lowest: lowest,
        highest: highest
    )
