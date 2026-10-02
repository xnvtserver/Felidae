class Node
    key(id)
    id: string
end

class Seen
    key(id)
    id: string
end

Node(id: "a")
Node(id: "b")
Node(id: "c")
Link(from: Node(id: "a"), to: Node(id: "b"), properties: {kind: "edge"})
Link(from: Node(id: "a"), to: Node(id: "c"), properties: {kind: "edge"})

def main() =>
    rows := Node.where(id: "a").join(
        properties: {kind: "edge"},
        direction: forward.class
    )
    edge_count := rows.count()
    for row in rows then
        Seen.insert(values: {id: row.right.id})
    end
    return (edge_count: edge_count, seen_count: Seen.count())
end
