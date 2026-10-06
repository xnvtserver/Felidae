class Department
    key(id).
    def id: string.
    def name: string.
end

class Employee
    key(id).
    def id: string.
    def name: string.
end

def Department(id: "department-debug", name: "Research").
def Employee(id: "employee-debug", name: "Ada").

def main() =>
    def schema_graph := Graph(Employee.class, Department.class).
    def employee := Employee.where(id: "employee-debug").first().
    def department := Department.where(id: "department-debug").first().
    def link := Link(
        from: employee,
        to: department,
        properties: {since: 2024, confidence: 0.9}
    ).
    def rows := Employee.where(id: "employee-debug")
        .join(properties: {since: 2024}, direction: forward.class).
    (
        class_edges: schema_graph.list().len(),
        links: rows.len(),
        department: rows.get(position: 0).right.name
    ).
end
