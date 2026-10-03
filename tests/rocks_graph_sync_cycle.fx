import "db".

class Employee
    key(id).
    id: string.
end

def main() =>
    synchronized := db.sync(path: "rocks_graph_sync_cycle_source.fx").
    Link(from: Employee(id: "e1"), to: Employee(id: "e2"), properties: {kind: "reports_to"}).
    Link(from: Employee(id: "e2"), to: Employee(id: "e1"), properties: {kind: "reports_to"}).
    direct := Employee().join(
        properties: {kind: "reports_to"},
        direction: forward.class
    ).
    recursive := Employee.where(id: "e1").recursive_join(
        properties: {kind: "reports_to"},
        direction: forward.class,
        min_depth: 1,
        max_depth: 5
    ).
    return (
        synchronized: synchronized,
        direct: direct.len(),
        recursive: recursive.len()
    ).
end
