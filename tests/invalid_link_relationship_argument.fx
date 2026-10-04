class Node
    key(id).
    def id: string.
end

def Node(id: "a").
def Node(id: "b").
Link(
    from: Node(id: "a"),
    to: Node(id: "b"),
    relationship: AssignedTo
).
