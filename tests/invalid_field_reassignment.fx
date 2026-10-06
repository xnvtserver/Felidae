class Node
    key(id).
    def id: string.
    def label: string.
end

def main() =>
    def node := Node(id: "n1", label: "before").
    node.label := "after".
    node.
end
