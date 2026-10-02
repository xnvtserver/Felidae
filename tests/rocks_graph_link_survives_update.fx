class Node
    key(id)
    id: string
    label: string
end

Node(id: "a", label: "before").
Node(id: "b", label: "target").
Link(from: Node(id: "a"), to: Node(id: "b"), properties: {kind: "connected"})

def main() =>
    changed := Node:where(id: "a"):update(values: {label: "after"})
    links := Node:where(id: "a"):join(
        properties: {kind: "connected"},
        direction: forward.class
    ),
    return (
        changed: changed,
        source: links.get(position: 0).left.label,
        target: links.get(position: 0).right.label
    )
end
