class Department
    key(id).
    def id: string.
end

class Employee
    key(id).
    def id: string.
end

def Department(id: "department-1").
def Employee(id: "employee-1").
Link(
    from: Employee(id: "employee-1"),
    to: Department(id: "department-1"),
    properties: {confidence: 0.8}
).

def main() =>
    return Employee().join(direction: forward.class).where(properties.confidence).
end
