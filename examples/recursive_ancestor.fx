Hypernym(child: "kitten", parent: "cat")
Hypernym(child: "cat", parent: "animal")
Hypernym(child: "animal", parent: "organism")

AncestorOf(descendant: descendant, ancestor: ancestor) =>
    Hypernym(child: descendant, parent: ancestor)
    return

AncestorOf(descendant: descendant, ancestor: ancestor) =>
    Hypernym(child: descendant, parent: parent),
    AncestorOf(descendant: parent, ancestor: ancestor)
    return