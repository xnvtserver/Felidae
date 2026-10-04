class Employee
    key(id).
    def id: string.
end

class Department
    key(id).
    def id: string.
end

def Employee(id: "employee-1").
def Department(id: "department-1").

def main() =>
    def employee := Employee.where(id: "employee-1").get(position: 0).
    def department := Department.where(id: "department-1").get(position: 0).
    def created := Link(
        from: employee,
        to: department,
        properties: {since: 2024, confidence: 0.9}
    ).
    def rows := Employee().join(properties: {since: 2024}, direction: forward.class).
    return (
        created_to: created.to.id,
        count: rows.len(),
        confidence: rows.get(position: 0).properties.confidence
    ).
end
