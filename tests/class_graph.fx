class Entity
    key(id).
    id: string.
end

class Employee extends Entity
    name: string.
end

class Department
    key(id).
    id: string.
end

def main() =>
    graph: object := Graph().
    edge := graph.add(
        Entity.class,
        Department.class,
        properties: {kind: "belongs_to"},
        direction: both.class
    ).
    neighbors := graph.neighbors(Employee.class).
    direct := Graph(Employee.class, Department.class).
    return (
        edge_from: edge.from,
        inherited_neighbor_count: neighbors.len(),
        adjacency_count: graph.adjacency.list.len(),
        direct_count: direct.list().len()
    ).
end
