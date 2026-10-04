class Department
    key(id).
    def id: string.
end

class Employee
    key(id).
    def id: string.
end

def Employee(id: "employee-1").
Link(
    from: Employee(id: "employee-1"),
    to: Department(id: "missing")
).
