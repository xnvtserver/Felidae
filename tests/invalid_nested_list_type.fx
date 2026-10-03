class Node
    key(id).
    id: string.
    coordinates: list<Pair<number, number>>.
end.

def main() =>
    return Node(id: "n1", coordinates: [Pair(1, "wrong")]).
end
