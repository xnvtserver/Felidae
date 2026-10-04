class Node
    key(id).
    def id: string.
end

class Seen
    key(id).
    def id: string.
end

def Node(id: "a").
def Node(id: "b").
def Node(id: "c").
Link(from: Node(id: "a"), to: Node(id: "b"), properties: {kind: "edge"}).
Link(from: Node(id: "a"), to: Node(id: "c"), properties: {kind: "edge"}).

def main() =>
    def rows := Node.where(id: "a").join(
        properties: {kind: "edge"},
        direction: forward.class
    ).
    def edge_count := rows.count().
    for row in rows then
        Seen.insert(values: {id: row.right.id}).
    end
    return (edge_count: edge_count, seen_count: Seen.count()).
end
