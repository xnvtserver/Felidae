class Department
    key(id).
    def id: string.
    def name: string.
end

class Employee
    key(id).
    def id: string.
    def name: string.
    def department_id: string.
end

def Department(id: "department-1", name: "Research").
def Employee(id: "employee-1", name: "Ada", department_id: "department-1").
Link(
    from: Employee(id: "employee-1"),
    to: Department(id: "department-1"),
    properties: {kind: "works_in", since: 2024, confidence: 0.9}
).

def main() =>
    def rows := Employee().join(
        properties: {kind: "works_in", confidence: 0.9},
        direction: forward.class
    ).
    def strong := Employee().join(direction: forward.class)
        .where(properties.confidence >= 0.9).
    def primary_key_match := Employee().join(direction: forward.class)
        .where(left.department_id == right.id).
    def first := rows.get(position: 0).
    return (
        count: rows.len(),
        strong: strong.len(),
        primary_key_match: primary_key_match.len(),
        employee: first.left.name,
        department: first.right.name,
        since: first.properties.since
    ).
end
