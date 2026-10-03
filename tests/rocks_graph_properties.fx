class Department
    key(id).
    id: string.
    name: string.
end

class Employee
    key(id).
    id: string.
    name: string.
    department_id: string.
end

Department(id: "department-1", name: "Research").
Employee(id: "employee-1", name: "Ada", department_id: "department-1").
Link(
    from: Employee(id: "employee-1"),
    to: Department(id: "department-1"),
    properties: {kind: "works_in", since: 2024, confidence: 0.9}
).

def main() =>
    rows := Employee().join(
        properties: {kind: "works_in", confidence: 0.9},
        direction: forward.class
    ).
    strong := Employee().join(direction: forward.class)
        .where(properties.confidence >= 0.9).
    primary_key_match := Employee().join(direction: forward.class)
        .where(left.department_id == right.id).
    first := rows.get(position: 0).
    return (
        count: rows.len(),
        strong: strong.len(),
        primary_key_match: primary_key_match.len(),
        employee: first.left.name,
        department: first.right.name,
        since: first.properties.since
    ).
end
