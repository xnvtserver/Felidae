class Node
    key(id)
    id: string
end

Node(id: "a")
Node(id: "b")

def main() =>
    Link(Node(id: "a"), Node(id: "b")),
    return Link(Node(id: "a"), Node(id: "b"))
end
