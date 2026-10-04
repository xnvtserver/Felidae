class Node
    key(id).
    def id: string.
end

def main() =>
    def numbers: list<number> := [1, 2, 3].
    def pairs: list<Pair<number, number>> := [Pair(1, 2), Pair(3, 4)].
    def node: obj := Node(id: "node-1").
    def maybe: optional<string> := nil.
    def payload: any := pairs.
    return (
        number_count: numbers.len(),
        pair_count: pairs.len(),
        node_id: node.id,
        missing: maybe,
        payload_count: payload.len()
    ).
end
