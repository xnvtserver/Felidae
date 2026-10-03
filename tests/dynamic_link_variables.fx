class Employee
    key(id).
    id: string.
end

class Department
    key(id).
    id: string.
end

Employee(id: "employee-1").
Department(id: "department-1").

def main() =>
    employee := Employee.where(id: "employee-1").get(position: 0).
    department := Department.where(id: "department-1").get(position: 0).
    created := Link(
        from: employee,
        to: department,
        properties: {since: 2024, confidence: 0.9}
    ).
    rows := Employee().join(properties: {since: 2024}, direction: forward.class).
    return (
        created_to: created.to.id,
        count: rows.len(),
        confidence: rows.get(position: 0).properties.confidence
    ).
end
