class Department
    key(id).
    id: string.
    name: string.
end

class Employee
    key(id).
    id: string.
    name: string.
end

Department(id: "department-debug", name: "Research").
Employee(id: "employee-debug", name: "Ada").

def main() =>
    schema_graph := Graph(Employee.class, Department.class).
    employee := Employee.where(id: "employee-debug").first().
    department := Department.where(id: "department-debug").first().
    link := Link(
        from: employee,
        to: department,
        properties: {since: 2024, confidence: 0.9}
    ).
    rows := Employee.where(id: "employee-debug")
        .join(properties: {since: 2024}, direction: forward.class).
    return (
        class_edges: schema_graph.list().len(),
        links: rows.len(),
        department: rows.get(position: 0).right.name
    ).
end
