class Node
    key(id)
    id: string
    label: string
end

def main() =>
    node := Node(id: "n1", label: "before")
    node.label := "after"
    return node
end
