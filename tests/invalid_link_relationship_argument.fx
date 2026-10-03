class Node
    key(id).
    id: string.
end

Node(id: "a").
Node(id: "b").
Link(
    from: Node(id: "a"),
    to: Node(id: "b"),
    relationship: AssignedTo
).
