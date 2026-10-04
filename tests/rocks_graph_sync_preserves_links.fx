import "db".

class Node
    key(id).
    def id: string.
    def label: string.
end

def main() =>
    def first_sync := db.sync(path: "rocks_graph_sync_preserves_links_source.fx").
    Link(
        from: Node(id: "a"),
        to: Node(id: "b"),
        properties: {kind: "connected"}
    ).
    def changed := Node.where(id: "a").update(values: {label: "modified"}).
    def second_sync := db.sync(path: "rocks_graph_sync_preserves_links_source.fx").
    def links := Node().where(id: "a").join(
        properties: {kind: "connected"},
        direction: forward.class
    ).
    return (
        first_sync: first_sync,
        changed: changed,
        second_sync: second_sync,
        links: links.len(),
        source_label: links.get(position: 0).left.label
    ).
end
