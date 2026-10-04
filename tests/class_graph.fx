class Entity
    key(id).
    def id: string.
end

class Employee extends Entity
    def name: string.
end

class Department
    key(id).
    def id: string.
end

def main() =>
    def graph: object := Graph().
    def edge := graph.add(
        Entity.class,
        Department.class,
        properties: {kind: "belongs_to"},
        direction: both.class
    ).
    def neighbors := graph.neighbors(Employee.class).
    def direct := Graph(Employee.class, Department.class).
    return (
        edge_from: edge.from,
        inherited_neighbor_count: neighbors.len(),
        adjacency_count: graph.adjacency.list.len(),
        direct_count: direct.list().len()
    ).
end
