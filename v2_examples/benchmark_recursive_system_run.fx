# A production benchmark fixture: recursive inference is issued through the
# language-native system.run API, not through a command-line query string.

Hypernym(child: "kitten", parent: "cat")
Hypernym(child: "cat", parent: "animal")
Hypernym(child: "animal", parent: "organism")

AncestorOf(descendant: descendant, ancestor: ancestor) =>
    Hypernym(child: descendant, parent: ancestor)
    return

AncestorOf(descendant: descendant, ancestor: ancestor) =>
    Hypernym(child: descendant, parent: parent)
    AncestorOf(descendant: parent, ancestor: ancestor)
    return

main() =>
    return system.run(value: "? AncestorOf(descendant: \"kitten\", ancestor: Ancestor)")
