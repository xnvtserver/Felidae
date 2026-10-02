class Node
    key(id)
    id: string
end

def main() =>
    numbers: list<number> := [1, 2, 3]
    pairs: list<Pair<number, number>> := [Pair(1, 2), Pair(3, 4)]
    node: obj := Node(id: "node-1")
    maybe: optional<string> := nil
    payload: any := pairs
    return (
        number_count: numbers.len(),
        pair_count: pairs.len(),
        node_id: node.id,
        missing: maybe,
        payload_count: payload.len()
    )
end
