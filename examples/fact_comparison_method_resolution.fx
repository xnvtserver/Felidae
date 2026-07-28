# Proves that comparison dispatch stays on normal methods: a source ancestor
# supplies membership while the target has no custom interpretation, so the
# built-in structural result is used directly.

Root(category: "base")
Child extend Root(category: "base", detail: "child")
Target(category: "base")

Root.membership(input: Root, against: Target) =>
    return {category: input.category}

main() =>
    child := Child(category: "base", detail: "child")
    target := Target(category: "base")
    return Relation.compare(left: child, right: target)
