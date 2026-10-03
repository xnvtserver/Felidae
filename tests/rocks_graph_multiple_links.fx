class Node
    key(id).
    id: string.
    label: string.
end

class Department
    key(id).
    id: string.
end

class Project
    key(id).
    id: string.
end

Node(id: "a", label: "source").
Node(id: "b", label: "first").
Node(id: "c", label: "second").
Department(id: "department-1").
Project(id: "project-1").

Link(Node(id: "a"), Node(id: "b"), {kind: "connected"}).
Link(Node(id: "a"), Node(id: "b"), {kind: "connected"}).
Link(Node(id: "a"), Node(id: "c"), {kind: "connected"}).
Link(from: Node(id: "a"), to: Department(id: "department-1"), properties: {kind: "assigned"}).
Link(from: Node(id: "a"), to: Project(id: "project-1"), properties: {kind: "assigned"}).
Link(from: Node(id: "a"), to: Node(id: "c"), properties: {kind: "reverse_example"}).

def main() =>
    all_links := Node().where(id: "a").join(direction: forward.class).
    connected := Node().where(id: "a")
        .join(properties: {kind: "connected"}, direction: forward.class).
    assigned := Node().where(id: "a")
        .join(properties: {kind: "assigned"}, direction: forward.class).
    reversed := Node().where(id: "a")
        .join(properties: {kind: "reverse_example"}, direction: forward.class).
    key_match := Node().where(id: "a")
        .join(properties: {kind: "connected"}, direction: forward.class)
        .where(right.id == "c").
    return (
        all_links: all_links.len(),
        connected: connected.len(),
        assigned: assigned.len(),
        reversed: reversed.get(position: 0).right.label,
        key_match: key_match.len()
    ).
end
