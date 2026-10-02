class Employee
    key(id)
    index(active)
    id: string
    name: string
    active: bool
end

Employee(id: "e1", name: "Grace", active: true)
Employee(id: "e2", name: "Ada", active: true)
Employee(id: "e3", name: "Lin", active: true)
Link(from: Employee(id: "e1"), to: Employee(id: "e1"), properties: {kind: "reports_to"})
Link(from: Employee(id: "e2"), to: Employee(id: "e1"), properties: {kind: "reports_to"})
Link(from: Employee(id: "e3"), to: Employee(id: "e2"), properties: {kind: "reports_to"})

def main() =>
    target := Employee.where(id: "e1").get(pos: 0),
    direct := Employee().where(active: true)
        .join(properties: {kind: "reports_to"}, direction: forward.class),
    limited := Employee().where(active: true)
        .join(properties: {kind: "reports_to"}, direction: forward.class)
        .limit(records: 1),
    both_from_source := Employee().where(id: "e1")
        .join(properties: {kind: "reports_to"}, direction: both.class),
    recursive := Employee().where(id: "e3")
        .recursive_join(
            properties: {kind: "reports_to"},
            direction: forward.class,
            min_depth: 1,
            max_depth: 5
        ),
    shortest := Employee().where(id: "e3")
        .shortest_path(
            to: target,
            properties: {kind: "reports_to"},
            direction: both.class,
            max_depth: 8
        ),
    return (
        direct_count: direct.len(),
        limited_count: limited.len(),
        both_count: both_from_source.len(),
        recursive_count: recursive.len(),
        shortest_depth: shortest.get(position: 0).depth
    )
end
