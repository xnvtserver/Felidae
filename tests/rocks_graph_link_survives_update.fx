class Node
    key(id).
    def id: string.
    def label: string.
end

def Node(id: "a", label: "before").
def Node(id: "b", label: "target").
Link(from: Node(id: "a"), to: Node(id: "b"), properties: {kind: "connected"}).

def main() =>
    def changed := Node.where(id: "a").update(values: {label: "after"}).
    def links := Node.where(id: "a").join(
        properties: {kind: "connected"},
        direction: forward.class
    ).
    (
        changed: changed,
        source: links.get(position: 0).left.label,
        target: links.get(position: 0).right.label
    ).
end
